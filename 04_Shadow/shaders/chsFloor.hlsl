#include "common.hlsli"

struct VertexPN {
    float3 Position;
    float3 Normal;
};

// Local Root Signature (for HitGroup)
StructuredBuffer<uint>   indexBuffer : register(t0, space1);
StructuredBuffer<VertexPN> vertexBuffer: register(t1, space1);


bool ShootShadowRay(float3 origin, float3 direction)
{
    RayDesc rayDesc;
    rayDesc.Origin = origin;
    rayDesc.Direction = direction;
    rayDesc.TMin = 0.1;
    rayDesc.TMax = 100000;

    ShadowPayload payload;
    payload.isHit = true;

    RAY_FLAG flags = RAY_FLAG_NONE;
    //flags |= RAY_FLAG_FORCE_OPAQUE;
    flags |= RAY_FLAG_SKIP_CLOSEST_HIT_SHADER;

    uint rayMask = ~(0x08); // ライトは除外

    TraceRay(
        gRtScene,
        flags,
        rayMask,
        0, // ray index
        1, // MultiplierForGeometryContrib
        1, // miss index
        rayDesc,
        payload);
    return payload.isHit;
}

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

uint randomU(float2 uv)
{
    float r = dot(uv, float2(127.1, 311.7));
    return uint(12345 * frac(sin(r) * 43758.5453123));
}


float nextRand(inout uint s) {
    s = (1664525u * s + 1013904223u);
    return float(s & 0x00FFFFFF) / float(0x01000000);
}

// Rotation with angle (in radians) and axis
float3x3 angleAxis3x3(float angle, float3 axis) {
    float c, s;
    sincos(angle, s, c);

    float t = 1 - c;
    float x = axis.x;
    float y = axis.y;
    float z = axis.z;

    return float3x3(
        t * x * x + c, t * x * y - s * z, t * x * z + s * y,
        t * x * y + s * z, t * y * y + c, t * y * z - s * x,
        t * x * z - s * y, t * y * z + s * x, t * z * z + c
        );
}

// Returns a random direction vector inside a cone
// Angle defined in radians
// Example: direction=(0,1,0) and angle=pi returns ([-1,1],[0,1],[-1,1])
float3 getConeSample(inout uint randSeed, float3 direction, float coneAngle) {
    float cosAngle = cos(coneAngle);
    const float PI = 3.1415926535;

    // Generate points on the spherical cap around the north pole [1].
    // [1] See https://math.stackexchange.com/a/205589/81266
    float z = nextRand(randSeed) * (1.0f - cosAngle) + cosAngle;
    float phi = nextRand(randSeed) * 2.0f * PI;

    float x = sqrt(1.0f - z * z) * cos(phi);
    float y = sqrt(1.0f - z * z) * sin(phi);
    float3 north = float3(0.f, 0.f, 1.f);

    // Find the rotation axis `u` and rotation angle `rot` [1]
    float3 axis = normalize(cross(north, normalize(direction)));
    float angle = acos(dot(normalize(direction), north));

    // Convert rotation axis and angle to 3x3 rotation matrix [2]
    float3x3 R = angleAxis3x3(angle, axis);

    return mul(R, float3(x, y, z));
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

    // ヒット位置から市松模様を作る.
    float2 v = step(0, sin(worldPosition.xz*1.5)) * 0.5;
    float  v2 = frac(v.x + v.y); // 0 or 0.5
    float3 floorDiffuse = saturate(v2 * 2 + 0.3);

    // ライティング.
    float3 lightDir = GetToLightDirection();
    float dotNL = saturate(dot(worldNormal, lightDir));

    payload.color.xyz = floorDiffuse * dotNL;

    float3 shadowRayDir = lightDir;
    bool isInShadow = false;
    if (gSceneParam.flags.x == 0 ) {
        isInShadow = ShootShadowRay(worldPosition, shadowRayDir);
    } else {
        float3 pointLightPosition = gSceneParam.pointLight.xyz;
        lightDir = normalize(pointLightPosition - worldPosition.xyz);

#if 01
        // 光源に向けて散らしたベクトルを生成.
        float3 perpL = cross(lightDir, float3(0, 1, 0));
        if (all(perpL == 0.0)) {
            perpL.x = 1.0;
        }
        float radius = 0.5;
        float3 toLightEdge = normalize((pointLightPosition + perpL * radius) - worldPosition);
        float coneAngle = acos(dot(lightDir, toLightEdge)) * 2.0;
        uint shadowRayCount = gSceneParam.shadowRayCount;
        uint randSeed = randomU(worldPosition.xz * 0.1);
        for (int i = 0; i < shadowRayCount; ++i) {
            shadowRayDir = getConeSample(randSeed, lightDir, coneAngle);
            isInShadow |= ShootShadowRay(worldPosition, shadowRayDir);
        }
#else
        // ハードシャドウにしたい時にはシャドウレイを散らさない.
        isInShadow = ShootShadowRay(worldPosition, lightDir);
#endif
    }

    if (isInShadow) {
        payload.color.xyz *= 0.5;
    }
}

