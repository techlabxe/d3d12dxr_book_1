#include "common.hlsli"


struct MyIntersectAttribute {
    float3 normal;
};

struct SDFGeometryInfo {
    float3 diffuse;
    uint   type;
    float3 extent;
    float  radius;
};

// Local Root Signature (for HitGroup)
ConstantBuffer<SDFGeometryInfo> gGeometryInfo : register(b0, space1);

float3 doLambert(float3 worldNormal, float3 diffuse) {
    float3 lightDir = GetToLightDirection();
    float dotNL = max(0, dot(worldNormal, lightDir));
    float3 lightColor = gSceneParam.lightColor.xyz;
    float3 color = diffuse * dotNL * lightColor;
    float3 ambientColor = gSceneParam.ambientColor.xyz;

    color += ambientColor * diffuse;
    return color;
}

float sdBox(float3 p, float3 center, float3 extent) {
    p = p - center;
    float3 q = abs(p) - extent;
    return length(max(q, 0.0));
}
float sdSphere(float3 p, float3 center, float r)
{
    p = p - center;
    return length(p) - r;
}
float sdTorus(float3 p, float3 center, float radius, float width)
{
    p = p - center;
    float2 q = float2(length(p.xz) - radius, p.y);
    return length(q) - width;
}

float CheckDistance(float3 position) {
    float distance = 0;
    uint type = gGeometryInfo.type;
    float3 extent = gGeometryInfo.extent;
    float radius = gGeometryInfo.radius;
    float3 objCenter = float3(0, 0, 0);
    switch (type) {
    case 0: // Box
        distance = sdBox(position, objCenter, extent);
        break;
    case 1: // Sphere
        distance = sdSphere(position, objCenter, radius);
        break;
    case 2: // Torus
        distance = sdTorus(position, objCenter, radius, 0.1);
        break;
    }
    return distance;
}

float3 GetNormalByDistanceFunc(float3 position) {
    float val[3] = { 0, 0, 0 };
    const float eps = 0.001f;
    float3 ofs[][2] = {
        { float3(eps, 0, 0), float3(-eps, 0, 0) },
        { float3(0, eps, 0), float3(0, -eps, 0) },
        { float3(0, 0, eps), float3(0, 0, -eps) },
    };
    for (int i = 0; i < 3; ++i) {
        float3 p0 = position + ofs[i][0];
        float3 p1 = position + ofs[i][1];
        val[i] = CheckDistance(p0) - CheckDistance(p1);
    }
    return normalize(float3(val[0], val[1], val[2]));
}


[shader("intersection")]
void mainIntersectSDF() {
    float t = RayTMin();
    const float threshold = 0.0001;
    const uint MaxSteps = 512;
    uint i = 0;

    // スフィアトレーシングによってカレント位置を進めつつ調査.
    while (i++ < MaxSteps && t <= RayTCurrent()) 
    {
        float3 position = ObjectRayOrigin() + t * ObjectRayDirection();
        float distance = CheckDistance(position);
        if (distance <= threshold ) {
            // 法線を求める.
            MyIntersectAttribute attr;
            attr.normal = GetNormalByDistanceFunc(position);
            uint kind = 0; // 種別は今は使わないのでゼロ.
            ReportHit(t, kind, attr);
            return;
        }
        t += distance;
    }
}

[shader("closesthit")]
void mainClosestHitSDF(inout Payload payload, MyIntersectAttribute attrib) {
    if (checkRecursiveLimit(payload)) {
        return;
    }

    float3 lightDir = GetToLightDirection();
    float4x3 mtx = ObjectToWorld4x3();

    float3 worldPosition = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    float3 worldNormal = mul(attrib.normal, (float3x3)mtx);
    float3 toEyeDirection = gSceneParam.eyePosition.xyz - worldPosition.xyz;

    worldNormal = normalize(worldNormal);
    toEyeDirection = normalize(toEyeDirection);

    float3 diffuse = gGeometryInfo.diffuse.xyz;
    payload.color = doLambert(worldNormal, diffuse);

    bool isInShadow = false;
    isInShadow = ShootShadowRay(worldPosition, lightDir);
    if (isInShadow) {
        payload.color.xyz *= 0.5;
    }
}
