#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include <memory>

#include "../GraphicsDevice.h"

namespace util
{
    using Microsoft::WRL::ComPtr;

    class CommandAllocator {
        using Device = std::unique_ptr<dx12::GraphicsDevice>;
    public:
        CommandAllocator(UINT backbufferCount, Device& device);

        ComPtr<ID3D12GraphicsCommandList4> CreateCommandList(Device& device) {
            auto allocator = GetCurrent();
            ComPtr<ID3D12GraphicsCommandList4> commandList;
            device->GetDevice()->CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                allocator.Get(),
                nullptr,
                IID_PPV_ARGS(&commandList)
            );
            return commandList;
        }

        void Reset() {
            GetCurrent()->Reset();
        }

        void ChangeAllocator() {
            m_currentIndex = (++m_currentIndex) % m_bufferCount;
        }
    private:
        using D3D12CommandAllocator = ComPtr<ID3D12CommandAllocator>;
        D3D12CommandAllocator GetCurrent() {
            return m_allocators[m_currentIndex];
        }
        std::vector<D3D12CommandAllocator> m_allocators;
        UINT m_currentIndex = 0;
        UINT m_bufferCount = 0;
    };
}