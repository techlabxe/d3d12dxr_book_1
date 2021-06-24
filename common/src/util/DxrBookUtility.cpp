#include "util/DxrBookUtility.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <dxcapi.h>
#include "d3dx12.h"

#pragma comment(lib, "dxcompiler.lib")

namespace util {
    bool LoadFile(std::vector<char>& out, const std::wstring& fileName)
    {
        std::ifstream infile(fileName, std::ios::binary);
        if (!infile) {
            return false;
        }

        out.resize(infile.seekg(0, std::ios::end).tellg());
        infile.seekg(0, std::ios::beg).read(out.data(), out.size());
        return true;
    }

    AccelerationStructureBuffers CreateAccelerationStructure(
        std::unique_ptr<dx12::GraphicsDevice>& device,
        const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC& asDesc)
    {
        AccelerationStructureBuffers asb{};
        // 必要なメモリ量を求める.
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO asPrebuild{};
        device->GetDevice()->GetRaytracingAccelerationStructurePrebuildInfo(
            &asDesc.Inputs, &asPrebuild
        );

        // スクラッチバッファを確保.
        asb.scratch = device->CreateBuffer(
            asPrebuild.ScratchDataSizeInBytes,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_HEAP_TYPE_DEFAULT
        );

        // AS 用メモリバッファを確保.
        asb.asbuffer = device->CreateBuffer(
            asPrebuild.ResultDataMaxSizeInBytes,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
            D3D12_HEAP_TYPE_DEFAULT
        );

        if (asb.scratch == nullptr || asb.asbuffer == nullptr) {
            throw std::runtime_error("CreateAccelerationStructure failed.");
        }

        // アップデート用バッファを確保.
        if (asDesc.Inputs.Flags & D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE) {
            asb.update = device->CreateBuffer(
                asPrebuild.UpdateScratchDataSizeInBytes,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_HEAP_TYPE_DEFAULT
            );
            if (asb.update == nullptr) {
                throw std::runtime_error("CreateAccelerationStructure failed.");
            }
        }

        return asb;
    }

    ComPtr<ID3D12Resource> CreateConstantBuffer(std::unique_ptr<dx12::GraphicsDevice>& device, size_t requestSize)
    {
        requestSize = RoundUp(requestSize, 255);
        return device->CreateBuffer(
            requestSize,
            D3D12_RESOURCE_FLAG_NONE,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            D3D12_HEAP_TYPE_UPLOAD);
    }

    ComPtr<ID3D12Resource> CreateBuffer(std::unique_ptr<dx12::GraphicsDevice>& device, size_t requestSize, const void* initData, D3D12_HEAP_TYPE heapType, D3D12_RESOURCE_FLAGS flags, const wchar_t* name)
    {
        auto initialState = D3D12_RESOURCE_STATE_COPY_DEST;
        if (heapType == D3D12_HEAP_TYPE_UPLOAD) {
            initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
        }

        auto resource = device->CreateBuffer(
            requestSize, flags, initialState, heapType, name);
        if (resource && name != nullptr) {
            resource->SetName(name);
        }
        if (initData != nullptr) {
            if (heapType == D3D12_HEAP_TYPE_DEFAULT) {
                device->WriteToDefaultMemory(resource, initData, requestSize);
            }
            if (heapType == D3D12_HEAP_TYPE_UPLOAD) {
                device->WriteToHostVisibleMemory(resource, initData, requestSize);
            }
        }
        return resource;
    }

    ComPtr<ID3D12Resource> CreateBufferUAV(std::unique_ptr<dx12::GraphicsDevice>& device, size_t requestSize, D3D12_RESOURCE_STATES initialState, const wchar_t* name)
    {
        auto flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        return device->CreateBuffer(
            requestSize, flags,
            initialState, D3D12_HEAP_TYPE_DEFAULT
        );
    }

    dx12::Descriptor CreateStructuredSRV(
        std::unique_ptr<dx12::GraphicsDevice>& device,
        ComPtr<ID3D12Resource> resource,
        UINT numElements, UINT firstElement, DXGI_FORMAT format)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Format = format;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Buffer.NumElements = numElements;
        srvDesc.Buffer.FirstElement = firstElement;

