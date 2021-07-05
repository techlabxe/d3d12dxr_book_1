#include "GraphicsDevice.h"

#include <stdexcept>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

namespace dx12
{

    void DescriptorHeapManager::Allocate(Descriptor* desc) {
        auto it = m_freeMap.find(1);
        if (it != m_freeMap.end()) {
            if (!it->second.empty()) {
                *desc = it->second.front();
                it->second.pop_front();
                return;
            }
        }
        auto hCpu = m_heap->GetCPUDescriptorHandleForHeapStart();
        auto hGpu = m_heap->GetGPUDescriptorHandleForHeapStart();

        *desc = { };
        auto heapDesc = m_heap->GetDesc();
        if (m_allocateIndex < heapDesc.NumDescriptors) {
            auto offset = m_incrementSize * m_allocateIndex;
            hCpu.ptr += offset;
            hGpu.ptr += offset;
            (*desc).heapBaseOffset = offset;
            (*desc).hCpu = hCpu;
            (*desc).hGpu = hGpu;
            (*desc).type = heapDesc.Type;
            m_allocateIndex++;
        }
    }

    void DescriptorHeapManager::Deallocate(Descriptor* desc) {
        auto it = m_freeMap.find(1);
        if (it == m_freeMap.end()) {
            m_freeMap.insert(std::make_pair(1, DescriptorList()));
            it = m_freeMap.find(1);
        }
        it->second.push_front(*desc);
    }

    void DescriptorHeapManager::AllocateTable(UINT count, Descriptor* descs) {
        auto it = m_freeMap.find(count);
        if (it != m_freeMap.end()) {
            if (!it->second.empty()) {
                *descs = it->second.front();
                it->second.pop_front();
                return;
            }
        }

        auto hCpu = m_heap->GetCPUDescriptorHandleForHeapStart();
        auto hGpu = m_heap->GetGPUDescriptorHandleForHeapStart();
        auto heapDesc = m_heap->GetDesc();

        *descs = { };
        if (m_allocateIndex + count < heapDesc.NumDescriptors) {
            auto offset = m_incrementSize * m_allocateIndex;
            hCpu.ptr += offset;
            hGpu.ptr += offset;
            (*descs).heapBaseOffset = offset;
            (*descs).hCpu = hCpu;
            (*descs).hGpu = hGpu;
            (*descs).type = heapDesc.Type;
            m_allocateIndex += count;
        }
    }

    void DescriptorHeapManager::DeallocateTable(UINT count, Descriptor* descs) {
        auto it = m_freeMap.find(count);
        if (it == m_freeMap.end()) {
            m_freeMap.insert(std::make_pair(count, DescriptorList()));
            it = m_freeMap.find(count);
        }
        it->second.push_front(*descs);
    }

    GraphicsDevice::GraphicsDevice() : 
        m_width(0), m_height(0), m_frameFences(), m_fenceValues(), m_viewport(), m_scissorRect()
    {
        m_defaultHeapProps = D3D12_HEAP_PROPERTIES{
            D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 1, 1
        };
        m_uploadHeapProps = D3D12_HEAP_PROPERTIES{
            D3D12_HEAP_TYPE_UPLOAD, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 1, 1
        };
    }
    GraphicsDevice::~GraphicsDevice() {
    }

    bool GraphicsDevice::OnInit()
    {
        HRESULT hr;
        UINT dxgiFlags = 0;
#if _DEBUG 
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
            debug->EnableDebugLayer();
            dxgiFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
#endif

        // DXGIFactory
        ComPtr<IDXGIFactory3> factory;
        hr = CreateDXGIFactory2(dxgiFlags, IID_PPV_ARGS(&factory));
        if (FAILED(hr)) {
            return false;
        }

        // ハードウェアアダプタの検索.
        ComPtr<IDXGIAdapter1> useAdapter;
        {
            UINT adapterIndex = 0;
            ComPtr<IDXGIAdapter1> adapter;
            while (DXGI_ERROR_NOT_FOUND != factory->EnumAdapters1(adapterIndex, &adapter)) {
                DXGI_ADAPTER_DESC1 desc1{};
                adapter->GetDesc1(&desc1);
                ++adapterIndex;
                if (desc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                    continue;
                }

                hr = D3D12CreateDevice(
                    adapter.Get(),
                    D3D_FEATURE_LEVEL_12_0,
                    __uuidof(ID3D12Device), nullptr);
                if (SUCCEEDED(hr)) {
                    break;
                }
            }
            adapter.As(&useAdapter);
        }

        hr = D3D12CreateDevice(useAdapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_d3d12Device));
        if (FAILED(hr)) {
            return false;
        }

