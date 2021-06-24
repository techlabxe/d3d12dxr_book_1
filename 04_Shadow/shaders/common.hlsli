struct SceneCB {
    matrix mtxView;       // ビュー行列.
    matrix mtxProj;       // プロジェクション行列.
    matrix mtxViewInv;    // ビュー逆行列.
    matrix mtxProjInv;    // プロジェクション逆行列.
    float4 lightDirection; // 平行光源の向き.
    float4 lightColor;     // 平行光源色.
    float4 ambientColor;   // 環境光.
    float4 eyePosition;    // 視点.

    float3 pointLight;      // ポイントライト.
    uint   shadowRayCount;  // シャドウレイ数.
    uint4  flags;   // x: 平行光源シャドウON/OFF, y: ポイントライト位置描画
};

struct Payload {
    float3 color;
    int    recursive;
};
struct ShadowPayload {
    bool isHit;
};

struct MyAttribute {
    float2 barys;
};


// Common Function
inline float3 CalcBarycentrics(float2 barys)
{
    return float3(
        1.0 - barys.x - barys.y,
        barys.x,
        barys.y);
}

inline float2 CalcHitAttribute2(float2 vertexAttribute[3], float2 barycentrics)
{
    float2 ret;
    ret = vertexAttribute[0];
    ret += barycentrics.x * (vertexAttribute[1] - vertexAttribute[0]);
    ret += barycentrics.y * (vertexAttribute[2] - vertexAttribute[0]);
    return ret;
}

float3 CalcHitAttribute3(float3 vertexAttribute[3], float2 barycentrics)
{
    float3 ret;
    ret = vertexAttribute[0];
    ret += barycentrics.x * (vertexAttribute[1] - vertexAttribute[0]);
    ret += barycentrics.y * (vertexAttribute[2] - vertexAttribute[0]);
    return ret;
}

inline bool checkRecursiveLimit(inout Payload payload) {
    payload.recursive++;
    if (payload.recursive >= 15) {
        payload.color = float3(0, 0, 0);
        return true;
    }
    return false;
}

// Global Root Signature
RaytracingAccelerationStructure gRtScene : register(t0);
ConstantBuffer<SceneCB> gSceneParam : register(b0);

// ワールド空間でのライトへ向かう向きを取得する.
float3 GetToLightDirection() {
    return -normalize(gSceneParam.lightDirection.xyz);
}