        return device->CreateShaderResourceView(
            resource, &srvDesc);
    }

    dx12::Descriptor CreateStructuredSRV(
        std::unique_ptr<dx12::GraphicsDevice>& device,
        ComPtr<ID3D12Resource> resource,
        UINT numElements, UINT firstElement, UINT stride)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Buffer.NumElements = numElements;
        srvDesc.Buffer.FirstElement = firstElement;
        srvDesc.Buffer.StructureByteStride = stride;

        return device->CreateShaderResourceView(
            resource, &srvDesc);
    }

    dx12::Descriptor CreateStructuredUAV(
        std::unique_ptr<dx12::GraphicsDevice>& device,
        ComPtr<ID3D12Resource> resource,
        UINT numElements, UINT firstElement, UINT stride)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.Buffer.StructureByteStride = stride;
        uavDesc.Buffer.NumElements = numElements;
        uavDesc.Buffer.FirstElement = firstElement;
        return device->CreateUnorderedAccessView(
            resource, &uavDesc);
    }

    std::wstring ConvertFromUTF8(const std::string& s)
    {
        DWORD dwRet = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
        std::vector<wchar_t> buf(dwRet);
        dwRet = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, buf.data(), int(buf.size() - 1));
        return std::wstring(buf.data());
    }

    D3D12_RAYTRACING_GEOMETRY_DESC GetGeometryDesc(const PolygonMesh& mesh)
    {
        auto geometryDesc = D3D12_RAYTRACING_GEOMETRY_DESC{};
        geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
        auto& triangles = geometryDesc.Triangles;
        triangles.VertexBuffer.StartAddress = mesh.vertexBuffer->GetGPUVirtualAddress();
        triangles.VertexBuffer.StrideInBytes = mesh.vertexStride;
        triangles.VertexCount = mesh.vertexCount;
        triangles.IndexBuffer = mesh.indexBuffer->GetGPUVirtualAddress();
        triangles.IndexCount = mesh.indexCount;
        triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
        triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
        return geometryDesc;
    }

    D3D12_RAYTRACING_GEOMETRY_DESC GetGeometryDesc(const ProcedualMesh& mesh)
    {
        auto geometryDesc = D3D12_RAYTRACING_GEOMETRY_DESC{};
        geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;
        geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
        auto& aabbs = geometryDesc.AABBs;
        aabbs.AABBCount = 1;
        aabbs.AABBs.StartAddress = mesh.aabbBuffer->GetGPUVirtualAddress();
        aabbs.AABBs.StrideInBytes = sizeof(D3D12_RAYTRACING_AABB);

        return geometryDesc;
    }

    UINT WriteShaderIdentifier(void* dst, const void* shaderId)
    {
        memcpy(dst, shaderId, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        return D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    }

    UINT WriteGPUDescriptor(void* dst, const dx12::Descriptor& descriptor)
    {
        auto handle = descriptor.hGpu;
        memcpy(dst, &handle, sizeof(handle));
        return UINT(sizeof(handle));
    }

    UINT WriteGpuResourceAddr(void* dst, const ComPtr<ID3D12Resource>& res)
    {
        D3D12_GPU_VIRTUAL_ADDRESS addr = res->GetGPUVirtualAddress();
        memcpy(dst, &addr, sizeof(addr));
        return UINT(sizeof(addr));
    }
    UINT WriteGpuResourceAddr(void* dst, const D3D12_GPU_VIRTUAL_ADDRESS addr)
    {
        memcpy(dst, &addr, sizeof(addr));
        return UINT(sizeof(addr));
    }


    std::vector<char> CompileShader(const std::filesystem::path& hlslFilename)
    {
        HRESULT hr;
        std::ifstream infile(hlslFilename, std::ifstream::binary);
        if (!infile)     {
            throw std::runtime_error("failed shader compile.");
        }
        std::wstring fileName = hlslFilename.filename().wstring();
        std::stringstream strstream;
        strstream << infile.rdbuf();
        std::string shaderCode = strstream.str();

        ComPtr<IDxcLibrary> library;
        DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&library));

        ComPtr<IDxcCompiler> compiler;
        DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));

        ComPtr<IDxcBlobEncoding> source;
        library->CreateBlobWithEncodingFromPinned(
            (LPBYTE)shaderCode.c_str(), (UINT32)shaderCode.size(), CP_UTF8, &source);

        ComPtr<IDxcIncludeHandler> includeHandler;
        library->CreateIncludeHandler(&includeHandler);

        std::vector<LPCWSTR> arguments;
#if _DEBUG
        arguments.emplace_back(L"/Od");
