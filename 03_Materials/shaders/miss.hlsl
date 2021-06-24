#include "common.hlsli"

[shader("miss")]
void mainMiss(inout Payload payload) {
    // 現在のレイをベクトルとしてキューブマップから画像を得る.
    float4 color = gBackground.SampleLevel(
        gSampler, WorldRayDirection(), 0.0);
    payload.color = color.xyz;
}
