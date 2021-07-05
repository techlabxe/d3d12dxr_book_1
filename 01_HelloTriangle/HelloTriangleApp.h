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
    // 3�p�`�f�[�^�ɑ΂��� BLAS ���\�z���܂�.
    void CreateTriangleBLAS();

    // BLAS �𑩂˂ăV�[���� TLAS ���\�z���܂�.
    void CreateSceneTLAS();

    // ���C�g���[�V���O�p�� StateObject ���\�z���܂�.
    void CreateStateObject();

    // ���C�g���[�V���O���ʏ������ݗp�o�b�t�@�𐶐����܂�.
    void CreateResultBuffer();

    // ���C�g���[�V���O�Ŏg�p���� ShaderTable ���\�z���܂�.
    void CreateShaderTable();

    // ���[�g�V�O�l�`��(Global) �𐶐����܂�.
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


