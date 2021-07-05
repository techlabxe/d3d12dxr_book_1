#pragma once

#include "DxrBookFramework.h"
#include "util/Camera.h"
#include <DirectXMath.h>
#include "util/DxrBookUtility.h"
#include "util/DxrModel.h"

namespace AppHitGroups {
    static const wchar_t* Floor = L"hgFloor";
    static const wchar_t* StaticModel = L"hgModel";
    static const wchar_t* CharaModel = L"hgCharaModel";
}

class ModelScene : public DxrBookFramework {
public:
    ModelScene(UINT width, UINT height);

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

    void CreateSkinningPipeline();

    // ���C�g���[�V���O�p�� StateObject ���\�z���܂�.
    void CreateStateObject();

    // ���C�g���[�V���O���ʏ������ݗp�o�b�t�@�𐶐����܂�.
    void CreateResultBuffer();

    // ���[�g�V�O�l�`��(Global) �𐶐����܂�.
    void CreateRootSignatureGlobal();

    // RayGen�V�F�[�_�[�p���[�J�����[�g�V�O�l�`���𐶐����܂�.
    void CreateRayGenLocalRootSignature();

    // �X�t�B�A�p�̃��[�J�����[�g�V�O�l�`���𐶐����܂�.
    void CreateModelLocalRootSignature();

    // ���p�̃��[�J�����[�g�V�O�l�`���𐶐����܂�.
    void CreateFloorLocalRootSignature();

    // ���C�g���[�V���O�Ŏg�p���� ShaderTable ���\�z���܂�.
    void CreateShaderTable();

    void RenderHUD();

    void UpdateSceneTLAS(UINT frameIndex);

    // ���f���f�[�^�̏���.
    void PrepareModels();

    // �V�[�����ɃI�u�W�F�N�g��z�u����.
    void DeployObjects(std::vector<D3D12_RAYTRACING_INSTANCE_DESC>& instanceDescs);

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

    // �`��̃V�F�[�_�[�G���g������������.
    uint8_t* WriteHitgroupShaderRecord(uint8_t* dst, const PolygonMesh& mesh, UINT hgEntrySize);
    // ���f���̃V�F�[�_�[�G���g������������.
    uint8_t* WriteHitgroupShaderRecord(uint8_t* dst, std::shared_ptr<util::DxrModelActor> actor, UINT hgEntrySize);

    // TLAS 
    util::DynamicBuffer m_instanceDescsBuffer;
    ComPtr<ID3D12Resource> m_tlas;
    ComPtr<ID3D12Resource> m_tlasUpdate;
    dx12::Descriptor m_tlasDescriptor;

    ComPtr<ID3D12RootSignature> m_rootSignatureGlobal;

    ComPtr<ID3D12RootSignature> m_rsRGS;   // RayGen�V�F�[�_�[�̃��[�J�����[�g�V�O�l�`��.
    ComPtr<ID3D12RootSignature> m_rsFloor; // ���̃��[�J�����[�g�V�O�l�`��.
    ComPtr<ID3D12RootSignature> m_rsModel; // �X�t�B�A�̃��[�J�����[�g�V�O�l�`��.
    ComPtr<ID3D12RootSignature> m_rsSkinningCompute;
    ComPtr<ID3D12PipelineState> m_psoSkinCompute;

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
        UINT     frameIndex;
    };
    SceneParam m_sceneParam;
    UINT m_frameCount = 0;

    struct GUIParams {
        float elbowL;
        float elbowR;
        float neck;
    };
    GUIParams m_guiParams;

    util::Camera m_camera;
    util::DynamicConstantBuffer m_sceneCB;

    util::DxrModel m_modelTable;
    util::DxrModel m_modelPot;
    util::DxrModel m_modelChara;

    std::shared_ptr<util::DxrModelActor> m_actorTable;
    std::shared_ptr<util::DxrModelActor> m_actorPot1;
    std::shared_ptr<util::DxrModelActor> m_actorPot2;
    std::shared_ptr<util::DxrModelActor> m_actorChara;
};