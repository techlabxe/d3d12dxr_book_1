#include "common.hlsli"

[shader("miss")]
void mainMiss(inout Payload payload) {
    // ���݂̃��C���x�N�g���Ƃ��ăL���[�u�}�b�v����摜�𓾂�.
    float4 color = gBackground.SampleLevel(
        gSampler, WorldRayDirection(), 0.0);
    payload.color = color.xyz;
}