#endif

        const auto target = L"lib_6_4";
        ComPtr<IDxcOperationResult> dxcResult;
        hr = compiler->Compile(source.Get(), fileName.c_str(), L"", target,
            arguments.data(), UINT(arguments.size()),
            nullptr, 0,
            includeHandler.Get(), &dxcResult);
        if (FAILED(hr))     {
            throw std::runtime_error("failed shader compile.");
        }

        dxcResult->GetStatus(&hr);
        if (FAILED(hr))     {
            ComPtr<IDxcBlobEncoding> errBlob;
            hr = dxcResult->GetErrorBuffer(&errBlob);

            auto size = errBlob->GetBufferSize();
            std::vector<char> infoLog(size + 1);
            memcpy(infoLog.data(), errBlob->GetBufferPointer(), size);
            infoLog.back() = 0;
            OutputDebugStringA(infoLog.data());
            throw std::runtime_error("failed shader compile");
        }

        ComPtr<IDxcBlob> blob;
        hr = dxcResult->GetResult(&blob);
        if (SUCCEEDED(hr)) {
            std::vector<char> result;
            auto size = blob->GetBufferSize();
            result.resize(size);
            memcpy(result.data(), blob->GetBufferPointer(), size);
            return result;
        }
        throw std::runtime_error("failed shader compile");
    }


    void RootSignatureHelper::Add(RootType type, UINT shaderRegister, UINT registerSpace)
    {
        D3D12_ROOT_PARAMETER rootParam{};
        rootParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rootParam.ParameterType = static_cast<D3D12_ROOT_PARAMETER_TYPE>(type);
        rootParam.Descriptor.ShaderRegister = shaderRegister;
        rootParam.Descriptor.RegisterSpace = registerSpace;
        m_params.push_back(rootParam);
    }

    void RootSignatureHelper::Add(RangeType type, UINT shaderRegister, UINT registerSpace, UINT descriptorCount)
    {
        D3D12_DESCRIPTOR_RANGE range{};
        range.RangeType = static_cast<D3D12_DESCRIPTOR_RANGE_TYPE>(type);
        range.NumDescriptors = descriptorCount;
        range.BaseShaderRegister = shaderRegister;
        range.RegisterSpace = registerSpace;
        range.OffsetInDescriptorsFromTableStart = 0;
        m_ranges.push_back(range);

        // あとで Range 解決するのでインデックスを保存.
        UINT rangeIndex = UINT(m_params.size());
        m_rangeLocation.push_back(rangeIndex);

        D3D12_ROOT_PARAMETER rootParam{};
        rootParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rootParam.DescriptorTable.NumDescriptorRanges = 1;
        rootParam.DescriptorTable.pDescriptorRanges = nullptr; // ここは生成時に解決する.
        m_params.push_back(rootParam);
    }

    void RootSignatureHelper::AddStaticSampler(UINT shaderRegister, UINT registerSpace, D3D12_FILTER filter, AddressMode addressU, AddressMode addressV, AddressMode addressW)
    {
        CD3DX12_STATIC_SAMPLER_DESC desc;
        desc.Init(shaderRegister,
            filter,
            static_cast<D3D12_TEXTURE_ADDRESS_MODE>(addressU),
            static_cast<D3D12_TEXTURE_ADDRESS_MODE>(addressV),
            static_cast<D3D12_TEXTURE_ADDRESS_MODE>(addressW)
        );
        desc.RegisterSpace = registerSpace;
        m_samplers.push_back(desc);
    }

    void RootSignatureHelper::Clear() {
        m_params.clear();
        m_ranges.clear();
        m_rangeLocation.clear();
        m_samplers.clear();
    }

    ComPtr<ID3D12RootSignature> RootSignatureHelper::Create(std::unique_ptr<dx12::GraphicsDevice>& device, bool isLocalRoot, const wchar_t* name)
    {
        for (UINT i = 0; i < UINT(m_ranges.size()); ++i) {
            auto index = m_rangeLocation[i];
            m_params[index].DescriptorTable.pDescriptorRanges = &m_ranges[i];
        }
        D3D12_ROOT_SIGNATURE_DESC desc{};
        if (!m_params.empty()) {
            desc.pParameters = m_params.data();
            desc.NumParameters = UINT(m_params.size());
        }
        if (!m_samplers.empty()) {
            desc.pStaticSamplers = m_samplers.data();
            desc.NumStaticSamplers = UINT(m_samplers.size());
        }
        if (isLocalRoot) {
            desc.Flags |= D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
        }

        ComPtr<ID3DBlob> blob, errBlob;
        HRESULT hr = D3D12SerializeRootSignature(
            &desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &blob, &errBlob);
        if (FAILED(hr)) {
            return nullptr;
        }

        ComPtr<ID3D12RootSignature> rootSignature;
        hr = device->GetDevice()->CreateRootSignature(
            0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
        if (FAILED(hr)) {
            return nullptr;
        }

        return rootSignature;
    }

    DynamicConstantBuffer::~DynamicConstantBuffer()
    {
    }

    bool DynamicConstantBuffer::Initialize(Device& device, UINT requestSize, const wchar_t* name)
    {
        requestSize = RoundUp(requestSize, 256);
        UINT count = device->BackBufferCount;
        m_resources.resize(count);

        for (UINT i = 0; i < count; ++i) {
            m_resources[i] = device->CreateBuffer(
                requestSize,
                D3D12_RESOURCE_FLAG_NONE,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                D3D12_HEAP_TYPE_UPLOAD);
            if (m_resources[i]) {
                m_resources[i]->SetName(name);
            } else {
                return false;
            }
        }
        return true;
    }

    void DynamicConstantBuffer::Write(UINT bufferIndex, const void* src, UINT size)
    {
        auto& buffer = m_resources[bufferIndex];
        void* dst = nullptr;
        buffer->Map(0, nullptr, &dst);
        if (dst) {
            memcpy(dst, src, size);
            buffer->Unmap(0, nullptr);
        }
    }

    DynamicBuffer::~DynamicBuffer() {
        for (auto& desc : m_descriptors) {
            m_deallocator(desc);
        }
    }

    bool DynamicBuffer::Initialize(Device& device, UINT requestSize, const wchar_t* name)
    {
        requestSize = RoundUp(requestSize, 256);
        UINT count = device->BackBufferCount;
        m_resources.resize(count);

        for (UINT i = 0; i < count; ++i) {
            m_resources[i] = device->CreateBuffer(
                requestSize,
                D3D12_RESOURCE_FLAG_NONE,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                D3D12_HEAP_TYPE_UPLOAD);
            if (m_resources[i]) {
                m_resources[i]->SetName(name);
            } else {
                return false;
            }
        }
        m_descriptors.resize(count);
        m_deallocator = [&](dx12::Descriptor desc) {
            if (device) {
                device->DeallocateDescriptor(desc);
            }
        };
        for (UINT i = 0; i < count; ++i) {
            m_descriptors[i] = device->AllocateDescriptor();

        }
        return true;
    }

    void* DynamicBuffer::Map(UINT bufferIndex) {
        void* p = nullptr;
        m_resources[bufferIndex]->Map(0, nullptr, &p);
        return p;
    }
    void DynamicBuffer::Unmap(UINT bufferIndex) {
        m_resources[bufferIndex]->Unmap(0, nullptr);
    }

}


