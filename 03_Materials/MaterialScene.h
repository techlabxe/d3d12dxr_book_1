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

    void UpdateHUD();
    void RenderHUD();

    // シーン内にオブジェクトを配置する.
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

        // ジオメトリ情報を参照するディスクリプタ.
        dx12::Descriptor descriptorVB;
        dx12::Descriptor descriptorIB;

        UINT vertexCount = 0;
        UINT indexCount = 0;
        UINT vertexStride = 0;

        // BLAS 用バッファ.
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


    // TLAS 用バッファ.
    ComPtr<ID3D12Resource> m_tlas;
    dx12::Descriptor m_tlasDescriptor;

    ComPtr<ID3D12RootSignature> m_rootSignatureGlobal;

    ComPtr<ID3D12RootSignature> m_rsRGS;   // RayGenシェーダーのローカルルートシグネチャ.
    ComPtr<ID3D12RootSignature> m_rsFloor; // 床のローカルルートシグネチャ.
    ComPtr<ID3D12RootSignature> m_rsSphere1; // スフィアのローカルルートシグネチャ(反射・屈折用).
    ComPtr<ID3D12RootSignature> m_rsSphere2; // スフィアのローカルルートシグネチャ(Phong用).

    // DXR 結果書き込み用バッファ.
    ComPtr<ID3D12Resource> m_dxrOutput;
    dx12::Descriptor m_outputDescriptor;

    ComPtr<ID3D12StateObject> m_rtState;
    ComPtr<ID3D12Resource> m_shaderTable;
    ComPtr<ID3D12GraphicsCommandList4> m_commandList;

    D3D12_DISPATCH_RAYS_DESC m_dispatchRayDesc;

    struct MaterialParam
    {
        XMVECTOR albedo;
        XMVECTOR specular; // スペキュラー色 & w要素にPower
    };


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

    std::array<XMMATRIX, ReflectSpheres> m_spheresReflect;
    std::array<XMMATRIX, RefractSpheres> m_spheresRefract;
    std::array<XMMATRIX, NormalSpheres> m_spheresNormal;

    // Phong描画するための通常スフィアのマテリアル情報.
    std::array<MaterialParam, NormalSpheres> m_normalSphereMaterials;
    ComPtr<ID3D12Resource> m_normalSphereMaterialCB;


    TextureResource m_background;
    TextureResource m_groundTex;

    static const UINT BackBufferCount = dx12::GraphicsDevice::BackBufferCount;
    util::DynamicConstantBuffer m_sceneCB;

    util::Camera m_camera;


};
