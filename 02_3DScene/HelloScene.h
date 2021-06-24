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
    // シーンに必要なオブジェクトを生成する.
    void CreateSceneObjects();

    void CreateSceneBLAS();

    void CreateSceneTLAS();

    // レイトレーシング用の StateObject を構築します.
    void CreateStateObject();

    // レイトレーシング結果書き込み用バッファを生成します.
    void CreateResultBuffer();

    // ルートシグネチャ(Global) を生成します.
    void CreateRootSignatureGlobal();

    // RayGenシェーダー用ローカルルートシグネチャを生成します.
    void CreateLocalRootSignatureRayGen();

    // ClosestHitシェーダー用ローカルルートシグネチャを生成します.
    void CreateLocalRootSignatureCHS();

    // レイトレーシングで使用する ShaderTable を構築します.
    void CreateShaderTable();

    void UpdateHUD();
    void RenderHUD();

    // シーン内にオブジェクトを配置する.
    void DeployObjects(std::vector<D3D12_RAYTRACING_INSTANCE_DESC>& instanceDescs);

    
    // シェーダーレコードを書き込む(HitGroup).
    uint8_t* WriteShaderRecord(uint8_t* dst, const util::PolygonMesh& mesh, UINT recordSize);

    // ポリゴンメッシュ(床用Plane)
    util::PolygonMesh m_meshPlane;
    // ポリゴンメッシュ(Cube)
    util::PolygonMesh m_meshCube;

    // TLAS 用バッファ.
    ComPtr<ID3D12Resource> m_tlas;
    dx12::Descriptor m_tlasDescriptor;

    ComPtr<ID3D12RootSignature> m_rootSignatureGlobal;

    ComPtr<ID3D12RootSignature> m_rsRGS;   // RayGenシェーダーのローカルルートシグネチャ.
    ComPtr<ID3D12RootSignature> m_rsModel; // 床・キューブ用のローカルルートシグネチャ.

    // DXR 結果書き込み用バッファ.
    ComPtr<ID3D12Resource> m_dxrOutput;
    dx12::Descriptor m_outputDescriptor;

    ComPtr<ID3D12StateObject> m_rtState;
    ComPtr<ID3D12GraphicsCommandList4> m_commandList;

    struct SceneParam
    {
        XMMATRIX mtxView;       // ビュー行列.
        XMMATRIX mtxProj;       // プロジェクション行列.
        XMMATRIX mtxViewInv;    // ビュー逆行列.
        XMMATRIX mtxProjInv;    // プロジェクション逆行列.
        XMVECTOR lightDirection; // 平行光源の向き.
        XMVECTOR lightColor;    // 平行光源色.
        XMVECTOR ambientColor;  // 環境光.
    };
    SceneParam m_sceneParam;

    util::DynamicConstantBuffer m_sceneCB;
    util::Camera m_camera;

    // 動的な定数バッファを使うため、参照するシェーダーテーブルらもバッファ分構成する.
    static const UINT BackBufferCount = dx12::GraphicsDevice::BackBufferCount;
    ComPtr<ID3D12Resource> m_shaderTable;
    D3D12_DISPATCH_RAYS_DESC m_dispatchRayDesc;
};
