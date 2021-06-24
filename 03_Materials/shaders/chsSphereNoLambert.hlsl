#include "common.hlsli"

struct VertexPN {
    float3 Position;
    float3 Normal;
};
struct MaterialCB {
    float4 diffuseColor;
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
    v.Normal = CalcHitAttribute3(normals, attrib.barys);
    v.Normal = normalize(v.Normal);
    return v;
}

float3 ReflectionCubemapOnly(float3 vertexPosition, float3 vertexNormal)
{
    float3 worldPos = mul(float4(vertexPosition, 1), ObjectToWorld4x3());
    float3 worldNormal = mul(vertexNormal, (float3x3)ObjectToWorld4x3());
    float3 worldRayDir = WorldRayDirection();
    float3 reflectDir = reflect(worldRayDir, worldNormal);

    float3 reflectColor = gBackground.SampleLevel(gSampler, reflectDir, 0).xyz;
    return reflectColor;

    ////// Lambert ライティングを行う.
    //float3 lightDir = -normalize(gSceneParam.lightDirection.xyz);

    //float nl = saturate(dot(vertexNormal, lightDir));

    //float3 lightColor = gSceneParam.lightColor.xyz;
    //float3 ambientColor = gSceneParam.ambientColor.xyz;
    //float3 color = 0;
    ////color += lightColor * vtx.Color.xyz * nl;
    ////color += ambientColor * vtx.Color.xyz;

    //payload.color = color;

    //float3 dir = reflect(-normalize(gSceneParam.eyePosition.xyz - vertexPosition.xyz), vertexNormal);
    //payload.color += 0.2 * gBackground.SampleLevel(gSampler, dir, 0.0).xyz;

    //// 現在のレイと法線で反射ベクトルを求める.
    //float3 worldNormal = mul(vertexNormal, (float3x3)ObjectToWorld4x3());
    //float3 worldRayDir = WorldRayDirection();
    //float3 reflectDir = reflect(worldRayDir, worldNormal);
    //payload.color = gBackground.SampleLevel(gSampler, reflectDir, 0).xyz;
}



[shader("closesthit")]
void chsSphereMaterial(inout Payload payload, MyAttribute attrib) {
    if (checkRecursiveLimit(payload)) {
        return;
    }

    VertexPN vtx = GetHitVertexPN(attrib);

    uint instanceID = InstanceID();
#if 0
    // 現在のRADEONドライバでは正常に動かないケースがあるので回避策.
    // TLAS のインデックスから期待する InstanceID への逆引きとする.
    uint index = InstanceIndex();
    if (1 <= index && index <= 11) {
        instanceID = 0;
    }
    if (11 < index && index <= 21) {
        instanceID = 1;
    }
#endif

    if (instanceID == 0) {
        float3 reflectionColor = Reflection(vtx.Position, vtx.Normal, payload.recursive);
        payload.color = reflectionColor;
    }
    if (instanceID == 1) {
        payload.color = Refraction(vtx.Position, vtx.Normal, payload.recursive);
    }

    if (instanceID == 4) {
        payload.color = ReflectionCubemapOnly(vtx.Position, vtx.Normal);
    }

}
