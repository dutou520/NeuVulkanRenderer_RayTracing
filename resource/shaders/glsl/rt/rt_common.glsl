#ifndef RT_COMMON_GLSL
#define RT_COMMON_GLSL

#define PI 3.14159265358979323846

struct GPUTriangle {
    vec4 v0;      // xyz = v0, w = materialId
    vec4 v1;      // xyz = v1, w = objectId
    vec4 v2;      // xyz = v2, w = 0
    vec4 n0;      // xyz = n0, w = uv0.x
    vec4 n1;      // xyz = n1, w = uv0.y
    vec4 n2;      // xyz = n2, w = uv1.x
    vec4 uv12;    // x = uv1.y, y = uv2.x, z = uv2.y, w = 0
    vec4 tangent; // xyz = tangent, w = bitangentSign (+1 or -1)
};

struct GPUMaterial {
    vec4 albedoAndType;        // xyz = albedo, w = type (0=diffuse, 1=metal, 2=glass, 3=emissive)
    vec4 emissionAndIntensity; // xyz = emission color, w = intensity
    vec4 params;               // x = roughness, y = metallic, z = ior, w = transmission
    ivec4 texIndices;          // x = albedoTexIdx, y = roughnessTexIdx, z = normalTexIdx, w = unused (-1 if none)
    vec4 texScales;            // x = normalScale, y = roughnessScale, z = 0, w = 0
};

struct RayPayload {
    vec3 radiance;
    vec3 throughput;
    vec3 nextOrigin;
    vec3 nextDirection;
    float lastBsdfPdf;
    uint hitAndTerm; // bit 0: hit, bit 1: terminate
    vec3 primaryNormal;
    float primaryDepth;
    uvec4 rngState;
};

struct ShadowPayload {
    uint isOccluded; // 0 = unoccluded, 1 = occluded
};

uvec4 pcg4d(uvec4 v) {
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.w;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v.w += v.y * v.z;
    v ^= v >> 16u;
    v.x += v.y * v.w;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v.w += v.y * v.z;
    return v;
}

// Murmur3 finalizer — 将小整数映射到高质量随机位，消除相邻像素/帧间的结构性关联
uint murmur3Mix(uint h) {
    h ^= h >> 16u;
    h *= 0x85ebca6bu;
    h ^= h >> 13u;
    h *= 0xc2b2ae35u;
    h ^= h >> 16u;
    return h;
}

// 使用像素坐标、帧索引、外部 seed 构建高质量的 4D 初始状态
// 各分量先经 murmur3 打散后再预热一轮 pcg4d，确保即使 frameIndex=0 时也无明显噪声 pattern
uvec4 initRng(uvec2 pixel, uint frame, uint seed) {
    uvec4 v;
    v.x = murmur3Mix(pixel.x ^ (pixel.y  << 16u));
    v.y = murmur3Mix(pixel.y ^ (pixel.x  << 16u));
    v.z = murmur3Mix(frame   ^ (seed     * 1234567u));
    v.w = murmur3Mix(seed    ^ (frame    * 7654321u));
    return pcg4d(v); // 预热一轮，进一步打散初始状态
}

float randF(inout uvec4 state) {
    state = pcg4d(state);
    return float(state.x >> 8u) * (1.0 / 16777216.0);
}

vec2 randF2(inout uvec4 state) {
    state = pcg4d(state);
    return vec2(float(state.x >> 8u), float(state.y >> 8u)) * (1.0 / 16777216.0);
}

void buildOrthonormalBasis(vec3 n, out vec3 b1, out vec3 b2) {
    float s = n.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (s + n.z);
    float b = n.x * n.y * a;
    b1 = vec3(1.0 + s * n.x * n.x * a, s * b, -s * n.x);
    b2 = vec3(b, s + n.y * n.y * a, -n.y);
}

