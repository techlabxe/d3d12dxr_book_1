#include "common.hlsli"

struct VertexPN {
    float3 Position;
    float3 Normal;
};

// Local Root Signature (for HitGroup)
StructuredBuffer<uint>   indexBuffer : register(t0, space1);
StructuredBuffer<VertexPN> vertexBuffer: register(t1, space1);


VertexPN GetHitVertexPN(MyAttribute attrib)
{
    VertexPN v = (VertexPN)0;
    float3 barycentrics = CalcBarycentrics(attrib.barys);
    uint start = PrimitiveIndex() * 3; // Triangle List のため.

    float3 positions[3], normals[3];
    for (int i = 0; i < 3; ++i) {
        uint index = indexBuffer[start + i];
        positions[i] = vertexBuffer[index].Position;
        normals[i] = vertexBuffer[index].Normal;
    }
    v.Position = CalcHitAttribute3(positions, attrib.barys);
    v.Normal = CalcHitAttribute3(normals, attrib.barys);
    v.Normal = normalize(v.Normal);
    return v;
}


[shader("closesthit")]
void mainFloorCHS(inout Payload payload, MyAttribute attrib) {
    // 床のClosestHit.
    if (checkRecursiveLimit(payload)) {
        return;
    }
    VertexPN vtx = GetHitVertexPN(attrib);
    float3 worldPosition = mul(float4(vtx.Position, 1), ObjectToWorld4x3());
    float3 worldNormal = mul(vtx.Normal, (float3x3)ObjectToWorld4x3());

    payload.color = float3(1, 1, 1);//lerp(reflectColor, color, 0.8);

    // ヒット位置から市松模様を作る.
    float2 v = step(0, sin(worldPosition.xz*1.5)) * 0.5;
    float  v2 = frac(v.x + v.y); // 0 or 0.5
    float3 floorDiffuse = saturate(v2 * 2 + 0.3);

    // ライティング.
    float3 lightDir = -normalize(gSceneParam.lightDirection.xyz);
    float dotNL = saturate(dot(worldNormal, lightDir));

    payload.color.xyz = floorDiffuse * dotNL;

    bool isInShadow = false;

    isInShadow = ShootShadowRay(worldPosition, lightDir);
    if (isInShadow) {
        payload.color.xyz *= 0.5;
    }
}

