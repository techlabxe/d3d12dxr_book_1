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
    uint start = PrimitiveIndex() * 3; // Triangle List のため.

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

   // color += ambientColor * diffuse;
    return color;
}


[shader("closesthit")]
void mainCHS(inout Payload payload, MyAttribute attrib) {
    if (checkRecursiveLimit(payload)) {
        return;
    }
    VertexPNT vtx = GetHitVertexPNT(attrib);

    float4x3 mtx = ObjectToWorld4x3();
    float3 worldPosition = vtx.Position;// mul(float4(vtx.Position, 1), mtx).xyz;
    float3 worldNormal = vtx.Normal;// mul(vtx.Normal, (float3x3)mtx);
    float3 toEyeDirection = gSceneParam.eyePosition.xyz - worldPosition.xyz;

    worldNormal = normalize(worldNormal);
    toEyeDirection = normalize(toEyeDirection);

    float4 diffuse = diffuseTexture.SampleLevel(gSampler, vtx.Texcoord, 0);
    //float3 color = doLambert(worldNormal, diffuse);
    //color += CalcSpecular(toEyeDirection, worldNormal);
    payload.color.xyz = 1;// diffuse.xyz;
    if (diffuse.w < 0.5) {
        payload.color = float3(0, 0.75, 0.75);
    }
    float3 barys = CalcBarycentrics(attrib.barys);
    //payload.color = float3(vtx.Texcoord, 0);
}

[shader("anyhit")]
void mainAHS(inout Payload payload, MyAttribute attrib) {
    VertexPNT vtx = GetHitVertexPNT(attrib);
    float4 diffuse = diffuseTexture.SampleLevel(gSampler, vtx.Texcoord, 0);
    if (diffuse.w < 0.5) {
        IgnoreHit();
    }
}

struct MyIntersectAttribute {
    float3 color;
};

[shader("closesthit")]
void chsIntersectObject(inout Payload payload, MyIntersectAttribute attrib) {
    payload.color = float3(0, 1, 0);

    float3 worldPosition = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    float3 lightDir = GetToLightDirection();
    bool isInShadow = false;
    isInShadow = ShootShadowRay(worldPosition, lightDir);
    if (isInShadow) {
        payload.color.xyz *= 0.5;
    }
    //payload.color = attrib.color;
    //payload.color = length(WorldRayDirection()) - 1;
}

