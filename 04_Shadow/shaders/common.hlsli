struct SceneCB {
    matrix mtxView;       // �r���[�s��.
    matrix mtxProj;       // �v���W�F�N�V�����s��.
    matrix mtxViewInv;    // �r���[�t�s��.
    matrix mtxProjInv;    // �v���W�F�N�V�����t�s��.
    float4 lightDirection; // ���s�����̌���.
    float4 lightColor;     // ���s�����F.
    float4 ambientColor;   // ����.
    float4 eyePosition;    // ���_.

    float3 pointLight;      // �|�C���g���C�g.
    uint   shadowRayCount;  // �V���h�E���C��.
    uint4  flags;   // x: ���s�����V���h�EON/OFF, y: �|�C���g���C�g�ʒu�`��
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

// ���[���h��Ԃł̃��C�g�֌������������擾����.
float3 GetToLightDirection() {
    return -normalize(gSceneParam.lightDirection.xyz);
}