        DXGI_ADAPTER_DESC1 adapterDesc{};
        useAdapter->GetDesc1(&adapterDesc);
        {
            std::vector<char> buf;
            auto ret = WideCharToMultiByte(CP_UTF8, 0, adapterDesc.Description, -1, nullptr, 0, nullptr, nullptr);
            if (ret > 0) {
                buf.resize(ret + 1);
                ret = WideCharToMultiByte(CP_UTF8, 0, adapterDesc.Description, -1, buf.data(), int(buf.size()) - 1, nullptr, nullptr);
                m_adapterName = std::string(buf.data());
            }
        }

        // DXR サポートしているか確認.
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 options{};
        hr = m_d3d12Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options, UINT(sizeof(options)));
        if (FAILED(hr) || options.RaytracingTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED) {
            throw std::runtime_error("DirectX Raytracing not supported.");
        }

        // コマンドキューを作成.
        D3D12_COMMAND_QUEUE_DESC queueDesc{
            D3D12_COMMAND_LIST_TYPE_DIRECT, 0, D3D12_COMMAND_QUEUE_FLAG_NONE, 0
        };
        hr = m_d3d12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue));
        if (FAILED(hr)) {
            return false;
        }

        // ディスクリプタヒープ作成(RTV)
        ComPtr<ID3D12DescriptorHeap> heap;
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV, RenderTargetViewMax, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0
        };
        hr = m_d3d12Device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(heap.ReleaseAndGetAddressOf()));
        if (FAILED(hr)) {
            return false;
        }
        m_rtvHeap.Init(heap, m_d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV));

        // ディスクリプタヒープ作成(DSV)
        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{
            D3D12_DESCRIPTOR_HEAP_TYPE_DSV, DepthStencilViewMax, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0
        };
        hr = m_d3d12Device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(heap.ReleaseAndGetAddressOf()));
        if (FAILED(hr)) {
            return false;
        }
        m_dsvHeap.Init(heap, m_d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV));

        // 汎用(SRV/CBV/UAVなど)のディスクリプタヒープ作成.
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, ShaderResourceViewMax, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0
        };
        hr = m_d3d12Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(heap.ReleaseAndGetAddressOf()));
        if (FAILED(hr)) {
            return false;
        }
        m_heap.Init(heap, m_d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));


        // コマンドアロケーター準備.
        for (UINT i = 0; i < BackBufferCount; ++i) {
            hr = m_d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(m_commandAllocators[i].ReleaseAndGetAddressOf()));
            if (FAILED(hr)) {
                return false;
            }
        }

        m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        m_waitEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);;
        return true;
    }
    void GraphicsDevice::OnDestroy()
    {
        WaitForIdleGpu();

        CloseHandle(m_fenceEvent); m_fenceEvent = 0;
        CloseHandle(m_waitEvent); m_waitEvent = 0;

        for (UINT i = 0; i < BackBufferCount; ++i) {
            m_rtvHeap.Deallocate(&m_renderTargetDescriptors[i]);
        }
        m_dsvHeap.Deallocate(&m_dsvDescriptor);
    }

    bool GraphicsDevice::CreateSwapchain(UINT width, UINT height, HWND hwnd)
    {
        HRESULT hr;
        m_width = width;
        m_height = height;

        if (!m_swapchain)
        {
            ComPtr<IDXGIFactory3> factory;
            hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
            if (FAILED(hr)) {
                return false;
            }

            DXGI_SWAP_CHAIN_DESC1 scDesc{};
            scDesc.BufferCount = BackBufferCount;
            scDesc.Width = width;
            scDesc.Height = height;
            scDesc.Format = m_backbufferFormat;
            scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            scDesc.SampleDesc.Count = 1;

            ComPtr<IDXGISwapChain1> swapchain;
            hr = factory->CreateSwapChainForHwnd(
                m_commandQueue.Get(),
                hwnd,
                &scDesc,
                nullptr,
                nullptr,
                &swapchain
            );
            if (FAILED(hr)) {
                return false;
            }
            swapchain.As(&m_swapchain);

            for (UINT i = 0; i < BackBufferCount; ++i) {
                m_fenceValues[i] = 0;
                hr = m_d3d12Device->CreateFence(m_fenceValues[i], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_frameFences[i].ReleaseAndGetAddressOf()));
                if (FAILED(hr)) {
                    return false;
                }
            }
        }
        else
        {
            // 再生成時の処理.
            // TODO: 可変ウィンドウサイズをサポートするときには実装する.
        }

        for (UINT i = 0; i < BackBufferCount; ++i) {
            m_swapchain->GetBuffer(i, IID_PPV_ARGS(m_renderTargets[i].ReleaseAndGetAddressOf()));
            m_renderTargets[i]->SetName(L"SwapchainFrame");

            D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
            rtvDesc.Format = m_backbufferFormat;
            rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

            m_rtvHeap.Allocate(&m_renderTargetDescriptors[i]);
            auto rtvHandle = m_renderTargetDescriptors[i].hCpu;
            m_d3d12Device->CreateRenderTargetView(m_renderTargets[i].Get(), &rtvDesc, rtvHandle);
        }

        {
            D3D12_RESOURCE_DESC depthDesc{};
            depthDesc.Format = m_depthFormat;
            depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            depthDesc.Width = width;
            depthDesc.Height = height;
            depthDesc.DepthOrArraySize = 1;
            depthDesc.MipLevels = 1;
            depthDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
            depthDesc.SampleDesc.Count = 1;

            D3D12_CLEAR_VALUE clearValue{};
            clearValue.Format = m_depthFormat;
            clearValue.DepthStencil.Depth = 1.0f;

            hr = m_d3d12Device->CreateCommittedResource(
                &m_defaultHeapProps,
                D3D12_HEAP_FLAG_NONE,
                &depthDesc,
                D3D12_RESOURCE_STATE_DEPTH_WRITE,
                &clearValue,
                IID_PPV_ARGS(m_depthBuffer.ReleaseAndGetAddressOf()));
            if (FAILED(hr)) {
                return false;
            }
            m_depthBuffer->SetName(L"DepthBuffer(default)");
        }
        m_frameIndex = m_swapchain->GetCurrentBackBufferIndex();

        m_viewport.TopLeftX = 0.0f;
        m_viewport.TopLeftY = 0.0f;
        m_viewport.Width = float(width);
        m_viewport.Height = float(height);
        m_viewport.MinDepth = 0.0f;
        m_viewport.MaxDepth = 1.0f;

        m_scissorRect.left = 0;
        m_scissorRect.right = width;
        m_scissorRect.top = 0;
        m_scissorRect.bottom = height;

        return true;
    }

    GraphicsDevice::ComPtr<ID3D12GraphicsCommandList4> GraphicsDevice::CreateCommandList() {
        ComPtr<ID3D12GraphicsCommandList4> commandList;
        auto allocator = GetCurrentCommandAllocator();
        m_d3d12Device->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(commandList.ReleaseAndGetAddressOf())
        );
        return commandList;
    }
    GraphicsDevice::ComPtr<ID3D12Fence1> GraphicsDevice::CreateFence() {
        ComPtr<ID3D12Fence1> fence;
        m_d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.ReleaseAndGetAddressOf()));
        return fence;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GraphicsDevice::GetRenderTargetView() {
        return m_renderTargetDescriptors[m_frameIndex].hCpu;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GraphicsDevice::GetDepthStencilView() {
        return m_dsvDescriptor.hCpu;
    }

    GraphicsDevice::ComPtr<ID3D12Resource> GraphicsDevice::GetRenderTarget() {
        return m_renderTargets[m_frameIndex];
    }

    GraphicsDevice::ComPtr<ID3D12Resource> GraphicsDevice::GetDepthStencil() {
        return m_depthBuffer;
    }

    void GraphicsDevice::ExecuteCommandList(ComPtr<ID3D12GraphicsCommandList4> command)
    {
        ID3D12CommandList* commandLists[] = {
            command.Get(),
        };
        m_commandQueue->ExecuteCommandLists(1, commandLists);
    }

    void GraphicsDevice::Present(UINT interval) {
        if (m_swapchain) {
            m_swapchain->Present(interval, 0);
            WaitAvailableFrame();
        }
    }

    void GraphicsDevice::WaitForIdleGpu() {
        if (m_commandQueue) {
            auto commandList = CreateCommandList();
            auto waitFence = CreateFence();
            UINT64 fenceValue = 1;
            waitFence->SetEventOnCompletion(fenceValue, m_waitEvent);
            m_commandQueue->Signal(waitFence.Get(), fenceValue);
            WaitForSingleObject(m_waitEvent, INFINITE);
        }
    }

    GraphicsDevice::ComPtr<ID3D12Resource> GraphicsDevice::CreateBuffer(size_t size, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initialState, D3D12_HEAP_TYPE heapType, const wchar_t* name) {
        D3D12_HEAP_PROPERTIES heapProps{};
        if (heapType == D3D12_HEAP_TYPE_DEFAULT) {
            heapProps = GetDefaultHeapProps();
        }
        if (heapType == D3D12_HEAP_TYPE_UPLOAD) {
            heapProps = GetUploadHeapProps();
        }
        HRESULT hr;
        ComPtr<ID3D12Resource> resource;
        D3D12_RESOURCE_DESC resDesc{};
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resDesc.Alignment = 0;
        resDesc.Width = size;
        resDesc.Height = 1;
        resDesc.DepthOrArraySize = 1;
        resDesc.MipLevels = 1;
        resDesc.Format = DXGI_FORMAT_UNKNOWN;
        resDesc.SampleDesc = { 1, 0 };
        resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resDesc.Flags = flags;

        hr = m_d3d12Device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &resDesc,
            initialState,
            nullptr,
            IID_PPV_ARGS(resource.ReleaseAndGetAddressOf())
        );
        if (FAILED(hr)) {
            OutputDebugStringA("CreateBuffer failed.\n");
        }
        if (resource != nullptr && name != nullptr) {
            resource->SetName(name);
        }
        return resource;
    }

    GraphicsDevice::ComPtr<ID3D12Resource> GraphicsDevice::CreateTexture2D(UINT width, UINT height, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initialState, D3D12_HEAP_TYPE heapType) {
        D3D12_HEAP_PROPERTIES heapProps{};
        if (heapType == D3D12_HEAP_TYPE_DEFAULT) {
            heapProps = GetDefaultHeapProps();
        }
        if (heapType == D3D12_HEAP_TYPE_UPLOAD) {
            heapProps = GetUploadHeapProps();
        }
        HRESULT hr;
        ComPtr<ID3D12Resource> resource;
        D3D12_RESOURCE_DESC resDesc{};
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        resDesc.Alignment = 0;
        resDesc.Width = width;
        resDesc.Height = height;
        resDesc.DepthOrArraySize = 1;
        resDesc.MipLevels = 1;
        resDesc.Format = format;
        resDesc.SampleDesc = { 1, 0 };
        resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        resDesc.Flags = flags;

        hr = m_d3d12Device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &resDesc,
            initialState,
            nullptr,
            IID_PPV_ARGS(resource.ReleaseAndGetAddressOf())
        );
        if (FAILED(hr)) {
            OutputDebugStringA("CreateTexture2D failed.\n");
        }
        return resource;
    }

    dx12::Descriptor dx12::GraphicsDevice::CreateShaderResourceView(ComPtr<ID3D12Resource> resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc)
    {
        auto descriptor = AllocateDescriptor();
        m_d3d12Device->CreateShaderResourceView(
            resource.Get(),
            srvDesc,
            descriptor.hCpu);
        return descriptor;
    }
    dx12::Descriptor dx12::GraphicsDevice::CreateUnorderedAccessView(ComPtr<ID3D12Resource> resource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* uavDesc)
    {
        auto descriptor = AllocateDescriptor();
        m_d3d12Device->CreateUnorderedAccessView(
            resource.Get(),
            nullptr,
            uavDesc,
            descriptor.hCpu);
        return descriptor;
    }



    void GraphicsDevice::WriteToHostVisibleMemory(ComPtr<ID3D12Resource> resource, const void* pData, size_t dataSize) {
        if (resource == nullptr) {
            return;
        }
        void* mapped = nullptr;
        D3D12_RANGE range{ 0, dataSize };
        HRESULT hr = resource->Map(0, &range, &mapped);
        if (SUCCEEDED(hr)) {
            memcpy(mapped, pData, dataSize);
            resource->Unmap(0, &range);
        }
    }

    void GraphicsDevice::WriteToDefaultMemory(ComPtr<ID3D12Resource> resource, const void* pData, size_t dataSize) {
        if (resource == nullptr) {
            return;
        }
        ComPtr<ID3D12Resource> staging;
        HRESULT hr;
        auto heapProps = GetUploadHeapProps();
        D3D12_RESOURCE_DESC resDesc{};
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resDesc.Width = dataSize;
        resDesc.Height = 1;
        resDesc.DepthOrArraySize = 1;
        resDesc.MipLevels = 1;
        resDesc.SampleDesc = { 1, 0 };
        resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        hr = m_d3d12Device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &resDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(staging.ReleaseAndGetAddressOf())
        );
        if (FAILED(hr)) {
            return;
        }
        void* mapped = nullptr;
        D3D12_RANGE range{ 0, dataSize };
        hr = staging->Map(0, &range, &mapped);
        if (SUCCEEDED(hr)) {
            memcpy(mapped, pData, dataSize);
            staging->Unmap(0, &range);
        }

        auto command = CreateCommandList();
        ComPtr<ID3D12Fence> fence = CreateFence();
        command->CopyResource(resource.Get(), staging.Get());
        command->Close();

        ExecuteCommandList(command);

        WaitForIdleGpu();
    }


    Descriptor GraphicsDevice::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE type) {
        dx12::Descriptor descriptor;
        if (type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) {
            m_heap.Allocate(&descriptor);
        }
        if (type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER) {
        }
        if (type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV) {
            m_rtvHeap.Allocate(&descriptor);
        }
        if (type == D3D12_DESCRIPTOR_HEAP_TYPE_DSV) {
            m_dsvHeap.Allocate(&descriptor);
        }
        return descriptor;
    }
    void GraphicsDevice::DeallocateDescriptor(Descriptor& descriptor) {
        auto type = descriptor.type;
        if (type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) {
            m_heap.Deallocate(&descriptor);
        }
        if (type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER) {
        }
        if (type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV) {
            m_rtvHeap.Deallocate(&descriptor);
        }
        if (type == D3D12_DESCRIPTOR_HEAP_TYPE_DSV) {
            m_dsvHeap.Deallocate(&descriptor);
        }
    }
    GraphicsDevice::ComPtr<ID3D12DescriptorHeap> GraphicsDevice::GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE type) {
        if (type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) {
            return m_heap.GetHeap();
        }
        if (type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER) {

        }
        if (type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV) {
            return m_rtvHeap.GetHeap();
        }
        if (type == D3D12_DESCRIPTOR_HEAP_TYPE_DSV) {
            return m_dsvHeap.GetHeap();
        }
        return nullptr;
    }

    void GraphicsDevice::WaitAvailableFrame() {
        auto fence = m_frameFences[m_frameIndex];
        auto value = ++m_fenceValues[m_frameIndex];
        m_commandQueue->Signal(fence.Get(), value);

        m_frameIndex = m_swapchain->GetCurrentBackBufferIndex();
        fence = m_frameFences[m_frameIndex];
        auto finishValue = m_fenceValues[m_frameIndex];
        if (fence->GetCompletedValue() < finishValue) {
            fence->SetEventOnCompletion(finishValue, m_fenceEvent);
            WaitForSingleObject(m_fenceEvent, INFINITE);
        }
    }
}
