#pragma once

#include "DxrBookFramework.h"
#include "util/Camera.h"
#include "util/DxrBookUtility.h"
#include <DirectXMath.h>

namespace AppHitGroups {
    static const wchar_t* NoPhongMaterial = L"hgNoPhongSpheres";
    static const wchar_t* PhongMaterial = L"hgPhongSpheres";
    static const wchar_t* Floor = L"hgFloor";
}

class MaterialScene : public DxrBookFramework{
public:
    MaterialScene(UINT width, UINT height) : DxrBookFramework(width, height, L"MaterialScene") { }

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

    void UpdateHUD();
    void RenderHUD();

    // �V�[�����ɃI�u�W�F�N�g��z�u����.
    void DeployObjects(std::vector<D3D12_RAYTRACING_INSTANCE_DESC>& instanceDescs);

    struct TextureResource
    {
        ComPtr<ID3D12Resource> res;
        dx12::Descriptor srv;
    };
    TextureResource LoadTextureFromFile(const std::wstring& fileName);

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
        XMMATRIX mtxWorld = DirectX::XMMatrixIdentity();
    };
    PolygonMesh m_meshPlane;
    PolygonMesh m_meshSphere;

    uint8_t* WriteHitgroupFloor(uint8_t* dst, const PolygonMesh& mesh, UINT hitgroupRecordSize);
    uint8_t* WriteHitgroupMaterial(uint8_t* dst, const PolygonMesh& mesh, UINT hitgroupRecordSize);
    uint8_t* WriteHitgroupPhong(uint8_t* dst, const PolygonMesh& mesh, D3D12_GPU_VIRTUAL_ADDRESS address, UINT hitgroupRecordSize);


    enum SphereTypeCount {
        NormalSpheres = 10,
        ReflectSpheres = 10,
        RefractSpheres = 10,
        SpheresAll = (NormalSpheres+ ReflectSpheres+ RefractSpheres),
    };


    // TLAS �p�o�b�t�@.
    ComPtr<ID3D12Resource> m_tlas;
    dx12::Descriptor m_tlasDescriptor;

    ComPtr<ID3D12RootSignature> m_rootSignatureGlobal;

    ComPtr<ID3D12RootSignature> m_rsRGS;   // RayGen�V�F�[�_�[�̃��[�J�����[�g�V�O�l�`��.
    ComPtr<ID3D12RootSignature> m_rsFloor; // ���̃��[�J�����[�g�V�O�l�`��.
    ComPtr<ID3D12RootSignature> m_rsSphere1; // �X�t�B�A�̃��[�J�����[�g�V�O�l�`��(���ˁE���ܗp).
    ComPtr<ID3D12RootSignature> m_rsSphere2; // �X�t�B�A�̃��[�J�����[�g�V�O�l�`��(Phong�p).

    // DXR ���ʏ������ݗp�o�b�t�@.
    ComPtr<ID3D12Resource> m_dxrOutput;
    dx12::Descriptor m_outputDescriptor;

    ComPtr<ID3D12StateObject> m_rtState;
    ComPtr<ID3D12Resource> m_shaderTable;
    ComPtr<ID3D12GraphicsCommandList4> m_commandList;

    D3D12_DISPATCH_RAYS_DESC m_dispatchRayDesc;

    struct MaterialParam
    {
        XMVECTOR albedo;
        XMVECTOR specular; // �X�y�L�����[�F & w�v�f��Power
    };


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

    std::array<XMMATRIX, ReflectSpheres> m_spheresReflect;
    std::array<XMMATRIX, RefractSpheres> m_spheresRefract;
    std::array<XMMATRIX, NormalSpheres> m_spheresNormal;

    // Phong�`�悷�邽�߂̒ʏ�X�t�B�A�̃}�e���A�����.
    std::array<MaterialParam, NormalSpheres> m_normalSphereMaterials;
    ComPtr<ID3D12Resource> m_normalSphereMaterialCB;


    TextureResource m_background;
    TextureResource m_groundTex;

    static const UINT BackBufferCount = dx12::GraphicsDevice::BackBufferCount;
    util::DynamicConstantBuffer m_sceneCB;

    util::Camera m_camera;


};
