#include "common.hlsli"


// Local Root Signature (for RayGen)
RWTexture2D<float4> gOutput: register(u0);

[shader("raygeneration")]
void mainRayGen() {
    uint2 launchIndex = DispatchRaysIndex().xy;
    float2 dims = float2(DispatchRaysDimensions().xy);

    float2 d = (launchIndex.xy + 0.5) / dims.xy * 2.0 - 1.0;
    float aspect = dims.x / dims.y;

    matrix mtxViewInv = gSceneParam.mtxViewInv;
    matrix mtxProjInv = gSceneParam.mtxProjInv;

    RayDesc rayDesc;
    rayDesc.Origin = mul(mtxViewInv, float4(0, 0, 0, 1)).xyz;

    float4 target = mul(mtxProjInv, float4(d.x, -d.y, 1, 1));
    rayDesc.Direction = normalize(mul(mtxViewInv, float4(target.xyz, 0)).xyz);

    rayDesc.TMin = 0;
    rayDesc.TMax = 100000;

    Payload payload;
    payload.color = float3(0, 0, 0.5);
    payload.recursive = 0;

    RAY_FLAG flags = RAY_FLAG_NONE;
    //flags |= RAY_FLAG_CULL_BACK_FACING_TRIANGLES;
    uint rayMask = 0xFF;

    if (gSceneParam.flags.x == 0) {
        rayMask = ~(0x08);
    } else if (gSceneParam.flags.y == 0) {
        rayMask = ~(0x08);
    }
    

    TraceRay(
        gRtScene,
        flags,
        rayMask,
        0, // ray index
        1, // MultiplierForGeometryContrib
        0, // miss index
        rayDesc,
        payload);
    float3 col = payload.color;

    // Œ‹‰ÊŠi”[.
    gOutput[launchIndex.xy] = float4(col, 1);
}
