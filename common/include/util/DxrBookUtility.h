#pragma once

#include <memory>
#include <d3d12.h>
#include <wrl.h>
#include <DirectXMath.h>

#include <string>
#include <vector>
#include <filesystem>
#include <functional>

#include "GraphicsDevice.h"

namespace dx12 {
    class GraphicsDevice;
}

namespace util {
    using Microsoft::WRL::ComPtr;

    inline UINT RoundUp(size_t size, UINT align) {
        return UINT(size + align - 1) & ~(align-1);
    }

    bool LoadFile(std::vector<char>& out, const std::wstring& fileName);

    struct AccelerationStructureBuffers {
        ComPtr<ID3D12Resource> scratch;
        ComPtr<ID3D12Resource> asbuffer;
        ComPtr<ID3D12Resource> update;
    };
    // Acceleration Structure のバッファを構築する.
    //   AS をビルドするわけではない点に注意.
    AccelerationStructureBuffers CreateAccelerationStructure(
        std::unique_ptr<dx12::GraphicsDevice>& device,
        const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC& asDesc);

    // コンスタントバッファの確保.
    //  内部でサイズの切り上げ処理を適用します.
    ComPtr<ID3D12Resource> CreateConstantBuffer(
        std::unique_ptr<dx12::GraphicsDevice>& device,
        size_t requestSize
    );

    ComPtr<ID3D12Resource> CreateBuffer(
        std::unique_ptr<dx12::GraphicsDevice>& device,
        size_t requestSize,
        const void* initData,
        D3D12_HEAP_TYPE heapType,
        D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
        const wchar_t* name = nullptr
    );
    ComPtr<ID3D12Resource> CreateBufferUAV(
        std::unique_ptr<dx12::GraphicsDevice>& device,
        size_t requestSize,
        D3D12_RESOURCE_STATES initialState,
        const wchar_t* name);

    // StructuredBuffer 用のシェーダーリソースビューを生成します(フォーマット指定).
    dx12::Descriptor CreateStructuredSRV(
        std::unique_ptr<dx12::GraphicsDevice>& device,
        ComPtr<ID3D12Resource> resource,
        UINT numElements, UINT firstElement, DXGI_FORMAT format);

    // StructuredBuffer 用のシェーダーリソースビューを生成します(ストライド指定).
    dx12::Descriptor CreateStructuredSRV(
        std::unique_ptr<dx12::GraphicsDevice>& device,
        ComPtr<ID3D12Resource> resource,
        UINT numElements, UINT firstElement, UINT stride);

    // StructuredBuffer 用の UAV ディスクリプタを生成します.
    dx12::Descriptor CreateStructuredUAV(
        std::unique_ptr<dx12::GraphicsDevice>& device,
        ComPtr<ID3D12Resource> resource,
        UINT numElements, UINT firstElement, UINT stride);
 
    // UTF-8 文字列からワイドキャラ文字列へ変換します.
    std::wstring ConvertFromUTF8(const std::string& s);

    // 3角形ポリゴンによる形状情報.
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

