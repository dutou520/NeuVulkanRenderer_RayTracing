#version 460
#extension GL_EXT_ray_tracing : require

#include "rt_common.glsl"

hitAttributeEXT vec2 attribs;

layout(location = 0) rayPayloadInEXT RayPayload prd;

layout(std430, set = 0, binding = 2) readonly buffer TriangleBuffer { GPUTriangle triangles[]; };
layout(std430, set = 0, binding = 3) readonly buffer MaterialBuffer { GPUMaterial materials[]; };

void main() {
    int triIdx = gl_PrimitiveID;
    GPUTriangle tri = triangles[triIdx];
    float u = attribs.x;
    float v = attribs.y;
    float w = 1.0 - u - v;

    vec3 pos = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
    vec3 n = normalize(w * tri.n0.xyz + u * tri.n1.xyz + v * tri.n2.xyz);
    vec3 gn = normalize(cross(tri.v1.xyz - tri.v0.xyz, tri.v2.xyz - tri.v0.xyz));
    bool frontFace = dot(gl_WorldRayDirectionEXT, gn) < 0.0;
    vec3 N = frontFace ? n : -n;

    GPUMaterial mat = materials[int(tri.v0.w)];
    vec3 albedo = mat.albedoAndType.xyz;
    float ior = mat.params.z > 0.1 ? mat.params.z : 1.5;

    if (prd.primaryDepth > 9999.0) {
        prd.primaryNormal = N;
        prd.primaryDepth = gl_HitTEXT;
    }

    prd.hitAndTerm = 1u;

    vec3 V = -gl_WorldRayDirectionEXT;
    float eta = frontFace ? (1.0 / ior) : ior;
    float cosTheta = min(dot(V, N), 1.0);
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));

    bool cannotRefract = (eta * sinTheta) > 1.0;
    float reflectProb = fresnelSchlick(cosTheta, eta);

    vec3 nextDir;
    if (cannotRefract || reflectProb > randF(prd.rngState)) {
        nextDir = reflect(gl_WorldRayDirectionEXT, N);
    } else {
        nextDir = refract(gl_WorldRayDirectionEXT, N, eta);
    }

    prd.throughput *= albedo;
    prd.lastBsdfPdf = -1.0; // Delta distribution, no MIS
    prd.nextOrigin = pos + nextDir * 0.001;
    prd.nextDirection = nextDir;
}
