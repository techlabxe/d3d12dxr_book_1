struct SceneCB {
    matrix mtxView;       // ビュー行列.
    matrix mtxProj;       // プロジェクション行列.
    matrix mtxViewInv;    // ビュー逆行列.
    matrix mtxProjInv;    // プロジェクション逆行列.
    float4 lightDirection; // 平行光源の向き.
    float4 lightColor;     // 平行光源色.
    float4 ambientColor;   // 環境光.
    float4 eyePosition;    // 視点.
};

struct Payload {
    float3 color;
    int    recursive;
};

struct MyAttribute {
    float2 barys;
};


// Global Root Signature
RaytracingAccelerationStructure gRtScene : register(t0);
ConstantBuffer<SceneCB> gSceneParam : register(b0);
TextureCube<float4> gBackground : register(t1);
SamplerState gSampler: register(s0);

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

float3 Lambert(float3 vertexPosition, float3 vertexNormal)
{
    float3 worldPos = mul(float4(vertexPosition, 1), ObjectToWorld4x3());
    float3 worldNormal = mul(vertexNormal, (float3x3)ObjectToWorld4x3());
    float3 worldRayDir = WorldRayDirection();

    float3 lightDir = -normalize(gSceneParam.lightDirection.xyz);
    float nl = saturate(dot(worldNormal, lightDir));
    const float3 lightColor = gSceneParam.lightColor.xyz;
    const float3 ambientColor = gSceneParam.ambientColor.xyz;
    float3 color = lightColor * nl;
    color += ambientColor;

    return color;
}

float3 Reflection(float3 vertexPosition, float3 vertexNormal, int recursive)
{
    float3 worldPos = mul(float4(vertexPosition, 1), ObjectToWorld4x3());
    float3 worldNormal = mul(vertexNormal, (float3x3)ObjectToWorld4x3());
    float3 worldRayDir = WorldRayDirection();
    float3 reflectDir = reflect(worldRayDir, worldNormal);

    RAY_FLAG flags = RAY_FLAG_NONE;
    uint rayMask = 0xFF;

    RayDesc rayDesc;
    rayDesc.Origin = worldPos;
    rayDesc.Direction = reflectDir;
    rayDesc.TMin = 0.01;
    rayDesc.TMax = 100000;

    Payload reflectPayload;
    reflectPayload.color = float3(0, 0, 0);
    reflectPayload.recursive = recursive;
    TraceRay(
        gRtScene,
        flags,
        rayMask,
        0, // ray index
        1, // MultiplierForGeometryContrib
        0, // miss index
        rayDesc,
        reflectPayload);
    return reflectPayload.color;
}

float3 Refraction(float3 vertexPosition, float3 vertexNormal, int recursive)
{
    float4x3 mtx = ObjectToWorld4x3();
    float3 worldPos = mul(float4(vertexPosition, 1), mtx);
    float3 worldNormal = mul(vertexNormal, (float3x3)mtx);
    float3 worldRayDir = normalize(WorldRayDirection());
    worldNormal = normalize(worldNormal);

    const float refractVal = 1.4;
    float nr = dot(worldNormal, worldRayDir);
    float3 refracted;
    if (nr < 0) {
        // 表面. 空気中 -> 屈折媒質.
        float eta = 1.0 / refractVal;
        refracted = refract(worldRayDir, worldNormal, eta);
    } else {
        // 裏面. 屈折媒質 -> 空気中.
        float eta = refractVal / 1.0;
        refracted = refract(worldRayDir, -worldNormal, eta);
    }

    if (length(refracted) < 0.01) {
        return Reflection(vertexPosition, vertexNormal, recursive);
    }
    else
    {
        RAY_FLAG flags = RAY_FLAG_NONE;
        uint rayMask = 0xFF;

        RayDesc rayDesc;
        rayDesc.Origin = worldPos;
        rayDesc.Direction = refracted;
        rayDesc.TMin = 0.001;
        rayDesc.TMax = 100000;

        Payload refractPayload;
        refractPayload.color = float3(0, 1, 0);
        refractPayload.recursive = recursive;
        TraceRay(
            gRtScene,
            flags,
            rayMask,
            0, // ray index
            1, // MultiplierForGeometryContrib
            0, // miss index
            rayDesc,
            refractPayload);
        return refractPayload.color;
    }
}
