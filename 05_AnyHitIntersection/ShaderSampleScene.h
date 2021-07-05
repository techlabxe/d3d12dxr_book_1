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

    // レイトレーシング用の StateObject を構築します.
    void CreateStateObject();

    // レイトレーシング結果書き込み用バッファを生成します.
    void CreateResultBuffer();

    // ルートシグネチャ(Global) を生成します.
    void CreateRootSignatureGlobal();

    // RayGenシェーダー用ローカルルートシグネチャを生成します.
    void CreateRayGenLocalRootSignature();

    // スフィア用のローカルルートシグネチャを生成します.
    void CreateSphereLocalRootSignature();

    // 床用のローカルルートシグネチャを生成します.
    void CreateFloorLocalRootSignature();

    // 解析的に処理するボックス・球に向けてのローカルルートシグネチャを生成します.
    void CreateAnalyticPrimsLocalRootSignature();

    // 距離関数で処理するものに向けてのローカルルートシグネチャを生成します.
    void CreateSignedDistanceFieldLocalRootSignature();

    // レイトレーシングで使用する ShaderTable を構築します.
    void CreateShaderTable();

    void UpdateSceneTLAS(UINT frameIndex);

    // シーン内にオブジェクトを配置する.
    void DeployObjects(std::vector<D3D12_RAYTRACING_INSTANCE_DESC>& instanceDescs);

    util::PolygonMesh m_meshPlane;
    util::PolygonMesh m_meshFence;

    util::ProcedualMesh m_meshAABB;
    util::ProcedualMesh m_meshSDF;

    // 形状のシェーダーレコードを書き込む.
    uint8_t* WriteHitgroupShaderRecord(uint8_t* dst, const util::PolygonMesh& mesh, dx12::Descriptor texDescriptor, UINT hgEntrySize);
    uint8_t* WriteHitgroupShaderRecord(uint8_t* dst, const util::ProcedualMesh& mesh, dx12::Descriptor texDescriptor, ComPtr<ID3D12Resource> cb, UINT hgEntrySize);


    // TLAS 
    std::array<ComPtr<ID3D12Resource>, dx12::GraphicsDevice::BackBufferCount> m_instanceDescsBuffers;
    ComPtr<ID3D12Resource> m_tlas;
    ComPtr<ID3D12Resource> m_tlasScratch;
    dx12::Descriptor m_tlasDescriptor;

    UINT m_writeBufferIndex = 0;

    ComPtr<ID3D12RootSignature> m_rootSignatureGlobal;

    ComPtr<ID3D12RootSignature> m_rsRGS;   // RayGenシェーダーのローカルルートシグネチャ.
    ComPtr<ID3D12RootSignature> m_rsFloor; // 床のローカルルートシグネチャ.
    ComPtr<ID3D12RootSignature> m_rsModel; // スフィアのローカルルートシグネチャ.
    ComPtr<ID3D12RootSignature> m_rsAnalyticPrims;  // ボックス/球で使用するローカルルートシグネチャ.
    ComPtr<ID3D12RootSignature> m_rsSdfPrims; // DistanceField のオブジェクトで使用するローカルルートシグネチャ.

    // DXR 結果書き込み用バッファ.
    ComPtr<ID3D12Resource> m_dxrOutput;
    dx12::Descriptor m_outputDescriptor;

    ComPtr<ID3D12StateObject> m_rtState;
    util::DynamicBuffer m_shaderTable;
    ComPtr<ID3D12GraphicsCommandList4> m_commandList;

    std::array<D3D12_DISPATCH_RAYS_DESC, dx12::GraphicsDevice::BackBufferCount> m_dispatchRayDescs;

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
    };
    SceneParam m_sceneParam;

    util::Camera m_camera;
    util::DynamicConstantBuffer m_sceneCB;
    util::TextureResource m_texture;
    util::TextureResource m_whiteTex;

    struct AnalyticGeometryParam
    {
        XMFLOAT3 diffuse = XMFLOAT3(0.2f, 0.7f, 0.3f);
        UINT     type = 0; // 0: BOX, 1: Sphere の種別
        XMFLOAT3 center = XMFLOAT3(0.0f, 0.0f, 0.0f);   // AABB 内のどこを中心とするか.
        float    radius = 0.45f;
    } m_analyticGeomParam;
    util::DynamicConstantBuffer m_analyticCB;

    struct SDFGeometryParam {
        XMFLOAT3 diffuse = XMFLOAT3(0.2f, 0.4f, 1.0f);
        UINT type = 0; // 0: Box, 1: Sphere, 2: Torus の種別
        XMFLOAT3 extent = XMFLOAT3(0.2f, 0.3f, 0.45f); // BOXの大きさ.
        float    radius = 0.35f; // Sphere/torus半径.
    } m_sdfGeomParam;
    util::DynamicConstantBuffer m_sdfParamCB;
};