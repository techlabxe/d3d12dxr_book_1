#include "common.hlsli"

struct VertexPN {
    float3 Position;
    float3 Normal;
};
struct MaterialCB {
    float4 albedo;
    float4 specular; // xyz: 色, w: 強度.
};
// Local Root Signature (for HitGroup)
ConstantBuffer<MaterialCB> constantBuffer: register(b0, space1);
StructuredBuffer<uint>   indexBuffer : register(t0,space1);
StructuredBuffer<VertexPN>  vertexBuffer : register(t1,space1);


VertexPN GetHitVertexPN(MyAttribute attrib)
{
    VertexPN v = (VertexPN)0;
    uint start = PrimitiveIndex() * 3; // Triangle List のため.

    float3 positions[3], normals[3];
    for (int i = 0; i < 3; ++i) {
        uint index = indexBuffer[start + i];
        positions[i] = vertexBuffer[index].Position;
        normals[i] = vertexBuffer[index].Normal;
    }

    v.Position = CalcHitAttribute3(positions, attrib.barys);
    v.Normal = normalize(CalcHitAttribute3(normals, attrib.barys));
    return v;
}


float CalculateDiffuseCoefficient(float3 worldPosition, float3 incidentLightRay, float3 normal) {
    float dotNL = saturate(dot(-incidentLightRay, normal));
    return dotNL;
}
float CalculateSpecularCoefficient(float3 worldPosition, float3 incidentLightRay, float3 normal, float specularPower) {
    float3 reflectedLightRay = normalize(reflect(incidentLightRay, normal));
    return pow(saturate(dot(reflectedLightRay, normalize(-WorldRayDirection()))), specularPower);
}

float3 CalculatePhongLighting(float3 worldPosition, float3 albedo, float3 normal)
{
    const float diffuseCoef = 1.0;
    const float specularCoef = 1.0;

    float3 incidentLightRay = normalize(gSceneParam.lightDirection.xyz);
    float Kd = CalculateDiffuseCoefficient(worldPosition, incidentLightRay, normal);
    float3 diffuseColor = diffuseCoef * Kd * gSceneParam.lightColor.xyz * albedo;

    float3 specularColor = 0;
    float3 Ks = CalculateSpecularCoefficient(worldPosition, incidentLightRay, normal, constantBuffer.specular.w);
    specularColor = 0.7 * Ks * constantBuffer.specular.xyz;

    float3 ambientColor = gSceneParam.ambientColor.xyz;
    ambientColor *= albedo;

    return diffuseColor + specularColor + ambientColor;
}

[shader("closesthit")]
void chsSphereMaterialPhong(inout Payload payload, MyAttribute attrib) {
    if (checkRecursiveLimit(payload)) {
        return;
    }

    VertexPN vtx = GetHitVertexPN(attrib);

    uint instanceID = InstanceID();
    float3 albedo = constantBuffer.albedo.xyz;
    float power = constantBuffer.specular.w;

    if (instanceID == 2) {
        payload.color = CalculatePhongLighting(vtx.Position.xyz, albedo, vtx.Normal);
    } else {
        payload.color = float3(0,0,0); // 処理対象ではないので黒で返す.
    }
}
