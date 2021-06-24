struct Vertex {
    float3 Position;
    float3 Normal;
    float4 Color;
};
struct SceneCB {
    matrix mtxView;       // ビュー行列.
    matrix mtxProj;       // プロジェクション行列.
    matrix mtxViewInv;    // ビュー逆行列.
    matrix mtxProjInv;    // プロジェクション逆行列.
    float4 lightDirection; // 平行光源の向き.
    float4 lightColor;     // 平行光源色.
    float4 ambientColor;   // 環境光.
};

// Global Root Signature
RaytracingAccelerationStructure gRtScene : register(t0);
ConstantBuffer<SceneCB> gSceneParam : register(b0);

// Local Root Signature (for RayGen)
RWTexture2D<float4> gOutput: register(u0);

// Local Root Signature (for HitGroup)
StructuredBuffer<uint>   indexBuffer : register(t0, space1);
StructuredBuffer<Vertex> vertexBuffer : register(t1, space1);

struct Payload {
    float3 color;
};
struct MyAttribute {
    float2 barys;
};

inline float3 CalcBarycentrics(float2 barys)
{
    return float3(
        1.0 - barys.x - barys.y,
        barys.x,
        barys.y);
}

Vertex GetHitVertex(MyAttribute attrib)
{
    Vertex v = (Vertex)0;
    float3 barycentrics = CalcBarycentrics(attrib.barys);
    uint vertexId = PrimitiveIndex() * 3; // Triangle List のため.

    float weights[3] = {
        barycentrics.x,barycentrics.y,barycentrics.z
    };
    for (int i = 0; i < 3; ++i)
    {
        uint index = indexBuffer[vertexId + i];
        float w = weights[i];
        v.Position += vertexBuffer[index].Position * w;
        v.Normal += vertexBuffer[index].Normal * w;
        v.Color += vertexBuffer[index].Color * w;
    }
    v.Normal = normalize(v.Normal);
    return v;
}

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
    rayDesc.Direction = mul(mtxViewInv, float4(target.xyz, 0)).xyz;

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

    // 結果格納.
    gOutput[launchIndex.xy] = float4(col, 1);
}

[shader("miss")]
void mainMS(inout Payload payload) {
    payload.color = float3(0.4, 0.8, 0.9);
}

[shader("closesthit")]
void mainCHS(inout Payload payload, MyAttribute attrib){
    Vertex vtx = GetHitVertex(attrib);
    //// Lambert ライティングを行う.
    float3 lightDir = -normalize(gSceneParam.lightDirection.xyz);

    float nl = saturate(dot(vtx.Normal, lightDir));

    float3 lightColor = gSceneParam.lightColor.xyz;
    float3 ambientColor = gSceneParam.ambientColor.xyz;
    float3 color = 0;
    color += lightColor* vtx.Color.xyz* nl;
    color += ambientColor * vtx.Color.xyz;
    payload.color = color;
}