vec3 cosineSampleHemisphere(vec3 normal, inout uvec4 rng) {
    vec2 r = randF2(rng);
    float phi = 2.0 * PI * r.x;
    float cosTheta = sqrt(1.0 - r.y);
    float sinTheta = sqrt(r.y);

    vec3 tangent, bitangent;
    buildOrthonormalBasis(normal, tangent, bitangent);

    return normalize(tangent * (cos(phi) * sinTheta) +
                     bitangent * (sin(phi) * sinTheta) +
                     normal * cosTheta);
}

float D_GGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH2 = NdotH * NdotH;
    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom + 1e-7);
}

float G1_Smith(float NdotV, float roughness) {
    float a = roughness * roughness;
    float k = a / 2.0;
    return NdotV / (NdotV * (1.0 - k) + k + 1e-7);
}

float G2_Smith(float NdotV, float NdotL, float roughness) {
    return G1_Smith(NdotV, roughness) * G1_Smith(NdotL, roughness);
}

vec3 F_Schlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float fresnelSchlick(float cosTheta, float refIdx) {
    float r0 = (1.0 - refIdx) / (1.0 + refIdx);
    r0 = r0 * r0;
    return r0 + (1.0 - r0) * pow(1.0 - cosTheta, 5.0);
}

vec3 sampleGGX(vec3 N, vec3 V, float roughness, out vec3 H, out float pdf, inout uvec4 rng) {
    vec2 r = randF2(rng);
    float a = roughness * roughness;
    float phi = 2.0 * PI * r.x;
    float cosTheta = sqrt(max(0.0, (1.0 - r.y) / (1.0 + (a * a - 1.0) * r.y)));
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));

    vec3 tangent, bitangent;
    buildOrthonormalBasis(N, tangent, bitangent);
    H = normalize(tangent * (sinTheta * cos(phi)) + bitangent * (sinTheta * sin(phi)) + N * cosTheta);

    vec3 L = reflect(-V, H);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);
    float D = D_GGX(NdotH, roughness);
    pdf = (D * NdotH) / max(4.0 * VdotH, 1e-4);
    return L;
}

float evalGGX_PDF(vec3 N, vec3 V, vec3 L, float roughness) {
    vec3 H = normalize(V + L);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);
    if (NdotH <= 0.0 || VdotH <= 0.0) return 0.0;
    float D = D_GGX(NdotH, roughness);
    return (D * NdotH) / max(4.0 * VdotH, 1e-4);
}

vec3 evalGGX_BRDF(vec3 N, vec3 V, vec3 L, float roughness, vec3 F0) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    if (NdotV <= 0.0 || NdotL <= 0.0) return vec3(0.0);

    vec3 H = normalize(V + L);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    float D = D_GGX(NdotH, roughness);
    float G = G2_Smith(NdotV, NdotL, roughness);
    vec3 F = F_Schlick(VdotH, F0);

    return (D * F * G) / (4.0 * NdotV * NdotL + 1e-7);
}

vec3 evalOrenNayar(vec3 N, vec3 V, vec3 L, float roughness, vec3 albedo) {
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    if (NdotL <= 0.0 || NdotV <= 0.0) return vec3(0.0);

    float sigma = roughness;
    float sigma2 = sigma * sigma;
    float A = 1.0 - 0.5 * (sigma2 / (sigma2 + 0.33));
    float B = 0.45 * (sigma2 / (sigma2 + 0.09));

    vec3 l_proj = L - N * NdotL;
    vec3 v_proj = V - N * NdotV;
    float lenL = length(l_proj);
    float lenV = length(v_proj);
    float cosPhiDiff = (lenL > 1e-5 && lenV > 1e-5) ? clamp(dot(l_proj, v_proj) / (lenL * lenV), -1.0, 1.0) : 0.0;

    float thetaL = acos(clamp(NdotL, 0.0, 1.0));
    float thetaV = acos(clamp(NdotV, 0.0, 1.0));
    float alpha = max(thetaL, thetaV);
    float beta = min(thetaL, thetaV);

    float s = max(0.0, cosPhiDiff) * sin(alpha) * tan(beta);
    return (albedo / PI) * (A + B * s);
}

#endif // RT_COMMON_GLSL
