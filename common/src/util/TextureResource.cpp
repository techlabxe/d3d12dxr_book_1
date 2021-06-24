#include "GraphicsDevice.h"
#include "util/TextureResource.h"

#include <DirectXTex.h>
#include "d3dx12.h"

#include <iostream>
#include <fstream>
#include <vector>
#include <string>

namespace util {
    using namespace DirectX;
    template<class T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    TextureResource LoadTextureFromMemory(
        const void* data, UINT64 size,
        std::unique_ptr<dx12::GraphicsDevice>& device)
    {
        DirectX::TexMetadata metadata;
        DirectX::ScratchImage image;

        HRESULT hr = E_FAIL;
        TextureResource res{};
        hr = LoadFromDDSMemory(data, size, DDS_FLAGS_NONE, &metadata, image);
        if (FAILED(hr)) {
            hr = LoadFromWICMemory(data, size, WIC_FLAGS_NONE/*WIC_FLAGS_FORCE_RGB*/, &metadata, image);
        }
        if (FAILED(hr)) {
            return res;
        }

        CreateTexture(device->GetDevice().Get(), metadata, &res.resource);

        ComPtr<ID3D12Resource> srcBuffer;
        std::vector<D3D12_SUBRESOURCE_DATA> subresources;
        PrepareUpload(device->GetDevice().Get(), image.GetImages(), image.GetImageCount(), metadata, subresources);
        const auto totalBytes = GetRequiredIntermediateSize(res.resource.Get(), 0, UINT(subresources.size()));
        
        auto staging = device->CreateBuffer(
            totalBytes, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_HEAP_TYPE_UPLOAD);
        staging->SetName(L"Tex-Staging");

        auto command = device->CreateCommandList();
        UpdateSubresources(
            command.Get(),
            res.resource.Get(),
            staging.Get(),
            0, 0, UINT(subresources.size()), subresources.data());
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(res.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        command->ResourceBarrier(1, &barrier);
        command->Close();

        // 転送開始.
        device->ExecuteCommandList(command);

        // シェーダーリソースビューの作成.
        res.srv = device->AllocateDescriptor();
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = metadata.format;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        if (metadata.IsCubemap()) {
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
            srvDesc.TextureCube.MipLevels = UINT(metadata.mipLevels);
            srvDesc.TextureCube.MostDetailedMip = 0;
            srvDesc.TextureCube.ResourceMinLODClamp = 0;
        }
        else {
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.ResourceMinLODClamp = 0;
        }
        device->GetDevice()->CreateShaderResourceView(res.resource.Get(), &srvDesc, res.srv.hCpu);

        // 処理の完了を待つ.
        device->WaitForIdleGpu();

        return res;
    }

    TextureResource LoadTextureFromFile(
        const std::wstring& fileName,
        std::unique_ptr<dx12::GraphicsDevice>& device)
    {
        std::ifstream infile(fileName, std::ios::binary);
        if (!infile) {
            throw std::runtime_error("Cannot load texture file.");
        }
        std::vector<char> buf;
        buf.resize(infile.seekg(0, std::ios::end).tellg());
        infile.seekg(0, std::ios::beg).read(buf.data(), buf.size());
        return LoadTextureFromMemory(buf.data(), buf.size(), device);
    }

}

