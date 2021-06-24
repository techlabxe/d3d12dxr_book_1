#include "common.hlsli"

struct VertexPNT {
    float3 Position;
    float3 Normal;
    float2 Texcoord;
};

// Local Root Signature (for HitGroup)
StructuredBuffer<uint>   indexBuffer : register(t0, space1);
StructuredBuffer<VertexPNT> vertexBuffer: register(t1, space1);
Texture2D<float4> diffuseTexture : register(t2, space1);

VertexPNT GetHitVertexPNT(MyAttribute attrib)
{
    VertexPNT v = (VertexPNT)0;
    float3 barycentrics = CalcBarycentrics(attrib.barys);
    uint start = PrimitiveIndex() * 3; // Triangle List ‚Ì‚½‚ß.

    float3 positions[3], normals[3];
    float2 texcoords[3];
    for (int i = 0; i < 3; ++i) {
        uint index = indexBuffer[start + i];
        VertexPNT v = vertexBuffer[index];
        positions[i] = v.Position;
        normals[i] = v.Normal;
        texcoords[i] = v.Texcoord;
    }
    v.Position = CalcHitAttribute3(positions, attrib.barys);
    v.Normal = CalcHitAttribute3(normals, attrib.barys);
    v.Texcoord = CalcHitAttribute2(texcoords, attrib.barys);
    v.Normal = normalize(v.Normal);
    return v;
}

float3 doLambert(float3 worldNormal, float3 diffuse) {
    float3 lightDir = GetToLightDirection();
    float dotNL = max(0, dot(worldNormal, lightDir));
    float3 lightColor = gSceneParam.lightColor.xyz;
    float3 color = diffuse * dotNL * lightColor;
    float3 ambientColor = gSceneParam.ambientColor.xyz;
    return color;
}

[shader("anyhit")]
void mainAnyHit(inout Payload payload, MyAttribute attrib) {
    VertexPNT vtx = GetHitVertexPNT(attrib);
    float4 diffuse = diffuseTexture.SampleLevel(gSampler, vtx.Texcoord, 0);
    if (diffuse.w < 0.5) {
        IgnoreHit();
    }
}

[shader("closesthit")]
void mainAnyHitModel(inout Payload payload, MyAttribute attrib) {
    if (checkRecursiveLimit(payload)) {
        return;
    }

    VertexPNT vtx = GetHitVertexPNT(attrib);
    payload.color = float3(0.8, 0.8, 0.8);
}
