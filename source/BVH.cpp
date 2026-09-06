#include "BVH.h"
#include "neuLog.h"
#include <algorithm>
#include <chrono>

namespace neurender {

void BVH::Build(const std::vector<TriangleData>& triangles) {
    auto startTime = std::chrono::high_resolution_clock::now();

    m_Triangles = triangles;
    m_BuildNodes.clear();
    m_FlatNodes.clear();
    m_GPUTriangles.clear();
    m_EmissiveIndices.clear();

    if (m_Triangles.empty()) {
        LOG_W("BVH::Build called with empty triangles!");
        return;
    }

    BuildNode root;
    root.primStart = 0;
    root.primCount = static_cast<int>(m_Triangles.size());
    for (const auto& tri : m_Triangles) {
        root.box.Grow(tri.v0);
        root.box.Grow(tri.v1);
        root.box.Grow(tri.v2);
    }
    m_BuildNodes.push_back(root);

    Subdivide(0, 0);

    // Flatten tree
    m_FlatNodes.reserve(m_BuildNodes.size());
    m_GPUTriangles.resize(m_Triangles.size());
    Flatten(0);

    auto endTime = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    LOG_I("SAH BVH built: {} triangles, {} nodes, {:.2f} ms",
          m_Triangles.size(), m_FlatNodes.size(), ms);
}

int BVH::Subdivide(int nodeIdx, int depth) {
    BuildNode& node = m_BuildNodes[nodeIdx];
    if (node.primCount <= 4 || depth >= 32) {
        return nodeIdx;
    }

    // 1. Calculate centroid bounding box
    AABB centroidBox;
    for (int i = 0; i < node.primCount; ++i) {
        centroidBox.Grow(m_Triangles[node.primStart + i].Centroid());
    }

    glm::vec3 cExtent = centroidBox.max - centroidBox.min;
    if (cExtent.x < 1e-6f && cExtent.y < 1e-6f && cExtent.z < 1e-6f) {
        return nodeIdx; // All triangle centroids coincide
    }

    // 2. 32-Bin SAH Evaluation
    constexpr int NUM_BINS = 32;
    constexpr float COST_TRAVERSAL = 1.0f;
    constexpr float COST_INTERSECT = 1.5f;

    float nodeArea = node.box.Area();
    if (nodeArea < 1e-6f) nodeArea = 1e-6f;

    float bestCost = COST_INTERSECT * static_cast<float>(node.primCount);
    int bestAxis = -1;
    int bestSplitBin = -1;

    struct Bin {
        AABB box;
        int count = 0;
    };

    for (int axis = 0; axis < 3; ++axis) {
        if (cExtent[axis] < 1e-6f) continue;

        Bin bins[NUM_BINS];
        float scale = static_cast<float>(NUM_BINS) / cExtent[axis];
        float minVal = centroidBox.min[axis];

        for (int i = 0; i < node.primCount; ++i) {
            const auto& tri = m_Triangles[node.primStart + i];
            int b = static_cast<int>((tri.Centroid()[axis] - minVal) * scale);
            b = std::clamp(b, 0, NUM_BINS - 1);
            bins[b].count++;
            bins[b].box.Grow(tri.v0);
            bins[b].box.Grow(tri.v1);
            bins[b].box.Grow(tri.v2);
        }

        // Left-to-right prefix scan
        AABB leftBoxes[NUM_BINS - 1];
        int leftCounts[NUM_BINS - 1];
        AABB curLeftBox;
        int curLeftCount = 0;

        for (int b = 0; b < NUM_BINS - 1; ++b) {
            curLeftCount += bins[b].count;
            curLeftBox.Grow(bins[b].box);
            leftCounts[b] = curLeftCount;
            leftBoxes[b] = curLeftBox;
        }

        // Right-to-left suffix scan & cost evaluation
        AABB curRightBox;
        int curRightCount = 0;

        for (int b = NUM_BINS - 1; b > 0; --b) {
            curRightCount += bins[b].count;
            curRightBox.Grow(bins[b].box);

            int splitIdx = b - 1;
            int nL = leftCounts[splitIdx];
            int nR = curRightCount;

            if (nL == 0 || nR == 0) continue;

            float areaL = leftBoxes[splitIdx].Area();
            float areaR = curRightBox.Area();

            float cost = COST_TRAVERSAL + COST_INTERSECT * (areaL * nL + areaR * nR) / nodeArea;

            if (cost < bestCost) {
                bestCost = cost;
                bestAxis = axis;
                bestSplitBin = splitIdx;
            }
        }
    }

    // 3. Early termination check
    if (bestAxis == -1) {
        if (node.primCount <= 8) {
            return nodeIdx; // Keep as leaf
        }
        // Triangle count still large: split along widest centroid axis
        bestAxis = 0;
        if (cExtent.y > cExtent.x && cExtent.y > cExtent.z) bestAxis = 1;
        else if (cExtent.z > cExtent.x) bestAxis = 2;
    }

    int mid = -1;

    if (bestSplitBin != -1) {
        float scale = static_cast<float>(NUM_BINS) / cExtent[bestAxis];
        float minVal = centroidBox.min[bestAxis];

        auto it = std::partition(
            m_Triangles.begin() + node.primStart,
            m_Triangles.begin() + node.primStart + node.primCount,
            [&](const TriangleData& tri) {
                int b = static_cast<int>((tri.Centroid()[bestAxis] - minVal) * scale);
                b = std::clamp(b, 0, NUM_BINS - 1);
                return b <= bestSplitBin;
            }
        );
        mid = static_cast<int>(it - m_Triangles.begin());
    }

    // Fallback to median partition if binned partition was degenerate
    if (mid <= node.primStart || mid >= node.primStart + node.primCount) {
        mid = node.primStart + node.primCount / 2;
        std::nth_element(
            m_Triangles.begin() + node.primStart,
            m_Triangles.begin() + mid,
            m_Triangles.begin() + node.primStart + node.primCount,
            [bestAxis](const TriangleData& a, const TriangleData& b) {
                return a.Centroid()[bestAxis] < b.Centroid()[bestAxis];
            }
        );
    }

    int countLeft = mid - node.primStart;
    int countRight = node.primCount - countLeft;

    if (countLeft == 0 || countRight == 0) {
        return nodeIdx;
    }

    BuildNode leftNode;
    leftNode.primStart = node.primStart;
    leftNode.primCount = countLeft;
    for (int i = 0; i < countLeft; ++i) {
        const auto& t = m_Triangles[leftNode.primStart + i];
        leftNode.box.Grow(t.v0);
        leftNode.box.Grow(t.v1);
        leftNode.box.Grow(t.v2);
    }

    BuildNode rightNode;
    rightNode.primStart = mid;
    rightNode.primCount = countRight;
    for (int i = 0; i < countRight; ++i) {
        const auto& t = m_Triangles[rightNode.primStart + i];
        rightNode.box.Grow(t.v0);
        rightNode.box.Grow(t.v1);
        rightNode.box.Grow(t.v2);
    }

    int leftIdx = static_cast<int>(m_BuildNodes.size());
    m_BuildNodes.push_back(leftNode);
    int rightIdx = static_cast<int>(m_BuildNodes.size());
    m_BuildNodes.push_back(rightNode);

    m_BuildNodes[nodeIdx].left = leftIdx;
    m_BuildNodes[nodeIdx].right = rightIdx;
    m_BuildNodes[nodeIdx].primCount = 0; // mark internal

    Subdivide(leftIdx, depth + 1);
    Subdivide(rightIdx, depth + 1);

    return nodeIdx;
}

void BVH::Flatten(int nodeIdx) {
    const BuildNode& bn = m_BuildNodes[nodeIdx];
    int flatIdx = static_cast<int>(m_FlatNodes.size());
    m_FlatNodes.push_back(GPUBVHNode{});

    if (bn.primCount > 0) { // Leaf node
        m_FlatNodes[flatIdx].aabbMinAndLeft = glm::vec4(bn.box.min, static_cast<float>(bn.primStart));
        m_FlatNodes[flatIdx].aabbMaxAndCount = glm::vec4(bn.box.max, static_cast<float>(bn.primCount));

        for (int i = 0; i < bn.primCount; ++i) {
            int triIdx = bn.primStart + i;
            const auto& src = m_Triangles[triIdx];
            GPUTriangle& dst = m_GPUTriangles[triIdx];
            dst.v0 = glm::vec4(src.v0, static_cast<float>(src.materialId));
            dst.v1 = glm::vec4(src.v1, static_cast<float>(src.objectId));
            dst.v2 = glm::vec4(src.v2, 0.0f);
            dst.n0 = glm::vec4(src.n0, src.uv0.x);
            dst.n1 = glm::vec4(src.n1, src.uv0.y);
            dst.n2 = glm::vec4(src.n2, src.uv1.x);
            dst.uv12 = glm::vec4(src.uv1.y, src.uv2.x, src.uv2.y, 0.0f);
            dst.tangent = src.tangent;
        }
    } else { // Internal node
        m_FlatNodes[flatIdx].aabbMinAndLeft = glm::vec4(bn.box.min, 0.0f);
        m_FlatNodes[flatIdx].aabbMaxAndCount = glm::vec4(bn.box.max, 0.0f);

        // Flatten left child
        int leftFlat = static_cast<int>(m_FlatNodes.size());
        Flatten(bn.left);
        // Flatten right child
        int rightFlat = static_cast<int>(m_FlatNodes.size());
        Flatten(bn.right);

        // Store left child and right child indices explicitly
        m_FlatNodes[flatIdx].aabbMinAndLeft.w = static_cast<float>(leftFlat);
        m_FlatNodes[flatIdx].aabbMaxAndCount.w = static_cast<float>(-rightFlat);
    }
}

} // namespace neurender
