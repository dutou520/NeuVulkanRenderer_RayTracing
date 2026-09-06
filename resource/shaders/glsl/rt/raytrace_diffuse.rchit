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
    int matType = int(mat.albedoAndType.w);
    vec3 emission = mat.emissionAndIntensity.xyz * mat.emissionAndIntensity.w;
    float roughness = clamp(mat.params.x, 0.001, 1.0);
    float metallic = clamp(mat.params.y, 0.0, 1.0);
    float ior = mat.params.z > 0.1 ? mat.params.z : 1.5;

    // Apply normal map if available
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

    // Apply roughness map if available
    int roughTex = mat.texIndices.y;
    if (roughTex >= 0 && roughTex < 64) {
        float rSample = texture(u_Textures[nonuniformEXT(roughTex)], uv).r;
        roughness = clamp(roughness * rSample * mat.texScales.y, 0.001, 1.0);
    }

    // Apply albedo map if available
    int albTex = mat.texIndices.x;
    if (albTex >= 0 && albTex < 64) {
        albedo *= texture(u_Textures[nonuniformEXT(albTex)], uv).rgb;
    }

    // Primary hit normal/depth for denoiser
    if (prd.primaryDepth > 9999.0) {
        prd.primaryNormal = N;
        prd.primaryDepth = gl_HitTEXT;
    }

    prd.hitAndTerm = 1u; // hit=1, term=0 by default

    // 1. Emissive surface (Direct emission or light hit with MIS)
    int numLights = int(u_Push.postParams.w);
    if (length(emission) > 0.001) {
        if (prd.lastBsdfPdf < 0.0 || numLights <= 0) {
            prd.radiance += prd.throughput * emission;
        } else {
            float distSq = gl_HitTEXT * gl_HitTEXT;
            float cosLight = abs(dot(-gl_WorldRayDirectionEXT, gn));
            float lightArea = 0.5 * length(cross(tri.v1.xyz - tri.v0.xyz, tri.v2.xyz - tri.v0.xyz));
            float lightPdf = distSq / (max(lightArea, 1e-4) * max(cosLight, 1e-4) * float(max(numLights, 1)));

            float misWeight = prd.lastBsdfPdf / (prd.lastBsdfPdf + lightPdf);
            prd.radiance += prd.throughput * emission * misWeight;
        }
        prd.hitAndTerm = 3u; // terminate path
        return;
    }

    vec3 V = -gl_WorldRayDirectionEXT;
    float NdotV = max(dot(N, V), 0.0);

    // 2. Next Event Estimation (Direct Light Sampling with MIS) for non-delta surfaces
    if (numLights > 0 && matType != 2 && matType != 3) {
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

                float bsdfPdf = 0.0;
                vec3 f_bsdf = vec3(0.0);

                if (matType == 0) {
                    // Diffuse Lambertian
                    bsdfPdf = cosTheta / PI;
                    f_bsdf = albedo / PI;
                } else if (matType == 1) {
                    // GGX Metal
                    vec3 F0 = mix(vec3(0.04), albedo, metallic);
                    bsdfPdf = evalGGX_PDF(N, V, L, roughness);
                    f_bsdf = evalGGX_BRDF(N, V, L, roughness, F0);
                }

                float misWeight = lightPdf / (lightPdf + bsdfPdf);
                vec3 direct = prd.throughput * f_bsdf * cosTheta * (lEmission / max(lightPdf, 1e-5)) * misWeight;
                prd.radiance += direct;
            }
        }
    }

    // 3. BSDF Scatter & Importance Sampling
    vec3 nextDir;
    if (matType == 0) {
        // Diffuse Lambertian
        nextDir = cosineSampleHemisphere(N, prd.rngState);
        float cosTheta = max(dot(N, nextDir), 0.0);
        prd.lastBsdfPdf = cosTheta / PI;
        prd.throughput *= albedo;
    } else if (matType == 1) {
        // GGX Metal
        vec3 H;
        float ggxPdf;
        nextDir = sampleGGX(N, V, roughness, H, ggxPdf, prd.rngState);
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
    } else if (matType == 2) {
        // Glass (Dielectric)
        float eta = frontFace ? (1.0 / ior) : ior;
        float cosTheta = min(dot(V, N), 1.0);
        float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));

        bool cannotRefract = (eta * sinTheta) > 1.0;
        float reflectProb = fresnelSchlick(cosTheta, eta);

        if (cannotRefract || reflectProb > randF(prd.rngState)) {
            nextDir = reflect(gl_WorldRayDirectionEXT, N);
        } else {
            nextDir = refract(gl_WorldRayDirectionEXT, N, eta);
        }
        prd.throughput *= albedo;
        prd.lastBsdfPdf = -1.0;
    } else {
        prd.hitAndTerm = 3u;
        return;
    }

    prd.nextOrigin = pos + nextDir * 0.001;
    prd.nextDirection = nextDir;
}
