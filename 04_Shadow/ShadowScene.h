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

    // レイトレーシング用の StateObject を構築します.
    void CreateStateObject();

    // レイトレーシング結果書き込み用バッファを生成します.
    void CreateResultBuffer();

    // ルートシグネチャ(Global) を生成します.
    void CreateRootSignatureGlobal();

    // RayGenシェーダー用ローカルルートシグネチャを生成します.
    void CreateLocalRootSignatureRayGen();

    // スフィア用のローカルルートシグネチャを生成します.
    void CreateSphereLocalRootSignature();

    // 床用のローカルルートシグネチャを生成します.
    void CreateFloorLocalRootSignature();

    // レイトレーシングで使用する ShaderTable を構築します.
    void CreateShaderTable();

    // シーン内にオブジェクトを配置する.
    void DeployObjects(std::vector<D3D12_RAYTRACING_INSTANCE_DESC>& instanceDescs);

    // TLAS を再構築します.
    void UpdateSceneTLAS(UINT frameIndex);


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
    PolygonMesh m_meshSphere;
    PolygonMesh m_meshLightSphere;

    // PolygonMesh を元に、値セット済みの D3D12_RAYTRACING_GEOMETRY_DESC を取得.
    D3D12_RAYTRACING_GEOMETRY_DESC GetGeometryDescFromPolygonMesh(const PolygonMesh& mesh);
    
    // 形状のシェーダーエントリを書き込む.
    uint8_t* WriteHitgroupShaderEntry(uint8_t* dst, const PolygonMesh& mesh, UINT hgEntrySize);

    // TLAS 用バッファ.
    util::DynamicBuffer m_instanceDescsBuffer;
    ComPtr<ID3D12Resource> m_tlas;
    ComPtr<ID3D12Resource> m_tlasUpdate;

    dx12::Descriptor m_tlasDescriptor;

    ComPtr<ID3D12RootSignature> m_rootSignatureGlobal;

    ComPtr<ID3D12RootSignature> m_rsRGS;   // RayGenシェーダーのローカルルートシグネチャ.
    ComPtr<ID3D12RootSignature> m_rsFloor; // 床のローカルルートシグネチャ.
    ComPtr<ID3D12RootSignature> m_rsSphere; // スフィアのローカルルートシグネチャ.

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

        XMFLOAT3 pointLightPosition; // ポイントライト位置.
        UINT     shadowRayCount; // シャドウレイ数.
        XMUINT4  flags;
    };
    SceneParam m_sceneParam;
    XMFLOAT3 m_lightPos;
    float    m_lightDistanceFactor = 1.0;

    // スフィアごとに持たせる情報.
    struct SphereInstance {
        XMMATRIX mtxWorld = DirectX::XMMatrixIdentity();
    };
    std::array<SphereInstance, 10> m_spheres;
    SphereInstance m_pointLight;

    util::Camera m_camera;
    util::DynamicConstantBuffer m_sceneCB;

};