void util::primitive::GetPlane(std::vector<VertexPN>& vertices, std::vector<UINT>& indices, float size)
{
    VertexPN srcVertices[] = {
        VertexPN{ {-1.0f, 0.0f,-1.0f }, { 0.0f, 1.0f, 0.0f } },
        VertexPN{ {-1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
        VertexPN{ { 1.0f, 0.0f,-1.0f }, { 0.0f, 1.0f, 0.0f } },
        VertexPN{ { 1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
    };
    vertices.resize(4);
    std::transform(
        std::begin(srcVertices), std::end(srcVertices), vertices.begin(),
        [=](auto v) {
            v.Position.x *= size;
            v.Position.z *= size;
            return v;
        }
    );
    indices = { 0, 1, 2, 2, 1, 3 };
}

void util::primitive::GetPlane(std::vector<VertexPNC>& vertices, std::vector<UINT>& indices, float size)
{
    const auto white = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    VertexPNC srcVertices[] = {
        VertexPNC{ {-1.0f, 0.0f,-1.0f }, { 0.0f, 1.0f, 0.0f }, white },
        VertexPNC{ {-1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f }, white },
        VertexPNC{ { 1.0f, 0.0f,-1.0f }, { 0.0f, 1.0f, 0.0f }, white },
        VertexPNC{ { 1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f }, white },
    };
    vertices.resize(4);
    std::transform(
        std::begin(srcVertices), std::end(srcVertices), vertices.begin(),
        [=](auto v) {
            v.Position.x *= size;
            v.Position.z *= size;
            return v;
        }
    );
    indices = { 0, 1, 2, 2, 1, 3 };
}

void util::primitive::GetPlane(std::vector<VertexPNT>& vertices, std::vector<UINT>& indices, float size)
{
    VertexPNT srcVertices[] = {
        VertexPNT{ {-1.0f, 0.0f,-1.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f} },
        VertexPNT{ {-1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f} },
        VertexPNT{ { 1.0f, 0.0f,-1.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f} },
        VertexPNT{ { 1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f} },
    };
    vertices.resize(4);
    std::transform(
        std::begin(srcVertices), std::end(srcVertices), vertices.begin(),
        [=](auto v) {
            v.Position.x *= size;
            v.Position.z *= size;
            return v;
        }
    );
    indices = { 0, 1, 2, 2, 1, 3 };
}


void util::primitive::GetColoredCube(std::vector<VertexPNC>& vertices, std::vector<UINT>& indices, float size)
{
    vertices.clear();
    indices.clear();

    const DirectX::XMFLOAT4 red(1.0f, 0.0f, 0.0f, 1.0f);
    const DirectX::XMFLOAT4 green(0.0f, 1.0f, 0.0f, 1.0f);
    const DirectX::XMFLOAT4 blue(0.0f, 0.0f, 1.0f, 1.0);
    const DirectX::XMFLOAT4 white(1.0f, 1.0f, 1.0f, 1.0f);
    const DirectX::XMFLOAT4 black(0.0f, 0.0f, 0.0f, 1.0f);
    const DirectX::XMFLOAT4 yellow(1.0f, 1.0f, 0.0f, 1.0f);
    const DirectX::XMFLOAT4 magenta(1.0f, 0.0f, 1.0f, 1.0f);
    const DirectX::XMFLOAT4 cyan(0.0f, 1.0f, 1.0f, 1.0f);

    vertices = {
        // 裏
        { {-1.0f,-1.0f,-1.0f}, { 0.0f, 0.0f, -1.0f }, red },
        { {-1.0f, 1.0f,-1.0f}, { 0.0f, 0.0f, -1.0f }, yellow },
        { { 1.0f, 1.0f,-1.0f}, { 0.0f, 0.0f, -1.0f }, white },
        { { 1.0f,-1.0f,-1.0f}, { 0.0f, 0.0f, -1.0f }, magenta },
        // 右
        { { 1.0f,-1.0f,-1.0f}, { 1.0f, 0.0f, 0.0f }, magenta },
        { { 1.0f, 1.0f,-1.0f}, { 1.0f, 0.0f, 0.0f }, white},
        { { 1.0f, 1.0f, 1.0f}, { 1.0f, 0.0f, 0.0f }, cyan},
        { { 1.0f,-1.0f, 1.0f}, { 1.0f, 0.0f, 0.0f }, blue},
        // 左
        { {-1.0f,-1.0f, 1.0f}, { -1.0f, 0.0f, 0.0f }, black},
        { {-1.0f, 1.0f, 1.0f}, { -1.0f, 0.0f, 0.0f }, green},
        { {-1.0f, 1.0f,-1.0f}, { -1.0f, 0.0f, 0.0f }, yellow},
        { {-1.0f,-1.0f,-1.0f}, { -1.0f, 0.0f, 0.0f }, red},
        // 正面
        { { 1.0f,-1.0f, 1.0f}, { 0.0f, 0.0f, 1.0f}, blue},
        { { 1.0f, 1.0f, 1.0f}, { 0.0f, 0.0f, 1.0f}, cyan},
        { {-1.0f, 1.0f, 1.0f}, { 0.0f, 0.0f, 1.0f}, green},
        { {-1.0f,-1.0f, 1.0f}, { 0.0f, 0.0f, 1.0f}, black},
        // 上
        { {-1.0f, 1.0f,-1.0f}, { 0.0f, 1.0f, 0.0f}, yellow},
        { {-1.0f, 1.0f, 1.0f}, { 0.0f, 1.0f, 0.0f}, green },
        { { 1.0f, 1.0f, 1.0f}, { 0.0f, 1.0f, 0.0f}, cyan },
        { { 1.0f, 1.0f,-1.0f}, { 0.0f, 1.0f, 0.0f}, white},
        // 底
        { {-1.0f,-1.0f, 1.0f}, { 0.0f, -1.0f, 0.0f}, black},
        { {-1.0f,-1.0f,-1.0f}, { 0.0f, -1.0f, 0.0f}, red},
        { { 1.0f,-1.0f,-1.0f}, { 0.0f, -1.0f, 0.0f}, magenta},
        { { 1.0f,-1.0f, 1.0f}, { 0.0f, -1.0f, 0.0f}, blue},
    };
    indices = {
        0, 1, 2, 2, 3,0,
        4, 5, 6, 6, 7,4,
        8, 9, 10, 10, 11, 8,
        12,13,14, 14,15,12,
        16,17,18, 18,19,16,
        20,21,22, 22,23,20,
    };

    std::transform(
        vertices.begin(), vertices.end(), vertices.begin(),
        [=](auto v) {
            v.Position.x *= size;
            v.Position.y *= size;
            v.Position.z *= size;
            return v;
        }
    );
}
 

void util::primitive::GetPlaneXY(std::vector<VertexPNT>& vertices, std::vector<UINT>& indices, float size)
{
    VertexPNT srcVertices[] = {
        VertexPNT{ {-1.0f,  1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, {0.0f, 0.0f} },
        VertexPNT{ {-1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, {0.0f, 1.0f} },
        VertexPNT{ { 1.0f,  1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, {1.0f, 0.0f} },
        VertexPNT{ { 1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, {1.0f, 1.0f} },
    };
    vertices.resize(4);
    std::transform(
        std::begin(srcVertices), std::end(srcVertices), vertices.begin(),
        [=](auto v) {
            v.Position.x *= size;
            v.Position.y *= size;
            return v;
        }
    );
    indices = { 0, 1, 2, 2, 1, 3 };
}


static void SetSphereVertex(
    util::primitive::VertexPN& vert, 
    const DirectX::XMVECTOR& position, const DirectX::XMVECTOR& normal, const DirectX::XMFLOAT2& uv) {
    XMStoreFloat3(&vert.Position, position);
    XMStoreFloat3(&vert.Normal, normal);
}
static void SetSphereVertex(
    util::primitive::VertexPNT& vert,
    const DirectX::XMVECTOR& position, const DirectX::XMVECTOR& normal, const DirectX::XMFLOAT2& uv) {
    XMStoreFloat3(&vert.Position, position);
    XMStoreFloat3(&vert.Normal, normal);
    vert.UV = uv;
}

template<class T>
static void CreateSphereVertices(std::vector<T>& vertices, float radius, int slices, int stacks) {
    using namespace DirectX;

    vertices.clear();
    const auto SLICES = float(slices);
    const auto STACKS = float(stacks);
    for (int stack = 0; stack <= stacks; ++stack) {
        for (int slice = 0; slice <= slices; ++slice) {
            XMFLOAT3 p;
            p.y = 2.0f * stack / STACKS - 1.0f;
            float r = std::sqrtf(1 - p.y * p.y);
            float theta = 2.0f * XM_PI * slice / SLICES;
            p.x = r * std::sinf(theta);
            p.z = r * std::cosf(theta);

            XMVECTOR v = XMLoadFloat3(&p) * radius;
            XMVECTOR n = XMVector3Normalize(v);
            XMFLOAT2 uv = {
                float(slice) / SLICES,
                1.0f-float(stack) / STACKS,
            };

            T vtx{};
            SetSphereVertex(vtx, v, n, uv);
            vertices.push_back(vtx);
        }
    }
}
static void CreateSphereIndices(std::vector<UINT>& indices, int slices, int stacks) {
    for (int stack = 0; stack < stacks; ++stack) {
        const int sliceMax = slices + 1;
        for (int slice = 0; slice < slices; ++slice) {
            int idx = stack * sliceMax;
            int i0 = idx + (slice + 0) % sliceMax;
            int i1 = idx + (slice + 1) % sliceMax;
            int i2 = i0 + sliceMax;
            int i3 = i1 + sliceMax;

            indices.push_back(i0); indices.push_back(i1); indices.push_back(i2);
            indices.push_back(i2); indices.push_back(i1); indices.push_back(i3);
        }
    }
}

void util::primitive::GetSphere(std::vector<VertexPN>& vertices, std::vector<UINT>& indices, float radius, int slices, int stacks)
{
    vertices.clear();
    indices.clear();
    CreateSphereVertices(vertices, radius, slices, stacks);
    CreateSphereIndices(indices, slices, stacks);
}

void util::primitive::GetSphere(std::vector<VertexPNT>& vertices, std::vector<UINT>& indices, float radius, int slices, int stacks)
{
    vertices.clear();
    indices.clear();
    CreateSphereVertices(vertices, radius, slices, stacks);
    CreateSphereIndices(indices, slices, stacks);
}

