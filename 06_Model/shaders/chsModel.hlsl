#include "common.hlsli"

struct VertexPNT {
    float3 Position;
    float3 Normal;
    float2 Texcoord;
};

// Local Root Signature (for HitGroup)
StructuredBuffer<uint>   indexBuffer : register(t0, space1);
StructuredBuffer<float3> vtxPositionBuffer: register(t1, space1);
StructuredBuffer<float3> vtxNormalBuffer : register(t2, space1);
StructuredBuffer<float2> vtxTexcoordBuffer:register(t3, space1);

Texture2D<float4> texDiffuse: register(t0, space2);

struct MeshParameter {
    float4 diffuseColor;
    uint matrixBufferStride;
    uint matrixIndex;
};
ConstantBuffer<MeshParameter> meshParams : register(b0, space2);
// BLAS の行列用データ.
StructuredBuffer<float4> blasMatrixBuffer : register(t4, space1);


float4x4 GetTlasMatrix44() {
    float4x4 mtxTlas;
    float4x3 m = ObjectToWorld4x3();
    mtxTlas[0] = float4(m[0], 0);
    mtxTlas[1] = float4(m[1], 0);
    mtxTlas[2] = float4(m[2], 0);
    mtxTlas[3] = float4(m[3], 1);
    
    return mtxTlas;
}

float4x4 GetBlasMatrix44() {
    int matrixRange = meshParams.matrixBufferStride; // float4換算での個数.
    int frameOffset = matrixRange * gSceneParam.frameIndex;
    // matrixIndexはXMFLOAT3X4単位でのインデックス値.
    int index = meshParams.matrixIndex * 3; 
    index += frameOffset; // 現在フレームでアクセスすべき行列の場所.

    // float4 のデータからシェーダーで使うためのfloat4x4行列を構成する.
    float4 m0 = blasMatrixBuffer[index + 0];
    float4 m1 = blasMatrixBuffer[index + 1];
    float4 m2 = blasMatrixBuffer[index + 2];

    float4x4 mtx;
    mtx[0] = m0;
    mtx[1] = m1;
    mtx[2] = m2;
    mtx[3] = float4(0, 0, 0, 1);
    return transpose(mtx);
}


VertexPNT GetHitVertexPNT(MyAttribute attrib)
{
    VertexPNT v = (VertexPNT)0;
    float3 barycentrics = CalcBarycentrics(attrib.barys);
    uint start = PrimitiveIndex() * 3; // Triangle List のため.

    float3 positions[3], normals[3];
    float2 texcoords[3];
    for (int i = 0; i < 3; ++i) {
        uint index = indexBuffer[start + i];
        positions[i] = vtxPositionBuffer[index];
        normals[i] = vtxNormalBuffer[index];
        texcoords[i] = vtxTexcoordBuffer[index];
    }
    v.Position = CalcHitAttribute3(positions, attrib.barys);
    v.Normal = CalcHitAttribute3(normals, attrib.barys);
    v.Texcoord = CalcHitAttribute2(texcoords, attrib.barys);
    v.Normal = normalize(v.Normal);
    return v;
}

float3 GetDiffuse(float2 uv) {
    float3 diffuse = meshParams.diffuseColor.xyz;
    diffuse *= texDiffuse.SampleLevel(gSampler, uv, 0).xyz;
    return diffuse;
}

float3 doLambert(float3 worldNormal, float3 diffuse) {
    float3 lightDir = GetToLightDirection();
    float dotNL = max(0, dot(worldNormal, lightDir));
    float3 lightColor = gSceneParam.lightColor.xyz;
    float3 color = diffuse * dotNL * lightColor;
    float3 ambientColor = gSceneParam.ambientColor.xyz;

    color += ambientColor * diffuse;
    return color;
}

float CalcSpecular(float3 toEyeDirection, float3 worldNormal) {
    float3 lightDir = GetToLightDirection();
    float3 h = normalize(lightDir + toEyeDirection);
    float spec = saturate(dot(worldNormal, h));
    float e = 100;
    spec = pow(spec, e);// *((e + 1) / (2.0 * 3.141592));
    return spec;
}


[shader("closesthit")]
void mainModelCHS(inout Payload payload, MyAttribute attrib) {
    if (checkRecursiveLimit(payload)) {
        return;
    }
    VertexPNT vtx = GetHitVertexPNT(attrib);
//    float3 v = blasMatrix.blasMatrix._m30_m31_m32;

    float4x4 mtxBlas = GetBlasMatrix44();
    float4x4 mtxTlas = GetTlasMatrix44();
    float4x4 mtx = mul(mtxBlas, mtxTlas);

    float3 worldPosition = mul(float4(vtx.Position, 1), mtx).xyz;
    float3 worldNormal = mul(vtx.Normal, (float3x3)mtx);
    float3 toEyeDirection = gSceneParam.eyePosition.xyz - worldPosition.xyz;

    worldNormal = normalize(worldNormal);
    toEyeDirection = normalize(toEyeDirection);
   
    float3 diffuse = GetDiffuse(vtx.Texcoord);
    float3 color = doLambert(worldNormal, diffuse);
    color += CalcSpecular(toEyeDirection, worldNormal);

    float3 lightDir = GetToLightDirection();
    if (dot(worldNormal, lightDir) > 0) {
        // 光が直接あたる場合、他の射影物のチェック.
        bool isInShadow = ShootShadowRay(worldPosition, lightDir);
        if (isInShadow) {
            // 影の場合アンビエントのみ適用.
            color = diffuse * gSceneParam.ambientColor.xyz;
        }
    }

    payload.color.xyz = color;
}

[shader("closesthit")]
void mainModelCharaCHS(inout Payload payload, MyAttribute attrib) {
    if (checkRecursiveLimit(payload)) {
        return;
    }
    VertexPNT vtx = GetHitVertexPNT(attrib);

    float4x4 mtxBlas = GetBlasMatrix44();
    float4x4 mtxTlas = GetTlasMatrix44();
    float4x4 mtx = mul(mtxBlas, mtxTlas);

    float3 worldPosition = mul(float4(vtx.Position, 1), mtx).xyz;

    float3 worldNormal = mul(vtx.Normal, (float3x3)mtx);
    float3 toEyeDirection = gSceneParam.eyePosition.xyz - worldPosition.xyz;

    worldNormal = normalize(worldNormal);
    toEyeDirection = normalize(toEyeDirection);

    float3 diffuse = GetDiffuse(vtx.Texcoord);
    float3 color = doLambert(worldNormal, diffuse);
    // キャラクタにはスペキュラなしにしておく.
    //color += CalcSpecular(toEyeDirection, worldNormal);

    float3 wpos = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
    float3 lightDir = GetToLightDirection();
    if (dot(worldNormal, lightDir) > 0) {
        // 光が直接あたる場合、他の射影物のチェック.
        bool isInShadow = ShootShadowRay(worldPosition, lightDir);
        if (isInShadow) {
            // 影の場合アンビエントのみ適用.
            color = diffuse * gSceneParam.ambientColor.xyz;
        }
    }
    payload.color.xyz = color;
}
