RaytracingAccelerationStructure gRtScene : register(t0);
RWTexture2D<float4> gOutput: register(u0);

struct Payload {
    float3 color;
};
struct MyAttribute {
    float2 barys;
};

[shader("raygeneration")]
void mainRayGen() {
    uint2 launchIndex = DispatchRaysIndex().xy;
    float2 dims = float2(DispatchRaysDimensions().xy);
    
    float2 d = (launchIndex.xy + 0.5) / dims.xy * 2.0 - 1.0;

    RayDesc rayDesc;
    rayDesc.Origin = float3(d.x, -d.y, 1);
    rayDesc.Direction = float3(0, 0, -1);
    rayDesc.TMin = 0;
    rayDesc.TMax = 100000;

    Payload payload;
    payload.color = float3(0, 0, 0.5);

    RAY_FLAG flags = RAY_FLAG_NONE;
    uint rayMask = 0xFF;

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

[shader("miss")]
void mainMS(inout Payload payload) {
    payload.color = float3(0.4, 0.8, 0.9);
}

[shader("closesthit")]
void mainCHS(inout Payload payload, MyAttribute attrib){
    payload.color = float3(1, 1, 1);
    float3 col = 0;
    col.xy = attrib.barys;
    col.z = 1.0 - col.x - col.y;
    payload.color = col;
}