        // 使用するヒットグループの名前.
        std::wstring shaderName;
    };

    // 非ポリゴンによる形状情報.
    struct ProcedualMesh {
        ComPtr<ID3D12Resource> aabbBuffer;
        ComPtr<ID3D12Resource> blas;
        // 使用するヒットグループの名前.
        std::wstring shaderName;
    };

    // ポリゴンメッシュ情報から D3D12_RAYTRACING_GEOMETRY_DESC を構成する.
    //  構成される値は標準的な設定になるため、取得後適宜変更すること.
    D3D12_RAYTRACING_GEOMETRY_DESC GetGeometryDesc(const PolygonMesh& mesh);

    // 非ポリゴンメッシュ情報から D3D12_RAYTRACING_GEOMETRY_DESC を構成する.
    //  構成される値は標準的な設定になるため、取得後適宜変更すること.
    D3D12_RAYTRACING_GEOMETRY_DESC GetGeometryDesc(const ProcedualMesh& mesh);

    // Shader Table 構築時に各値を書き込むための関数.
    UINT WriteShaderIdentifier(void* dst, const void* shaderId);
    UINT WriteGPUDescriptor(void* dst, const dx12::Descriptor& descriptor);
    UINT WriteGpuResourceAddr(void* dst, const ComPtr<ID3D12Resource>& res);
    UINT WriteGpuResourceAddr(void* dst, const D3D12_GPU_VIRTUAL_ADDRESS addr);


    // RootSignature 生成のためのヘルパークラス.
    class RootSignatureHelper {
    public:
        enum class RootType {
            CBV = D3D12_ROOT_PARAMETER_TYPE_CBV,
            SRV = D3D12_ROOT_PARAMETER_TYPE_SRV,
            UAV = D3D12_ROOT_PARAMETER_TYPE_UAV,
        };
        enum class RangeType {
            SRV = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
            UAV = D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
            CBV = D3D12_DESCRIPTOR_RANGE_TYPE_CBV,
            Sampler = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER
        };
        enum class AddressMode {
            Wrap = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            Mirror = D3D12_TEXTURE_ADDRESS_MODE_MIRROR,
            Clamp = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            Border = D3D12_TEXTURE_ADDRESS_MODE_BORDER,
            MirrorOnce = D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE
        };

        void Add(
            RootType type,
            UINT shaderRegister,
            UINT registerSpace = 0
        );

        void Add(
            RangeType type,
            UINT shaderRegister,
            UINT registerSpace = 0,
            UINT descriptorCount = 1
        );

        void AddStaticSampler(
            UINT shaderRegister,
            UINT registerSpace = 0,
            D3D12_FILTER filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR,
            AddressMode addressU = AddressMode::Wrap,
            AddressMode addressV = AddressMode::Wrap,
            AddressMode addressW = AddressMode::Wrap
        );

        void Clear();

        ComPtr<ID3D12RootSignature> Create(
            std::unique_ptr<dx12::GraphicsDevice>& device,
            bool isLocalRoot, const wchar_t* name);
    private:
        std::vector<D3D12_ROOT_PARAMETER> m_params;
        std::vector<D3D12_DESCRIPTOR_RANGE> m_ranges;
        std::vector<UINT> m_rangeLocation;
        std::vector<D3D12_STATIC_SAMPLER_DESC> m_samplers;
    };

    class DynamicConstantBuffer {
    public:
        using ResourceType = ComPtr<ID3D12Resource>;
        using Device = std::unique_ptr<dx12::GraphicsDevice>;

        ~DynamicConstantBuffer();

        bool Initialize(Device& device, UINT requestSize, const wchar_t* name=L"");

        void Write(UINT bufferIndex, const void* src, UINT size);

        ResourceType Get(UINT bufferIndex) const { return m_resources[bufferIndex]; }
    private:
        std::vector<ResourceType> m_resources;
    };

    class DynamicBuffer {
    public:
        using ResourceType = ComPtr<ID3D12Resource>;
        using Device = std::unique_ptr<dx12::GraphicsDevice>;
        ~DynamicBuffer();

        bool Initialize(Device& device, UINT requestSize, const wchar_t* name = L"");

        void* Map(UINT bufferIndex);
        void Unmap(UINT bufferIndex);

        ResourceType Get(UINT bufferIndex) const { return m_resources[bufferIndex]; }
        dx12::Descriptor GetDescriptor(UINT bufferIndex) const { return m_descriptors[bufferIndex]; }
    private:
        std::vector<ResourceType> m_resources;
        std::vector<dx12::Descriptor> m_descriptors;
        std::function<void(dx12::Descriptor)> m_deallocator;
    };

    // シェーダーをコンパイルし、シェーダーバイナリを返す.
    //  シェーダーの種類は ライブラリ, シェーダーモデルは Shader Model 6.4 とする.
    std::vector<char> CompileShader(const std::filesystem::path& shaderFile);
}

namespace util {
    namespace primitive {
        using namespace DirectX;
        struct VertexPN {
            XMFLOAT3 Position;
            XMFLOAT3 Normal;
        };
        struct VertexPNC {
            XMFLOAT3 Position;
            XMFLOAT3 Normal;
            XMFLOAT4 Color;
        };
        struct VertexPNT {
            XMFLOAT3 Position;
            XMFLOAT3 Normal;
            XMFLOAT2 UV;
        };

        void GetPlane(std::vector<VertexPN>& vertices, std::vector<UINT>& indices, float size = 10.f);
        void GetPlane(std::vector<VertexPNC>& vertices, std::vector<UINT>& indices, float size = 10.f);
        void GetPlane(std::vector<VertexPNT>& vertices, std::vector<UINT>& indices, float size = 10.f);

        void GetColoredCube(std::vector<VertexPNC>& vertices, std::vector<UINT>& indices, float size = 1.0f);

        void GetPlaneXY(std::vector<VertexPNT>& vertices, std::vector<UINT>& indices, float size = 1.f);
        void GetSphere(std::vector<VertexPN>& vertices, std::vector<UINT>& indices, float radius = 1.0f, int slices = 16, int stacks = 24);
        void GetSphere(std::vector<VertexPNT>& vertices, std::vector<UINT>& indices, float radius = 1.0f, int slices = 16, int stacks = 24);
    }
}