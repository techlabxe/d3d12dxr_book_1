#include "common.hlsli"

[shader("miss")]
void mainMiss(inout Payload payload) {
    payload.color.xyz = 0.1;
}

[shader("miss")]
void shadowMiss(inout ShadowPayload payload) {
    payload.isHit = false;
}
