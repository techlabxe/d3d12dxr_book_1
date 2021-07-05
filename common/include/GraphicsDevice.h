#pragma once

#include <d3d12.h>
#include <dxgi1_5.h>

#include <wrl.h>
#include <string>
#include <memory>
#include <vector>
#include <list>
#include <array>
#include <unordered_map>


namespace dx12
{
    struct Descriptor
    {
        UINT heapBaseOffset;
        D3D12_CPU_DESCRIPTOR_HANDLE hCpu;
        D3D12_GPU_DESCRIPTOR_HANDLE hGpu;
        D3D12_DESCRIPTOR_HEAP_TYPE type;
        Descriptor() : heapBaseOffset(0), hCpu(), hGpu(), type(D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES) { }
        bool IsInvalid() const { 
            return type == D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
        }
    };

    class DescriptorHeapManager {
    public:
        template<class T>
        using ComPtr = Microsoft::WRL::ComPtr<T>;

        DescriptorHeapManager() = default;
        DescriptorHeapManager(const DescriptorHeapManager&) = delete;
        DescriptorHeapManager& operator=(const DescriptorHeapManager&) = delete;
        void Init(ComPtr<ID3D12DescriptorHeap> heap, UINT incSize) { m_heap = heap; m_incrementSize = incSize; }
        
        // ディスクリプタを確保する.
        void Allocate(Descriptor* desc);

        // ディスクリプタの解放処理.
        void Deallocate(Descriptor* desc);

        // 指定した個数が連続保証ありで確保する.
        // 返却されるのは先頭ディスクリプタのみ.
        void AllocateTable(UINT count, Descriptor* descs);

        // 連続保証ありで確保したものの解放処理.
        void DeallocateTable(UINT count, Descriptor* descs);

        ComPtr<ID3D12DescriptorHeap> GetHeap() { return m_heap; }
    private:
        UINT m_allocateIndex = 0;
        UINT m_incrementSize = 0;
        ComPtr<ID3D12DescriptorHeap> m_heap;

        using DescriptorList = std::list<Descriptor>;
        std::unordered_map<UINT, DescriptorList> m_freeMap;
    };

    class GraphicsDevice {
    public:
        template<class T>
        using ComPtr = Microsoft::WRL::ComPtr<T>;

        /* Constants */
        static const UINT BackBufferCount = 3;
        static const UINT RenderTargetViewMax = 64;
        static const UINT DepthStencilViewMax = 64;
        static const UINT ShaderResourceViewMax = 1024;

        GraphicsDevice();
        GraphicsDevice(const GraphicsDevice&) = delete;
        GraphicsDevice& operator=(const GraphicsDevice&) = delete;

        ~GraphicsDevice();

        bool OnInit();
        void OnDestroy();

        bool CreateSwapchain(UINT width, UINT height, HWND hwnd);

        D3D12_HEAP_PROPERTIES GetDefaultHeapProps() const {
            return m_defaultHeapProps;
        }

        D3D12_HEAP_PROPERTIES GetUploadHeapProps() const {
            return m_uploadHeapProps;
        }

        ComPtr<ID3D12CommandAllocator> GetCurrentCommandAllocator() {
            return m_commandAllocators[m_frameIndex];
        }
        UINT GetCurrentFrameIndex() const { return m_frameIndex; }
        
        ComPtr<ID3D12GraphicsCommandList4> CreateCommandList();
        ComPtr<ID3D12Fence1> CreateFence();

        D3D12_CPU_DESCRIPTOR_HANDLE GetRenderTargetView();
        D3D12_CPU_DESCRIPTOR_HANDLE GetDepthStencilView();
        ComPtr<ID3D12Resource> GetRenderTarget();
        ComPtr<ID3D12Resource> GetDepthStencil();
        const D3D12_VIEWPORT& GetDefaultViewport() const { return m_viewport; }

        ComPtr<ID3D12Device5> GetDevice() { return m_d3d12Device; }


        void ExecuteCommandList(ComPtr<ID3D12GraphicsCommandList4> command);
        void Present(UINT interval);

        void WaitForIdleGpu();

        ComPtr<ID3D12Resource> CreateBuffer(size_t size, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initialState, D3D12_HEAP_TYPE heapType, const wchar_t* name = nullptr);
        ComPtr<ID3D12Resource> CreateTexture2D(UINT width, UINT height, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initialState, D3D12_HEAP_TYPE heapType);
        
        dx12::Descriptor CreateShaderResourceView(ComPtr<ID3D12Resource> resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc);
        dx12::Descriptor CreateUnorderedAccessView(ComPtr<ID3D12Resource> resource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* uavDesc);

        // CPU から書き込み可能なリソースに書き込みをします.
        void WriteToHostVisibleMemory(ComPtr<ID3D12Resource> resource, const void* pData, size_t dataSize);

        // コピー用にステージングバッファを生成して、対象リソースにコピーします.
        void WriteToDefaultMemory(ComPtr<ID3D12Resource> resource, const void* pData, size_t dataSize);

        // ディスクリプタの確保.
        Descriptor AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        // ディスクリプタの解放.
        void DeallocateDescriptor(Descriptor& descriptor);

        ComPtr<ID3D12DescriptorHeap> GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE type);

        // 使用中のアダプタ名を取得.
        std::string GetAdapterName() const {
            return m_adapterName;
        }
        ComPtr<ID3D12CommandQueue> GetDefaultQueue() const {
            return m_commandQueue;
        }
    private:
        void WaitAvailableFrame();

        ComPtr<ID3D12Device5> m_d3d12Device;
        ComPtr<ID3D12CommandQueue> m_commandQueue;
        ComPtr<IDXGISwapChain3> m_swapchain;

        DescriptorHeapManager m_rtvHeap;
        DescriptorHeapManager m_dsvHeap;
        DescriptorHeapManager m_heap; // CBV/SRV/UAV など用.

        std::array<ComPtr<ID3D12CommandAllocator>, BackBufferCount> m_commandAllocators;
        std::array<ComPtr<ID3D12Fence1>, BackBufferCount> m_frameFences;
        std::array<UINT64, BackBufferCount> m_fenceValues;
        
        HANDLE m_fenceEvent = 0;
        HANDLE m_waitEvent = 0;

        UINT m_frameIndex = 0;

        DXGI_FORMAT m_backbufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        DXGI_FORMAT m_depthFormat = DXGI_FORMAT_D32_FLOAT;

        std::array<ComPtr<ID3D12Resource>, BackBufferCount> m_renderTargets;
        std::array<Descriptor, BackBufferCount> m_renderTargetDescriptors;
        ComPtr<ID3D12Resource> m_depthBuffer;
        Descriptor m_dsvDescriptor;

        UINT m_width;
        UINT m_height;
        D3D12_VIEWPORT	m_viewport;
        D3D12_RECT	m_scissorRect;

        D3D12_HEAP_PROPERTIES m_defaultHeapProps;
        D3D12_HEAP_PROPERTIES m_uploadHeapProps;

        std::string m_adapterName;
    };
} // dx12
