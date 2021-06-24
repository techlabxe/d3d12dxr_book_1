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
void mainSphereCHS(inout Payload payload, MyAttribute attrib) {
    if (checkRecursiveLimit(payload)) {
        return;
    }
    VertexPN vtx = GetHitVertexPN(attrib);

    const float3 colorTable[] = {
        float3(1.0f, 0.4f, 0.1f),
        float3(0.5f, 0.8f, 0.4f),
        float3(0.7f, 0.6f, 0.2f),
        float3(0.2f, 0.3f, 0.6f),
        float3(0.1f, 0.8f, 0.9f),
    };
    float3 sphereDiffuse = colorTable[ InstanceID() % 5];
    float3 worldPosition = mul(float4(vtx.Position, 1), ObjectToWorld4x3());
    float3 worldNormal = mul(vtx.Normal, (float3x3)ObjectToWorld4x3());

    if (gSceneParam.flags.x == 0) {
        // 平行光源でライティング.
        float3 lightDir = -normalize(gSceneParam.lightDirection.xyz);
        float dotNL = saturate(dot(worldNormal, lightDir));
        payload.color = dotNL * sphereDiffuse;
    } else {
        // ポイントライトでライティング.
        float3 pointLightPosition = gSceneParam.pointLight.xyz;
        float3 lightDir = normalize(pointLightPosition - worldPosition.xyz);
        float dotNL = saturate(dot(worldNormal, lightDir));
        payload.color = dotNL * sphereDiffuse;
    }

    payload.color += gSceneParam.ambientColor.xyz * sphereDiffuse;
}

