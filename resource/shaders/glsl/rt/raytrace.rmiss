#version 460
#extension GL_EXT_ray_tracing : require

#include "rt_common.glsl"
#include "../nishita_sky.glsl"

layout(location = 0) rayPayloadInEXT RayPayload prd;

layout(set = 0, binding = 6) uniform SkyParams {
    vec4 sunDirAndIntensity;
    vec4 atmosphereParams;
    vec4 groundAlbedo;
} u_Sky;

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
    vec3 rayDir = gl_WorldRayDirectionEXT;
    vec3 skyColor;

    float sunEnabled = u_Sky.groundAlbedo.w;

    if (u_Sky.atmosphereParams.x > 0.5) {
        skyColor = EvaluateNishitaSky(rayDir, u_Sky.sunDirAndIntensity.xyz, u_Sky.sunDirAndIntensity.w,
                                      u_Sky.atmosphereParams.y, u_Sky.atmosphereParams.z,
                                      u_Sky.atmosphereParams.w, u_Sky.groundAlbedo.xyz,
                                      sunEnabled);
    } else {
        skyColor = u_Push.envAndTone.xyz;
        if (sunEnabled > 0.5) {
            float cosTheta = dot(rayDir, normalize(u_Sky.sunDirAndIntensity.xyz));
            float sunAngle = max(0.001, u_Sky.atmosphereParams.w);
            float minCosSun = cos(sunAngle);
            if (cosTheta > minCosSun) {
                float diskIntensity = smoothstep(minCosSun, minCosSun + (1.0 - minCosSun) * 0.1, cosTheta);
                skyColor += vec3(1.0, 0.98, 0.92) * (u_Sky.sunDirAndIntensity.w * 60.0) * diskIntensity;
            }
        }
    }

    prd.radiance += prd.throughput * skyColor;
    prd.hitAndTerm = 2u; // hit=0, term=1
}
