#pragma once

#include "DxrBookFramework.h"
#include "util/DxrBookUtility.h"
#include <DirectXMath.h>

class HelloTriangle : public DxrBookFramework {
public:
    using XMFLOAT3 = DirectX::XMFLOAT3;

    HelloTriangle(UINT width, UINT height);
    void OnInit() override;
    void OnDestroy() override;

    void OnUpdate() override;
    void OnRender() override;

private:
    // 3角形データに対する BLAS を構築します.
    void CreateTriangleBLAS();

    // BLAS を束ねてシーンの TLAS を構築します.
    void CreateSceneTLAS();

    // レイトレーシング用の StateObject を構築します.
    void CreateStateObject();

    // レイトレーシング結果書き込み用バッファを生成します.
    void CreateResultBuffer();

    // レイトレーシングで使用する ShaderTable を構築します.
    void CreateShaderTable();

    // ルートシグネチャ(Global) を生成します.
    void CreateRootSignatureGlobal();

    struct Vertex {
        XMFLOAT3 Position;
    };
    ComPtr<ID3D12Resource> m_vertexBuffer;

    ComPtr<ID3D12Resource> m_blas;
    ComPtr<ID3D12Resource> m_tlas;

    ComPtr<ID3D12StateObject> m_rtState;
    ComPtr<ID3D12Resource> m_shaderTable;
    ComPtr<ID3D12RootSignature> m_rootSignatureGlobal;

    ComPtr<ID3D12Resource> m_dxrOutput;

    dx12::Descriptor m_tlasDescriptor;
    dx12::Descriptor m_outputDescriptor;

    D3D12_DISPATCH_RAYS_DESC m_dispatchRayDesc;

    ComPtr<ID3D12GraphicsCommandList4> m_commandList;
};


