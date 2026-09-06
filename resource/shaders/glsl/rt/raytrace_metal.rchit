#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable

#include "rt_common.glsl"

hitAttributeEXT vec2 attribs;

layout(location = 0) rayPayloadInEXT RayPayload prd;
layout(location = 1) rayPayloadEXT ShadowPayload shadowPrd;

layout(set = 0, binding = 0) uniform accelerationStructureEXT topLevelAS;
layout(std430, set = 0, binding = 2) readonly buffer TriangleBuffer { GPUTriangle triangles[]; };
layout(std430, set = 0, binding = 3) readonly buffer MaterialBuffer { GPUMaterial materials[]; };
layout(std430, set = 0, binding = 4) readonly buffer LightBuffer { int lightTriangles[]; };
layout(set = 0, binding = 5) uniform sampler2D u_Textures[64];

layout(push_constant) uniform PushConstants {
    vec4 camPos;
    vec4 camFront;
    vec4 camRight;
    vec4 camUp;
    uvec4 renderParams;
    vec4 envAndTone;
    vec4 postParams;
} u_Push;

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

    vec2 uv0 = vec2(tri.n0.w, tri.n1.w);
    vec2 uv1 = vec2(tri.n2.w, tri.uv12.x);
    vec2 uv2 = vec2(tri.uv12.y, tri.uv12.z);
    vec2 uv = w * uv0 + u * uv1 + v * uv2;

    GPUMaterial mat = materials[int(tri.v0.w)];
    vec3 albedo = mat.albedoAndType.xyz;
    float roughness = clamp(mat.params.x, 0.001, 1.0);
    float metallic = clamp(mat.params.y, 0.0, 1.0);

    int normTex = mat.texIndices.z;
    if (normTex >= 0 && normTex < 64) {
        vec3 T = normalize(tri.tangent.xyz);
        T = normalize(T - dot(T, N) * N);
        vec3 B = cross(N, T) * tri.tangent.w;
        mat3 TBN = mat3(T, B, N);
        vec3 nSample = texture(u_Textures[nonuniformEXT(normTex)], uv).rgb * 2.0 - 1.0;
        nSample.xy *= mat.texScales.x;
        N = normalize(TBN * nSample);
    }

    int roughTex = mat.texIndices.y;
    if (roughTex >= 0 && roughTex < 64) {
        float rSample = texture(u_Textures[nonuniformEXT(roughTex)], uv).r;
        roughness = clamp(roughness * rSample * mat.texScales.y, 0.001, 1.0);
    }

    int albTex = mat.texIndices.x;
    if (albTex >= 0 && albTex < 64) {
        albedo *= texture(u_Textures[nonuniformEXT(albTex)], uv).rgb;
    }

    if (prd.primaryDepth > 9999.0) {
        prd.primaryNormal = N;
        prd.primaryDepth = gl_HitTEXT;
    }

    prd.hitAndTerm = 1u;
    vec3 V = -gl_WorldRayDirectionEXT;
    float NdotV = max(dot(N, V), 0.0);

    // Next Event Estimation for GGX Metal
    int numLights = int(u_Push.postParams.w);
    if (numLights > 0) {
        int lightPick = int(randF(prd.rngState) * float(numLights)) % numLights;
        int lightTriIdx = lightTriangles[lightPick];
        GPUTriangle ltri = triangles[lightTriIdx];

        vec2 rLight = randF2(prd.rngState);
        float r1 = rLight.x;
        float r2 = rLight.y;
        float sqr1 = sqrt(r1);
        vec3 lightPt = (1.0 - sqr1) * ltri.v0.xyz + (sqr1 * (1.0 - r2)) * ltri.v1.xyz + (sqr1 * r2) * ltri.v2.xyz;
        vec3 lightNormal = normalize(cross(ltri.v1.xyz - ltri.v0.xyz, ltri.v2.xyz - ltri.v0.xyz));
        float lightArea = 0.5 * length(cross(ltri.v1.xyz - ltri.v0.xyz, ltri.v2.xyz - ltri.v0.xyz));

        vec3 toLight = lightPt - pos;
        float distSq = dot(toLight, toLight);
        float dist = sqrt(distSq);
        vec3 L = toLight / dist;

        float cosTheta = dot(N, L);
        float cosLight = abs(dot(-L, lightNormal));

        if (cosTheta > 0.0 && cosLight > 0.001) {
            vec3 shadowOrigin = pos + N * 0.001;
            shadowPrd.isOccluded = 1u;
            traceRayEXT(topLevelAS,
                        gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT,
                        0xFF, 1, 0, 1, shadowOrigin, 0.0005, L, dist - 0.002, 1);

            if (shadowPrd.isOccluded == 0u) {
                GPUMaterial lmat = materials[int(ltri.v0.w)];
                vec3 lEmission = lmat.emissionAndIntensity.xyz * lmat.emissionAndIntensity.w;
                float lightPdf = distSq / (max(lightArea, 1e-4) * cosLight * float(numLights));

                vec3 F0 = mix(vec3(0.04), albedo, metallic);
                float bsdfPdf = evalGGX_PDF(N, V, L, roughness);
                vec3 f_bsdf = evalGGX_BRDF(N, V, L, roughness, F0);

                float misWeight = lightPdf / (lightPdf + bsdfPdf);
                vec3 direct = prd.throughput * f_bsdf * cosTheta * (lEmission / max(lightPdf, 1e-5)) * misWeight;
                prd.radiance += direct;
            }
        }
    }

    vec3 H;
    float ggxPdf;
    vec3 nextDir = sampleGGX(N, V, roughness, H, ggxPdf, prd.rngState);
    float NdotL = dot(N, nextDir);
    if (NdotL <= 0.0 || ggxPdf <= 1e-7) {
        prd.hitAndTerm = 3u; // terminate
        return;
    }

    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    float VdotH = max(dot(V, H), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    vec3 F = F_Schlick(VdotH, F0);
    float G = G2_Smith(NdotV, NdotL, roughness);

    vec3 weight = F * (G * VdotH / max(1e-5, NdotV * NdotH));
    prd.throughput *= weight;
    prd.lastBsdfPdf = ggxPdf;
    prd.nextOrigin = pos + nextDir * 0.001;
    prd.nextDirection = nextDir;
}
