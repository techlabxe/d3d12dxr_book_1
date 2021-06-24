#pragma once

#include <exception>
#include <stdexcept>
#include <functional>
#include <fstream>
#include <vector>

#include "GraphicsDevice.h"

#include <DirectXMath.h>

class DxrBookFramework {
public:
    template<class T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    using XMFLOAT2 = DirectX::XMFLOAT2;
    using XMFLOAT3 = DirectX::XMFLOAT3;
    using XMFLOAT4 = DirectX::XMFLOAT4;
    using XMFLOAT3X4 = DirectX::XMFLOAT3X4;
    using XMFLOAT4X4 = DirectX::XMFLOAT4X4;
    using XMVECTOR = DirectX::XMVECTOR;
    using XMMATRIX = DirectX::XMMATRIX;
    using XMUINT3 = DirectX::XMUINT3;
    using XMUINT4 = DirectX::XMUINT4;

    virtual void OnInit() = 0;
    virtual void OnDestroy() = 0;

    virtual void OnUpdate() = 0;
    virtual void OnRender() = 0;

    enum class MouseButton
    {
        LBUTTON,
        RBUTTON,
        MBUTTON,
    };
    virtual void OnMouseDown(MouseButton button, int x, int y) { }
    virtual void OnMouseUp(MouseButton button, int x, int y) { }
    virtual void OnMouseMove(int x, int y) {}

    DxrBookFramework(UINT width, UINT height, const std::wstring& title) : m_width(width), m_height(height), m_title(title) { }

    UINT GetWidth() const { return m_width; }
    UINT GetHeight() const { return m_height; }
    float GetAspect() const { return float(m_width) / float(m_height); }
    const wchar_t* GetTitle() const { return m_title.c_str(); }

protected:
    bool InitializeGraphicsDevice(HWND hwnd) {
        m_device = std::make_unique<dx12::GraphicsDevice>();
        if (!m_device->OnInit()) {
            return false;
        }
        if (!m_device->CreateSwapchain(GetWidth(), GetHeight(), hwnd)) {
            return false;
        }
        return true;
    }
    void TerminateGraphicsDevice() {
        if (m_device) {
            m_device->OnDestroy();
        }
        m_device.reset();
    }
    ComPtr<ID3D12RootSignature> CreateRootSignature(const D3D12_ROOT_SIGNATURE_DESC& rsDesc ) {
        ComPtr<ID3DBlob> blob, errBlob;
        HRESULT hr = D3D12SerializeRootSignature(
            &rsDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &blob, &errBlob);
        if (FAILED(hr)) {
            throw std::runtime_error("RootSignature failed.");
        }

        ComPtr<ID3D12RootSignature> rs;
        hr = m_device->GetDevice()->CreateRootSignature(
            0, blob->GetBufferPointer(), blob->GetBufferSize(),
            IID_PPV_ARGS(rs.ReleaseAndGetAddressOf())
        );
        if (FAILED(hr)) {
            throw std::runtime_error("RootSignature failed.");
        }
        return rs;
    }
    std::unique_ptr<dx12::GraphicsDevice> m_device;
private:
    UINT m_width;
    UINT m_height;
    std::wstring m_title;

};


