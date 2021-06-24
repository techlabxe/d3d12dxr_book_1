#pragma once

#include "DxrBookFramework.h"
#include "util/Camera.h"
#include "util/DxrBookUtility.h"

#include <DirectXMath.h>

namespace AppHitGroups {
    static const wchar_t* Floor = L"hgFloor";
    static const wchar_t* Light = L"hgLight";
    static const wchar_t* Sphere = L"hgSphere";
}

class ShadowScene : public DxrBookFramework {
public:
    ShadowScene(UINT width, UINT height) : DxrBookFramework(width, height, L"ShadowScene") { }

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
    void CreateLocalRootSignatureRayGen();

    // �X�t�B�A�p�̃��[�J�����[�g�V�O�l�`���𐶐����܂�.
    void CreateSphereLocalRootSignature();

    // ���p�̃��[�J�����[�g�V�O�l�`���𐶐����܂�.
    void CreateFloorLocalRootSignature();

    // ���C�g���[�V���O�Ŏg�p���� ShaderTable ���\�z���܂�.
    void CreateShaderTable();

    // �V�[�����ɃI�u�W�F�N�g��z�u����.
    void DeployObjects(std::vector<D3D12_RAYTRACING_INSTANCE_DESC>& instanceDescs);

    // TLAS ���č\�z���܂�.
    void UpdateSceneTLAS(UINT frameIndex);


    struct PolygonMesh {
        ComPtr<ID3D12Resource> vertexBuffer;
        ComPtr<ID3D12Resource> indexBuffer;

        // �W�I���g�������Q�Ƃ���f�B�X�N���v�^.
        dx12::Descriptor descriptorVB;
        dx12::Descriptor descriptorIB;

        UINT vertexCount = 0;
        UINT indexCount = 0;
        UINT vertexStride = 0;

        // BLAS �p�o�b�t�@.
        ComPtr<ID3D12Resource> blas;

        std::wstring shaderName;
    };
    PolygonMesh m_meshPlane;
    PolygonMesh m_meshSphere;
    PolygonMesh m_meshLightSphere;

    // PolygonMesh �����ɁA�l�Z�b�g�ς݂� D3D12_RAYTRACING_GEOMETRY_DESC ���擾.
    D3D12_RAYTRACING_GEOMETRY_DESC GetGeometryDescFromPolygonMesh(const PolygonMesh& mesh);
    
    // �`��̃V�F�[�_�[�G���g������������.
    uint8_t* WriteHitgroupShaderEntry(uint8_t* dst, const PolygonMesh& mesh, UINT hgEntrySize);

    // TLAS �p�o�b�t�@.
    util::DynamicBuffer m_instanceDescsBuffer;
    ComPtr<ID3D12Resource> m_tlas;
    ComPtr<ID3D12Resource> m_tlasUpdate;

    dx12::Descriptor m_tlasDescriptor;

    ComPtr<ID3D12RootSignature> m_rootSignatureGlobal;

    ComPtr<ID3D12RootSignature> m_rsRGS;   // RayGen�V�F�[�_�[�̃��[�J�����[�g�V�O�l�`��.
    ComPtr<ID3D12RootSignature> m_rsFloor; // ���̃��[�J�����[�g�V�O�l�`��.
    ComPtr<ID3D12RootSignature> m_rsSphere; // �X�t�B�A�̃��[�J�����[�g�V�O�l�`��.

    // DXR ���ʏ������ݗp�o�b�t�@.
    ComPtr<ID3D12Resource> m_dxrOutput;
    dx12::Descriptor m_outputDescriptor;

    ComPtr<ID3D12StateObject> m_rtState;
    ComPtr<ID3D12Resource> m_shaderTable;
    ComPtr<ID3D12GraphicsCommandList4> m_commandList;

    D3D12_DISPATCH_RAYS_DESC m_dispatchRayDesc;

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

        XMFLOAT3 pointLightPosition; // �|�C���g���C�g�ʒu.
        UINT     shadowRayCount; // �V���h�E���C��.
        XMUINT4  flags;
    };
    SceneParam m_sceneParam;
    XMFLOAT3 m_lightPos;
    float    m_lightDistanceFactor = 1.0;

    // �X�t�B�A���ƂɎ���������.
    struct SphereInstance {
        XMMATRIX mtxWorld = DirectX::XMMatrixIdentity();
    };
    std::array<SphereInstance, 10> m_spheres;
    SphereInstance m_pointLight;

    util::Camera m_camera;
    util::DynamicConstantBuffer m_sceneCB;

};