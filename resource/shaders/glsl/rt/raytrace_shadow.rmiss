#version 460
#extension GL_EXT_ray_tracing : require

#include "rt_common.glsl"

layout(location = 1) rayPayloadInEXT ShadowPayload shadowPrd;

void main() {
    shadowPrd.isOccluded = 0u; // Missed all blockers, unoccluded
}
