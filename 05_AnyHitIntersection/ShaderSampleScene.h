#pragma once

#include "DxrBookFramework.h"
#include "util/Camera.h"
#include "util/DxrModel.h"
#include <DirectXMath.h>

#include "util/DxrBookUtility.h"

namespace AppHitGroups {
    static const wchar_t* IntersectAABB = L"hgIntersectAABB";
    static const wchar_t* IntersectSDF = L"hgIntersectSDF";
    static const wchar_t* Floor = L"hgFloor";
    static const wchar_t* AnyHitModel = L"hgAnyHitModel";
}

class ShadersSampleScene : public DxrBookFramework {
public:
    ShadersSampleScene(UINT width, UINT height);

    void OnInit() override;
    void OnDestroy() override;

    void OnUpdate() override;
    void OnRender() override;

    void OnMouseDown(MouseButton button, int x, int y) override;
    void OnMouseUp(MouseButton button, int x, int y) override;
    void OnMouseMove(int dx, int dy) override;

private:
    void CreateSceneObjects();

    void CreateSceneBLAS();

    void CreateSceneTLAS();

    // ���C�g���[�V���O�p�� StateObject ���\�z���܂�.
    void CreateStateObject();

    // ���C�g���[�V���O���ʏ������ݗp�o�b�t�@�𐶐����܂�.
    void CreateResultBuffer();

    // ���[�g�V�O�l�`��(Global) �𐶐����܂�.
    void CreateRootSignatureGlobal();

    // RayGen�V�F�[�_�[�p���[�J�����[�g�V�O�l�`���𐶐����܂�.
    void CreateRayGenLocalRootSignature();

    // �X�t�B�A�p�̃��[�J�����[�g�V�O�l�`���𐶐����܂�.
    void CreateSphereLocalRootSignature();

    // ���p�̃��[�J�����[�g�V�O�l�`���𐶐����܂�.
    void CreateFloorLocalRootSignature();

    // ��͓I�ɏ�������{�b�N�X�E���Ɍ����Ẵ��[�J�����[�g�V�O�l�`���𐶐����܂�.
    void CreateAnalyticPrimsLocalRootSignature();

    // �����֐��ŏ���������̂Ɍ����Ẵ��[�J�����[�g�V�O�l�`���𐶐����܂�.
    void CreateSignedDistanceFieldLocalRootSignature();

    // ���C�g���[�V���O�Ŏg�p���� ShaderTable ���\�z���܂�.
    void CreateShaderTable();

    void UpdateSceneTLAS(UINT frameIndex);

    // �V�[�����ɃI�u�W�F�N�g��z�u����.
    void DeployObjects(std::vector<D3D12_RAYTRACING_INSTANCE_DESC>& instanceDescs);

    util::PolygonMesh m_meshPlane;
    util::PolygonMesh m_meshFence;

    util::ProcedualMesh m_meshAABB;
    util::ProcedualMesh m_meshSDF;

    // �`��̃V�F�[�_�[���R�[�h����������.
    uint8_t* WriteHitgroupShaderRecord(uint8_t* dst, const util::PolygonMesh& mesh, dx12::Descriptor texDescriptor, UINT hgEntrySize);
    uint8_t* WriteHitgroupShaderRecord(uint8_t* dst, const util::ProcedualMesh& mesh, dx12::Descriptor texDescriptor, ComPtr<ID3D12Resource> cb, UINT hgEntrySize);


    // TLAS 
    std::array<ComPtr<ID3D12Resource>, dx12::GraphicsDevice::BackBufferCount> m_instanceDescsBuffers;
    ComPtr<ID3D12Resource> m_tlas;
    ComPtr<ID3D12Resource> m_tlasScratch;
    dx12::Descriptor m_tlasDescriptor;

    UINT m_writeBufferIndex = 0;

    ComPtr<ID3D12RootSignature> m_rootSignatureGlobal;

    ComPtr<ID3D12RootSignature> m_rsRGS;   // RayGen�V�F�[�_�[�̃��[�J�����[�g�V�O�l�`��.
    ComPtr<ID3D12RootSignature> m_rsFloor; // ���̃��[�J�����[�g�V�O�l�`��.
    ComPtr<ID3D12RootSignature> m_rsModel; // �X�t�B�A�̃��[�J�����[�g�V�O�l�`��.
    ComPtr<ID3D12RootSignature> m_rsAnalyticPrims;  // �{�b�N�X/���Ŏg�p���郍�[�J�����[�g�V�O�l�`��.
    ComPtr<ID3D12RootSignature> m_rsSdfPrims; // DistanceField �̃I�u�W�F�N�g�Ŏg�p���郍�[�J�����[�g�V�O�l�`��.

    // DXR ���ʏ������ݗp�o�b�t�@.
    ComPtr<ID3D12Resource> m_dxrOutput;
    dx12::Descriptor m_outputDescriptor;

    ComPtr<ID3D12StateObject> m_rtState;
    util::DynamicBuffer m_shaderTable;
    ComPtr<ID3D12GraphicsCommandList4> m_commandList;

    std::array<D3D12_DISPATCH_RAYS_DESC, dx12::GraphicsDevice::BackBufferCount> m_dispatchRayDescs;

    struct SceneParam
    {
        XMMATRIX mtxView;       // �r���[�s��.
        XMMATRIX mtxProj;       // �v���W�F�N�V�����s��.
        XMMATRIX mtxViewInv;    // �r���[�t�s��.
        XMMATRIX mtxProjInv;    // �v���W�F�N�V�����t�s��.
        XMVECTOR lightDirection; // ���s�����̌���.
        XMVECTOR lightColor;    // ���s�����F.
        XMVECTOR ambientColor;  // ����.
        XMVECTOR eyePosition;   // ���_.
    };
    SceneParam m_sceneParam;

    util::Camera m_camera;
    util::DynamicConstantBuffer m_sceneCB;
    util::TextureResource m_texture;
    util::TextureResource m_whiteTex;

    struct AnalyticGeometryParam
    {
        XMFLOAT3 diffuse = XMFLOAT3(0.2f, 0.7f, 0.3f);
        UINT     type = 0; // 0: BOX, 1: Sphere �̎��
        XMFLOAT3 center = XMFLOAT3(0.0f, 0.0f, 0.0f);   // AABB ���̂ǂ��𒆐S�Ƃ��邩.
        float    radius = 0.45f;
    } m_analyticGeomParam;
    util::DynamicConstantBuffer m_analyticCB;

    struct SDFGeometryParam {
        XMFLOAT3 diffuse = XMFLOAT3(0.2f, 0.4f, 1.0f);
        UINT type = 0; // 0: Box, 1: Sphere, 2: Torus �̎��
        XMFLOAT3 extent = XMFLOAT3(0.2f, 0.3f, 0.45f); // BOX�̑傫��.
        float    radius = 0.35f; // Sphere/torus���a.
    } m_sdfGeomParam;
    util::DynamicConstantBuffer m_sdfParamCB;
};