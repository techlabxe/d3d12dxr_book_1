#include "common.hlsli"


struct MyIntersectAttribute {
    float3 normal;
};

struct AABBGeometryInfo {
    float3 diffuse;
    uint   type;
    float3 center;
    float  radius;
};

// Local Root Signature (for HitGroup)
ConstantBuffer<AABBGeometryInfo> gGeometryInfo : register(b0, space1);

float3 doLambert(float3 worldNormal, float3 diffuse) {
    float3 lightDir = GetToLightDirection();
    float dotNL = max(0, dot(worldNormal, lightDir));
    float3 lightColor = gSceneParam.lightColor.xyz;
    float3 color = diffuse * dotNL * lightColor;
    float3 ambientColor = gSceneParam.ambientColor.xyz;

    color += ambientColor * diffuse;
    return color;
}

bool IntersectToAABBDetail(float3 aabb[2], out float tmin, out float tmax)
{
    float3 origin = ObjectRayOrigin();
    float3 raydir = ObjectRayDirection();
    float3 tmin3, tmax3;
    int3 sign3 = raydir > 0;
    float3 invRay = 1 / raydir;

    tmin3.x = (aabb[1 - sign3.x].x - origin.x);
    tmax3.x = (aabb[sign3.x].x - origin.x);

    tmin3.y = (aabb[1 - sign3.y].y - origin.y);
    tmax3.y = (aabb[sign3.y].y - origin.y);

    tmin3.z = (aabb[1 - sign3.z].z - origin.z);
    tmax3.z = (aabb[sign3.z].z - origin.z);

    tmin3 *= invRay;
    tmax3 *= invRay;

    tmin = max(max(tmin3.x, tmin3.y), tmin3.z);
    tmax = min(min(tmax3.x, tmax3.y), tmax3.z);

    // 交点は現在のレイの範囲内であるか.
    return tmax > tmin && tmax >= RayTMin() && tmin <= RayTCurrent();
}

bool IntersectToAABB(float3 center, float3 aabbMin, float3 aabbMax, out float t, out float3 normal)
{
    float tmin, tmax;
    float3 aabb[2] = { aabbMin + center, aabbMax + center};
    if (IntersectToAABBDetail(aabb, tmin, tmax)) {
        t = tmin >= RayTMin() ? tmin : tmax;

        float3 hitPosition = ObjectRayOrigin() + t * ObjectRayDirection();
        float3 distanceToBounds[2] = {
            abs(aabb[0] - hitPosition),
            abs(aabb[1] - hitPosition)
        };
        const float eps = 0.0001;
        if (distanceToBounds[0].x < eps) normal = float3(-1, 0, 0);
        else if (distanceToBounds[0].y < eps) normal = float3(0, -1, 0);
        else if (distanceToBounds[0].z < eps) normal = float3(0, 0, -1);
        else if (distanceToBounds[1].x < eps) normal = float3(1, 0, 0);
        else if (distanceToBounds[1].y < eps) normal = float3(0, 1, 0);
        else if (distanceToBounds[1].z < eps) normal = float3(0, 0, 1);
        return true;
    }
    return false;
}


bool IntersectToSphere(float3 center, float radius, out float thit, out float3 normal) {
    float t0, t1;
    float3 m = ObjectRayOrigin() - center;
    float3 d = ObjectRayDirection();
    float a = dot(d, d);
    float b = 2.0 * dot(m, d);
    float c = dot(m, m) - radius * radius;
    float discr = b * b - 4 * a * c;

    if (discr < 0) {
        return false;
    }
    if (discr == 0) {
        t0 = -b / (2.0 * a);
        t1 = t0;
    } else {
        float q = 0;
        // 計算誤差が少なくなるように解を選択する.
        if (b > 0) {
            q = -b - sqrt(discr);
        } else {
            q = -b + sqrt(discr);
        }
        t0 = q / (2.0*a);
        t1 = 2.0 * c / q;
    }
    if (t0 > t1) {
        float t = t0;
        t0 = t1;
        t1 = t;
    }


    if (RayTMin() < t0 && t0 < RayTCurrent()) {
        thit = t0;
    } else if (RayTMin() < t1 && t1 < RayTCurrent()) {
        thit = t1;
    } else {
        return false;
    }

    normal = normalize(m + d * thit);
    return true;
}


[shader("intersection")]
void mainIntersectAABB() {
    // BLAS 構築時に想定している AABB は
    //   -0.5～0.5 の範囲での辺の長さ 1.0, 中心点 (0,0,0) を想定.
    //  やや小さい範囲の BOX をここでは判定するものとする.
    const float3 aabbmin = float3(-.35, -.35, -.35);
    const float3 aabbmax = float3(+.35, +.35, +.35);
    // 球体の場合には半径を 0.45 として、判定基準となる AABB よりも小さめに設定.
    const float3 objCenter = gGeometryInfo.center;
    const float  radius = gGeometryInfo.radius;

    float3 localPosition = ObjectRayOrigin() + ObjectRayDirection() * RayTCurrent();
    float len = length(localPosition - objCenter);

    float thit = 0.0;
    float3 normal = 0;

    int shaderType = gGeometryInfo.type;
    // ヒットした場合には法線と位置を通知.
    switch (shaderType) {
    case 0:
        if (IntersectToAABB(objCenter, aabbmin, aabbmax, thit, normal)) {
            MyIntersectAttribute attr;
            attr.normal = normal;
            ReportHit(thit, 0, attr);
        }
        break;
    case 1:
        if (IntersectToSphere(objCenter, radius, thit, normal)) {
            MyIntersectAttribute attr;
            attr.normal = normal;
            ReportHit(thit, 0, attr);
        }
        break;
    }
}

[shader("closesthit")]
void mainClosestHitAABB(inout Payload payload, MyIntersectAttribute attrib) {
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