bool IntersectToAABBDetail(float3 aabb[2], out float tmin, out float tmax)
{
    float3 origin = WorldRayOrigin();
    float3 raydir = WorldRayDirection();
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

bool IntersectToAABB(float3 aabbMin, float3 aabbMax, float3 ray_origin, float3 ray_dir, out float t, out float3 normal)
{
    float tmin, tmax;
    float3 aabb[2] = { aabbMin, aabbMax };
    if (IntersectToAABBDetail(aabb, tmin, tmax)) {
        t = tmin >= RayTMin() ? tmin : tmax;

        float3 hitPosition = ray_origin + t * ray_dir;
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

bool IntersectToSphere(float3 center, float radius, float3 rayOrigin, float3 rayDir, out float t, out float3 normal)
{
    float t0, t1;
    float3 L = rayOrigin - center;
    float a = dot(rayDir, rayDir);
    float b = 2 * dot(rayDir, L);
    float c = dot(L, L) - radius * radius;
    float discr = b * b - 4 * a * c;

    float x0 = 0, x1=0;
    if (discr < 0) {
        return false;
    }
    if (discr == 0) {
        x0 = x1 = -b / (2 * a);
    } else {
        float q = (b > 0) ? (-0.5 * (b + sqrt(discr))) : (-0.5 * (b - sqrt(discr)));

        x0 = q / a;
        x1 = c / q;
    }
    if (x0 > x1) {
        float tmp = x0;
        x0 = x1;
        x1 = tmp;
    }
    t0 = x0;
    t1 = x1;

    if (RayTMin() < t0 && t0 < RayTCurrent()) {
        t = t0;
    } else if (RayTMin() < t1 && t1 < RayTCurrent()) {
        t = t1;
    } else {
        return false;
    }
    return true;
}

bool MyIntersectToSphere(float3 center, float radius, out float thit) {
    float t0, t1;
    float3 m = WorldRayOrigin() - center;
    float3 d = WorldRayDirection();
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

    return true;
}


float sdSphere(float3 p, float3 center, float r)
{
    p = p - center;
    return length(p) - r;
}
float sdBox(float3 p, float3 center, float3 extent) {
    p = p - center;
    float3 q = abs(p) - extent;
    return length(max(q, 0.0));
}

float sdTorus(float3 p, float3 center, float radius, float width)
{
    p = p - center;
    float2 q = float2(length(p.xz) - radius, p.y);
    return length(q) - width;
}

bool IsInRange(float value, float min, float max) {
    return (value >= min && value <= max);
}
bool IsCulled(float3 surfaceNormal, float3 raydirection) {
    float dn = dot(surfaceNormal, raydirection);
    bool isCulled =
        ((RayFlags() & RAY_FLAG_CULL_BACK_FACING_TRIANGLES) && dn > 0) ||
        ((RayFlags() & RAY_FLAG_CULL_FRONT_FACING_TRIANGLES) && dn < 0);
    return isCulled;
}
bool IsValidHit(float thit, float3 normal, float3 raydir) {
    return IsInRange(thit, RayTMin(), RayTCurrent()) &&
        !IsCulled(normal, raydir);
}

float distanceFunc(float3 position) {
    float distance = 0;
#if 0
    const float3 center = float3(0, 0, 0);
    float radius = 0.45;
    distance = sdSphere(position, center, radius);
#elif 0
    const float3 extent = float3(0.5, 0.5, 0.5);
    const float3 center = float3(-0.0, 0.0, 0);
    distance = sdBox(position, center, extent);
#else
    const float3 center = float3(0, 0.0, 0);
    float radius = 0.4;
    float width = 0.1;
    distance = sdTorus(position, center, radius, width);
#endif

    return distance;
}

float3 GetNormalByDistanceFunc(float3 position) {
    float val[3] = { 0, 0, 0 };
    const float eps = 0.01f;
    float3 ofs[][2] = {
        { float3(eps, 0, 0), float3(-eps, 0, 0) },
        { float3(0, eps, 0), float3(0, -eps, 0) },
        { float3(0, 0, eps), float3(0, 0, -eps) },
    };
    for (int j = 0; j < 3; ++j) {
        float3 p0 = position + ofs[j][0];
        float3 p1 = position + ofs[j][1];

        val[j] = distanceFunc(p0) - distanceFunc(p1);
    }
    float3 normal = float3(val[0], val[1], val[2]);
    return normalize(normal);
}


[shader("intersection")]
void mainIS() {
    float3 worldPosition = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    float3 objCenter = float3(0, 0, 0);
    float len = length(worldPosition - objCenter);
    //if (len < 1.0) 
    {
        //MyIntersectAttribute attr;
        //attr.color = 0;
        //ReportHit(RayTMin(), 0, attr);
    }

    float3 w_origin = WorldRayOrigin();
    float3 w_dir = WorldRayDirection();
    float3 ray_origin = w_origin + w_dir * RayTMin();
    float3 ray_dir = w_dir;

    float thit = 0.0;
    float3 aabbmin = float3(-.5, -.5, -.5);
    float3 aabbmax = float3(+.5, +.5, +.5);
    float3 normal = 0;
#if 0
    if (IntersectToAABB(aabbmin, aabbmax, ray_origin, ray_dir, thit, normal)) {
        MyIntersectAttribute attr;
        attr.color = 0;
        ReportHit(thit, 0, attr);
    }
#elif 0
    float3 center = float3(0, 0, 0);
    float radius = 0.5;
    //if (IntersectToSphere(center, radius, w_origin, ray_dir, thit, normal)) 
    if(MyIntersectToSphere(center,radius, thit))
    {
        MyIntersectAttribute attr;
        attr.color = 0;
        ReportHit(thit, 0, attr);
    }
#else 
    float t = RayTMin();
    const float threshold = 0.0001;
    const uint MaxSteps = 512;
    uint i = 0;
    const float rayDirLength = length(WorldRayDirection());
    float3 rayDir = ObjectRayDirection();//normalize(WorldRayDirection());
    t /= rayDirLength;

    while (/*i++ < MaxSteps &&*/ t <= RayTCurrent()) {
        float3 position = /*WorldRayOrigin()*/ ObjectRayOrigin() + t * rayDir;
        //float distance = sdSphere(position - float3(0, 0.25, 0), 0.25);
        //float distance = sdBox(position, float3(0.5, 0.5, 0.5));
        float distance = distanceFunc(position);

        //f( abs(position.x) < 0.5 && abs(position.y) < 0.5 && abs(position.z) < 0.1) {
        if (distance <= threshold ) {
#if 0
            const float eps = 0.01f;
            float3 ofs[][2] = {
                { float3(eps, 0, 0), float3(-eps, 0, 0) },
                { float3(0, eps, 0), float3(0, -eps, 0) },
                { float3(0, 0, eps), float3(0, 0, -eps) },
            };

            float3 center = float3(0, .25, 0);
            float radius = 0.25;
            float elem[3] = { 0, 0, 0 };
            for (int j = 0; j < 3; ++j) {
                float3 p0 = position + ofs[j][0];
                float3 p1 = position + ofs[j][1];

                elem[j] = sdSphere(p0 - center, radius) - sdSphere(p1 - center, radius);
            }
            float3 norm = normalize(float3(elem[0], elem[1], elem[2]));
#else
            float3 norm = GetNormalByDistanceFunc(position);
            if (isnan(norm.y)) {
                return;
            }
#endif
            //float3 hitSurfaceNormal = sdCalcNormal(position);
            //if (IsValidHit(t, hitSurfaceNormal, WorldRayDirection())) 
            {
                MyIntersectAttribute attr;
                attr.color = norm * 0.5 + 0.5;// normalize(position) * 0.5 + 0.5;// hitSurfaceNormal * 0.5 + 0.5;
                //attr.color = (length(WorldRayDirection()) - 1.0) * 10.0;
                ReportHit(t/rayDirLength, 0, attr);
                return;
            }
        }
        t += distance;
    }
#endif
}

