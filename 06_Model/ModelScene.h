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

    // レイトレーシング用の StateObject を構築します.
    void CreateStateObject();

    // レイトレーシング結果書き込み用バッファを生成します.
    void CreateResultBuffer();

    // ルートシグネチャ(Global) を生成します.
    void CreateRootSignatureGlobal();

    // RayGenシェーダー用ローカルルートシグネチャを生成します.
    void CreateRayGenLocalRootSignature();

    // スフィア用のローカルルートシグネチャを生成します.
    void CreateModelLocalRootSignature();

    // 床用のローカルルートシグネチャを生成します.
    void CreateFloorLocalRootSignature();

    // レイトレーシングで使用する ShaderTable を構築します.
    void CreateShaderTable();

    void RenderHUD();

    void UpdateSceneTLAS(UINT frameIndex);

    // モデルデータの準備.
    void PrepareModels();

    // シーン内にオブジェクトを配置する.
    void DeployObjects(std::vector<D3D12_RAYTRACING_INSTANCE_DESC>& instanceDescs);

    struct PolygonMesh {
        ComPtr<ID3D12Resource> vertexBuffer;
        ComPtr<ID3D12Resource> indexBuffer;

        // ジオメトリ情報を参照するディスクリプタ.
        dx12::Descriptor descriptorVB;
        dx12::Descriptor descriptorIB;

        UINT vertexCount = 0;
        UINT indexCount = 0;
        UINT vertexStride = 0;

        // BLAS 用バッファ.
        ComPtr<ID3D12Resource> blas;

        std::wstring shaderName;
    };
    PolygonMesh m_meshPlane;

    // 形状のシェーダーエントリを書き込む.
    uint8_t* WriteHitgroupShaderRecord(uint8_t* dst, const PolygonMesh& mesh, UINT hgEntrySize);
    // モデルのシェーダーエントリを書き込む.
    uint8_t* WriteHitgroupShaderRecord(uint8_t* dst, std::shared_ptr<util::DxrModelActor> actor, UINT hgEntrySize);

    // TLAS 
    util::DynamicBuffer m_instanceDescsBuffer;
    ComPtr<ID3D12Resource> m_tlas;
    ComPtr<ID3D12Resource> m_tlasUpdate;
    dx12::Descriptor m_tlasDescriptor;

    ComPtr<ID3D12RootSignature> m_rootSignatureGlobal;

    ComPtr<ID3D12RootSignature> m_rsRGS;   // RayGenシェーダーのローカルルートシグネチャ.
    ComPtr<ID3D12RootSignature> m_rsFloor; // 床のローカルルートシグネチャ.
    ComPtr<ID3D12RootSignature> m_rsModel; // スフィアのローカルルートシグネチャ.
    ComPtr<ID3D12RootSignature> m_rsSkinningCompute;
    ComPtr<ID3D12PipelineState> m_psoSkinCompute;

    // DXR 結果書き込み用バッファ.
    ComPtr<ID3D12Resource> m_dxrOutput;
    dx12::Descriptor m_outputDescriptor;

    ComPtr<ID3D12StateObject> m_rtState;
    ComPtr<ID3D12Resource> m_shaderTable;
    ComPtr<ID3D12GraphicsCommandList4> m_commandList;

    D3D12_DISPATCH_RAYS_DESC m_dispatchRayDesc;

    struct SceneParam
    {
        XMMATRIX mtxView;       // ビュー行列.
        XMMATRIX mtxProj;       // プロジェクション行列.
        XMMATRIX mtxViewInv;    // ビュー逆行列.
        XMMATRIX mtxProjInv;    // プロジェクション逆行列.
        XMVECTOR lightDirection; // 平行光源の向き.
        XMVECTOR lightColor;    // 平行光源色.
        XMVECTOR ambientColor;  // 環境光.
        XMVECTOR eyePosition;   // 視点.
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