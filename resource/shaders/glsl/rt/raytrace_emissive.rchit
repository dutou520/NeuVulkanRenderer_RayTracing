#version 460
#extension GL_EXT_ray_tracing : require

#include "rt_common.glsl"

hitAttributeEXT vec2 attribs;

layout(location = 0) rayPayloadInEXT RayPayload prd;

layout(std430, set = 0, binding = 2) readonly buffer TriangleBuffer { GPUTriangle triangles[]; };
layout(std430, set = 0, binding = 3) readonly buffer MaterialBuffer { GPUMaterial materials[]; };

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

    vec3 n = normalize(w * tri.n0.xyz + u * tri.n1.xyz + v * tri.n2.xyz);
    vec3 gn = normalize(cross(tri.v1.xyz - tri.v0.xyz, tri.v2.xyz - tri.v0.xyz));
    bool frontFace = dot(gl_WorldRayDirectionEXT, gn) < 0.0;
    vec3 N = frontFace ? n : -n;

    GPUMaterial mat = materials[int(tri.v0.w)];
    vec3 emission = mat.emissionAndIntensity.xyz * mat.emissionAndIntensity.w;

    if (prd.primaryDepth > 9999.0) {
        prd.primaryNormal = N;
        prd.primaryDepth = gl_HitTEXT;
    }

    int numLights = int(u_Push.postParams.w);
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

    prd.hitAndTerm = 3u; // hit=1, term=1 (terminate path on emissive)
}
