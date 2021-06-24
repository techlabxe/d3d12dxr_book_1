#pragma once

#include "DxrBookFramework.h"
#include "util/Camera.h"
#include "util/DxrBookUtility.h"
#include <DirectXMath.h>

namespace AppHitGroups {
    static const wchar_t* Object = L"hgObject";
}

class HelloScene : public DxrBookFramework{
public:
    HelloScene(UINT width, UINT height) : DxrBookFramework(width, height, L"3DScene") { }

    void OnInit() override;
    void OnDestroy() override;

    void OnUpdate() override;
    void OnRender() override;

    void OnMouseDown(MouseButton button, int x, int y) override;
    void OnMouseUp(MouseButton button, int x, int y) override;
    void OnMouseMove(int dx, int dy) override;

private:
    // �V�[���ɕK�v�ȃI�u�W�F�N�g�𐶐�����.
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

    // ClosestHit�V�F�[�_�[�p���[�J�����[�g�V�O�l�`���𐶐����܂�.
    void CreateLocalRootSignatureCHS();

    // ���C�g���[�V���O�Ŏg�p���� ShaderTable ���\�z���܂�.
    void CreateShaderTable();

    void UpdateHUD();
    void RenderHUD();

    // �V�[�����ɃI�u�W�F�N�g��z�u����.
    void DeployObjects(std::vector<D3D12_RAYTRACING_INSTANCE_DESC>& instanceDescs);

    
    // �V�F�[�_�[���R�[�h����������(HitGroup).
    uint8_t* WriteShaderRecord(uint8_t* dst, const util::PolygonMesh& mesh, UINT recordSize);

    // �|���S�����b�V��(���pPlane)
    util::PolygonMesh m_meshPlane;
    // �|���S�����b�V��(Cube)
    util::PolygonMesh m_meshCube;

    // TLAS �p�o�b�t�@.
    ComPtr<ID3D12Resource> m_tlas;
    dx12::Descriptor m_tlasDescriptor;

    ComPtr<ID3D12RootSignature> m_rootSignatureGlobal;

    ComPtr<ID3D12RootSignature> m_rsRGS;   // RayGen�V�F�[�_�[�̃��[�J�����[�g�V�O�l�`��.
    ComPtr<ID3D12RootSignature> m_rsModel; // ���E�L���[�u�p�̃��[�J�����[�g�V�O�l�`��.

    // DXR ���ʏ������ݗp�o�b�t�@.
    ComPtr<ID3D12Resource> m_dxrOutput;
    dx12::Descriptor m_outputDescriptor;

    ComPtr<ID3D12StateObject> m_rtState;
    ComPtr<ID3D12GraphicsCommandList4> m_commandList;

    struct SceneParam
    {
        XMMATRIX mtxView;       // �r���[�s��.
        XMMATRIX mtxProj;       // �v���W�F�N�V�����s��.
        XMMATRIX mtxViewInv;    // �r���[�t�s��.
        XMMATRIX mtxProjInv;    // �v���W�F�N�V�����t�s��.
        XMVECTOR lightDirection; // ���s�����̌���.
        XMVECTOR lightColor;    // ���s�����F.
        XMVECTOR ambientColor;  // ����.
    };
    SceneParam m_sceneParam;

    util::DynamicConstantBuffer m_sceneCB;
    util::Camera m_camera;

    // ���I�Ȓ萔�o�b�t�@���g�����߁A�Q�Ƃ���V�F�[�_�[�e�[�u������o�b�t�@���\������.
    static const UINT BackBufferCount = dx12::GraphicsDevice::BackBufferCount;
    ComPtr<ID3D12Resource> m_shaderTable;
    D3D12_DISPATCH_RAYS_DESC m_dispatchRayDesc;
};
