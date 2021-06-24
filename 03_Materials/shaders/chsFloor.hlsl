#include "common.hlsli"

struct VertexPNT {
    float3 Position;
    float3 Normal;
    float2 UV;
};

// Local Root Signature (for HitGroup)
StructuredBuffer<uint>   indexBuffer : register(t0, space1);
StructuredBuffer<VertexPNT> vertexBuffer: register(t1, space1);
Texture2D<float4> diffuse : register(t2, space1);


VertexPNT GetHitVertexPNT(MyAttribute attrib)
{
    VertexPNT v = (VertexPNT)0;
    float3 barycentrics = CalcBarycentrics(attrib.barys);
    uint start = PrimitiveIndex() * 3; // Triangle List ‚Ì‚½‚ß.

    float3 positions[3], normals[3];
    float2 texcoords[3];
    for (int i = 0; i < 3; ++i) {
        uint index = indexBuffer[start + i];
        positions[i] = vertexBuffer[index].Position;
        normals[i] = vertexBuffer[index].Normal;
        texcoords[i] = vertexBuffer[index].UV;
    }
    v.Position = CalcHitAttribute3(positions, attrib.barys);
    v.Normal = CalcHitAttribute3(normals, attrib.barys);
    v.UV = CalcHitAttribute2(texcoords, attrib.barys);

    v.Normal = normalize(v.Normal);
    return v;
}


[shader("closesthit")]
void mainFloorCHS(inout Payload payload, MyAttribute attrib) {
    // °‚ÌClosestHit.
    if (checkRecursiveLimit(payload)) {
        return;
    }
    VertexPNT vtx = GetHitVertexPNT(attrib);
    float4 diffuseColor = diffuse.SampleLevel(gSampler, vtx.UV, 0.0);

    float3 lambert = Lambert(vtx.Position, vtx.Normal);
    float3 color = lambert * diffuseColor.xyz;
    float3 reflectColor = Reflection(vtx.Position, vtx.Normal, payload.recursive);
    // ”½ŽË‚ÌF‚ð2Š„’ö“x‘«‚µ‚Ä‚¨‚­.
    payload.color = lerp(reflectColor, color, 0.8);

}

