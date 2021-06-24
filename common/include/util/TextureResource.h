#pragma once
#include <wrl.h>
#include <d3d12.h>
#include "GraphicsDevice.h"

#include <memory>

namespace util {
    
    struct TextureResource {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        dx12::Descriptor srv;
    };
    
    TextureResource LoadTextureFromMemory(
        const void* data, UINT64 size, 
        std::unique_ptr<dx12::GraphicsDevice>& device);

    TextureResource LoadTextureFromFile(
        const std::wstring& fileName, 
        std::unique_ptr<dx12::GraphicsDevice>& device);
}
