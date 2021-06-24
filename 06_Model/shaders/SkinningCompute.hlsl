StructuredBuffer<float3> srcPositionBuffer : register(t0);
StructuredBuffer<float3> srcNormalBuffer : register(t1);
StructuredBuffer<float4> srcJointWeightsBuffer : register(t2);
StructuredBuffer<uint4>  srcJointIndicesBuffer : register(t3);
StructuredBuffer<float4x4> srcJointMatrices : register(t4);


RWStructuredBuffer<float3> dstPositionBuffer : register(u0);
RWStructuredBuffer<float3> dstNormalBuffer : register(u1);

[numthreads(1, 1, 1)]
void mainCS( uint3 dtid : SV_DispatchThreadID )
{
    int index = dtid.x;
    float3 position = srcPositionBuffer[index];
    float3 normal = srcNormalBuffer[index];

    uint4 jointIndices = srcJointIndicesBuffer[index];
    float4 jointWeights = srcJointWeightsBuffer[index];

    float weights[4] = {
        jointWeights.x, jointWeights.y, jointWeights.z, jointWeights.w,
    };
    float4x4 matrices[4] = {
        srcJointMatrices[jointIndices.x],
        srcJointMatrices[jointIndices.y],
        srcJointMatrices[jointIndices.z],
        srcJointMatrices[jointIndices.w],
    };
    float4x4 mtx = (float4x4)0;
    for (int i = 0; i < 4; ++i) {
        mtx += matrices[i] * weights[i];
    }

    float4 deformPos = mul(float4(position, 1), mtx);
    float3 deformNrm = mul(normal, (float3x3)mtx);

    dstPositionBuffer[index] = deformPos.xyz;
    dstNormalBuffer[index] = normalize(deformNrm);
}