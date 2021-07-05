#include "MaterialScene.h"

#include "Win32Application.h"

#include <fstream>
#include <random>
#include <DirectXTex.h>
#include "d3dx12.h"
#include "imgui.h"
#include "backends/imgui_impl_dx12.h"
#include "backends/imgui_impl_win32.h"

#include <wincodec.h>
#include "util/DxrBookUtility.h"

namespace {
    using namespace DirectX;
    XMVECTOR colorTable[] = {
        XMVectorSet(1.0f, 1.0f, 1.0f, 0.0f),
        XMVectorSet(0.5f, 0.8f, 0.4f, 0.0f),
        XMVectorSet(0.7f, 0.6f, 0.2f, 0.0f),
        XMVectorSet(0.2f, 0.3f, 0.6f, 0.0f),
        XMVectorSet(0.1f, 0.8f, 0.9f, 0.0f),
    };
}

MaterialScene::MaterialScene(UINT width, UINT height) : DxrBookFramework(width, height, L"MaterialScene"),
m_meshPlane(), m_meshSphere(), m_dispatchRayDesc(), m_sceneParam(),
m_spheresReflect(), m_spheresRefract(), m_spheresNormal(),
m_normalSphereMaterials()
{
}

void MaterialScene::OnInit()
{
    if (!InitializeGraphicsDevice(Win32Application::GetHWND()))
    {
        throw std::runtime_error("Failed Initialize GraphicsDevice.");
    }
    // WIC を用いるため初期化.
    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    // シーンに配置するオブジェクトの生成.
    CreateSceneObjects();

    // 床の BLAS を構築する.
    CreateSceneBLAS();

    CreateSceneTLAS();

    // グローバル Root Signature を用意.
    CreateRootSignatureGlobal();

    // ローカル Root Signature を用意.
    CreateLocalRootSignatureRayGen();
    CreateSphereLocalRootSignature();
    CreateFloorLocalRootSignature();

    // コンパイル済みシェーダーよりステートオブジェクトを用意.
    CreateStateObject();

    // シーン用コンスタントバッファの確保.
    m_sceneCB.Initialize(m_device, sizeof(SceneParam), L"SceneCB");

    // レイトレーシング結果格納のためのバッファ(UAV)を用意.
    CreateResultBuffer();

    // テクスチャの用意.
    m_background = LoadTextureFromFile(L"yokohama2_cube.dds");
    m_groundTex = LoadTextureFromFile(L"trianglify-lowres.png");

    // 描画で使用する Shader Table を用意.
    CreateShaderTable();

    // コマンドリスト用意.
    //  描画時に積むのでここではクローズしておく.
    m_commandList = m_device->CreateCommandList();
    m_commandList->Close();

    // ImGui 向け DX12 設定.
    auto heap = m_device->GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto descriptorForImGui = m_device->AllocateDescriptor();
    ImGui_ImplDX12_Init(
        m_device->GetDevice().Get(),
        dx12::GraphicsDevice::BackBufferCount,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        heap.Get(),    
        descriptorForImGui.hCpu, descriptorForImGui.hGpu);

    XMFLOAT3 eyePos(6.0f, 4.0f, 20.0f);
    eyePos = XMFLOAT3(-0.77f, 0.95f, -4.16f);
    eyePos= XMFLOAT3( -0.09f, 1.51f, 6.90f);
    XMFLOAT3 target(0.0f, 0.0f, 0.0f);
    m_camera.SetLookAt(eyePos, target);

    m_camera.SetPerspective(
        XM_PIDIV4, GetAspect(), 0.1f, 100.0f
    );
}

void MaterialScene::OnDestroy()
{
    ImGui_ImplDX12_Shutdown();
    TerminateGraphicsDevice();
}

void MaterialScene::OnUpdate()
{
    XMFLOAT3 lightDir{ -0.1f,-1.0f, -0.15f }; // ワールド座標系での光源の向き.

    m_sceneParam.mtxView = m_camera.GetViewMatrix();
    m_sceneParam.mtxProj = m_camera.GetProjectionMatrix();
    m_sceneParam.mtxViewInv = XMMatrixInverse(nullptr, m_sceneParam.mtxView);
    m_sceneParam.mtxProjInv = XMMatrixInverse(nullptr, m_sceneParam.mtxProj);

    m_sceneParam.lightColor = XMVectorSet(1.0f, 1.0f, 1.0f, 0.0f);
    m_sceneParam.lightDirection = XMVector3Normalize(XMLoadFloat3(&lightDir));
    m_sceneParam.ambientColor = XMVectorSet(0.2f, 0.2f, 0.2f, 0.0f);
    m_sceneParam.eyePosition = m_camera.GetPosition();

    UpdateHUD();
}

void MaterialScene::OnRender()
{
    auto device = m_device->GetDevice();
    auto renderTarget = m_device->GetRenderTarget();
    auto allocator = m_device->GetCurrentCommandAllocator();
    allocator->Reset();
    m_commandList->Reset(allocator.Get(), nullptr);
    auto frameIndex = m_device->GetCurrentFrameIndex();

    auto sceneConstantBuffer = m_sceneCB.Get(frameIndex);
    void* mapped = nullptr;
    sceneConstantBuffer->Map(0, nullptr, &mapped);
    if (mapped) {
        memcpy(mapped, &m_sceneParam, sizeof(m_sceneParam));
        sceneConstantBuffer->Unmap(0, nullptr);
    }

    ID3D12DescriptorHeap* descriptorHeaps[] = {
        m_device->GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV).Get(),
    };
    m_commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
    m_commandList->SetComputeRootSignature(m_rootSignatureGlobal.Get());
    m_commandList->SetComputeRootDescriptorTable(0, m_tlasDescriptor.hGpu);
    m_commandList->SetComputeRootConstantBufferView(1, sceneConstantBuffer->GetGPUVirtualAddress());
    m_commandList->SetComputeRootDescriptorTable(2, m_background.srv.hGpu);
    

    // レイトレーシング結果バッファを UAV 状態へ.
    auto barrierToUAV = CD3DX12_RESOURCE_BARRIER::Transition(
        m_dxrOutput.Get(),
        D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS
    );
    m_commandList->ResourceBarrier(1, &barrierToUAV);

    m_commandList->SetPipelineState1(m_rtState.Get());
    m_commandList->DispatchRays(&m_dispatchRayDesc);

    // レイトレーシング結果をバックバッファへコピーする.
    // バリアを設定し各リソースの状態を遷移させる.
    D3D12_RESOURCE_BARRIER barriers[] = {
        CD3DX12_RESOURCE_BARRIER::Transition(
            m_dxrOutput.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
        CD3DX12_RESOURCE_BARRIER::Transition(
            renderTarget.Get(),
            D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST),
    };
    m_commandList->ResourceBarrier(_countof(barriers), barriers);
    m_commandList->CopyResource(renderTarget.Get(), m_dxrOutput.Get());

    // バックバッファをレンダーターゲット状態にして UI を書き込めるようにする.
    auto barrierToRT = CD3DX12_RESOURCE_BARRIER::Transition(
        renderTarget.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET
    );
    m_commandList->ResourceBarrier(1, &barrierToRT);
    auto rtv = m_device->GetRenderTargetView();
    auto viewport = m_device->GetDefaultViewport();
    m_commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    m_commandList->RSSetViewports(1, &viewport);
    
    RenderHUD();

    // Present 可能なようにバリアをセット.
    auto barrierToPresent = CD3DX12_RESOURCE_BARRIER::Transition(
        renderTarget.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT
    );
    m_commandList->ResourceBarrier(1, &barrierToPresent);

    m_commandList->Close();

    m_device->ExecuteCommandList(m_commandList);

    m_device->Present(1);
}

void MaterialScene::UpdateHUD()
{
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    auto framerate = ImGui::GetIO().Framerate;
    ImGui::Begin("Information");
    ImGui::Text("GPU: %s", m_device->GetAdapterName().c_str());
    ImGui::Text("Framerate %.3f ms", 1000.0f / framerate);

    XMFLOAT3 camPos;
    XMStoreFloat3(&camPos, m_camera.GetPosition());
    ImGui::Text("CameraPos (%.2f, %.2f, %.2f)", camPos.x, camPos.y, camPos.z);
    ImGui::End();
}

void MaterialScene::RenderHUD()
{
    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_commandList.Get());
}

void MaterialScene::DeployObjects(std::vector<D3D12_RAYTRACING_INSTANCE_DESC>& instanceDescs)
{
    D3D12_RAYTRACING_INSTANCE_DESC templateDesc{};
    templateDesc.InstanceMask = 0xFF;
    templateDesc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;

    // 床を配置.
    {
        D3D12_RAYTRACING_INSTANCE_DESC desc{};
        XMStoreFloat3x4(
            reinterpret_cast<XMFLOAT3X4*>(&desc.Transform), XMMatrixIdentity());
        desc.InstanceID = 0;
        desc.InstanceMask = 0xFF;
        desc.InstanceContributionToHitGroupIndex = 0;
        desc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
        desc.AccelerationStructure = m_meshPlane.blas->GetGPUVirtualAddress();
        instanceDescs.push_back(desc);
    }

    // スフィアを配置 (反射).
    int instanceID = 0; // ここを4 にするとキューブマップからのフェッチになる.
    //instanceID = 4;
    for (const auto& spherePos : m_spheresReflect) {
        D3D12_RAYTRACING_INSTANCE_DESC desc{};
        XMStoreFloat3x4(
            reinterpret_cast<XMFLOAT3X4*>(&desc.Transform), spherePos);
        desc.InstanceID = instanceID;
        desc.InstanceMask = 0xFF;
        desc.InstanceContributionToHitGroupIndex = 1;
        desc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
        desc.AccelerationStructure = m_meshSphere.blas->GetGPUVirtualAddress();
        instanceDescs.push_back(desc);
    }

    // スフィアを配置 (屈折).
    instanceID = 1;
    for (const auto& spherePos : m_spheresRefract) {
        D3D12_RAYTRACING_INSTANCE_DESC desc{};
        XMStoreFloat3x4(
            reinterpret_cast<XMFLOAT3X4*>(&desc.Transform), spherePos);
        desc.InstanceID = instanceID;
        desc.InstanceMask = 0xFF;
        desc.InstanceContributionToHitGroupIndex = 1;
        desc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
        desc.AccelerationStructure = m_meshSphere.blas->GetGPUVirtualAddress();
        instanceDescs.push_back(desc);
    }

    // Lambert 描画スフィアを配置.
    instanceID = 2;
    auto entryOffset = 2; // 別のシェーダーテーブルを使う.
    for (const auto& spherePos : m_spheresNormal) {
        D3D12_RAYTRACING_INSTANCE_DESC desc{};
        XMStoreFloat3x4(
            reinterpret_cast<XMFLOAT3X4*>(&desc.Transform), spherePos);
        desc.InstanceID = instanceID;
        desc.InstanceMask = 0xFF;
        desc.InstanceContributionToHitGroupIndex = entryOffset;
        desc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
        desc.AccelerationStructure = m_meshSphere.blas->GetGPUVirtualAddress();
        instanceDescs.push_back(desc);
        entryOffset++;
    }
}


MaterialScene::TextureResource MaterialScene::LoadTextureFromFile(const std::wstring& fileName)
{
    DirectX::TexMetadata metadata;
    DirectX::ScratchImage image;

    HRESULT hr = E_FAIL;
    const std::wstring extDDS(L"dds");
    const std::wstring extPNG(L"png");
    if (fileName.length() < 3) {
        throw std::runtime_error("texture filename is invalid.");
    }
    
    if (std::equal(std::rbegin(extDDS), std::rend(extDDS), std::rbegin(fileName))) {
        hr = LoadFromDDSFile(fileName.c_str(), DDS_FLAGS_NONE, &metadata, image);
    }
    if (std::equal(std::rbegin(extPNG), std::rend(extPNG), std::rbegin(fileName))) {
        hr = LoadFromWICFile(fileName.c_str(), WIC_FLAGS_NONE, &metadata, image);
    }
    

    ComPtr<ID3D12Resource> texRes;
    ComPtr<ID3D12Device> device;
    m_device->GetDevice().As(&device);
    CreateTexture(device.Get(), metadata, &texRes);
    texRes->SetName(fileName.c_str());

    ComPtr<ID3D12Resource> srcBuffer;
    std::vector<D3D12_SUBRESOURCE_DATA> subresources;
    PrepareUpload(device.Get(), image.GetImages(), image.GetImageCount(), metadata, subresources);
    const auto totalBytes = GetRequiredIntermediateSize(texRes.Get(), 0, UINT(subresources.size()));

    auto staging = m_device->CreateBuffer(
        totalBytes, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_HEAP_TYPE_UPLOAD);
    staging->SetName(L"Tex-Staging");


    auto command = m_device->CreateCommandList();
    UpdateSubresources(
        command.Get(), texRes.Get(), staging.Get(), 0, 0, UINT(subresources.size()), subresources.data());
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(texRes.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    command->ResourceBarrier(1, &barrier);
    command->Close();

    // 転送開始と処理完了を待つ.
    m_device->ExecuteCommandList(command);
    m_device->WaitForIdleGpu();

    TextureResource ret;
    ret.res = texRes;
    ret.srv = m_device->AllocateDescriptor();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = metadata.format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    if (metadata.IsCubemap()) {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.TextureCube.MipLevels = UINT(metadata.mipLevels);
        srvDesc.TextureCube.MostDetailedMip = 0;
        srvDesc.TextureCube.ResourceMinLODClamp = 0;
    } else {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.ResourceMinLODClamp = 0;
    }
    m_device->GetDevice()->CreateShaderResourceView(texRes.Get(), &srvDesc, ret.srv.hCpu);

    return ret;
}

uint8_t* MaterialScene::WriteHitgroupFloor(uint8_t* dst, const PolygonMesh& mesh, UINT hitgroupRecordSize)
{
    ComPtr<ID3D12StateObjectProperties> rtsoProps;
    m_rtState.As(&rtsoProps);
    auto recordStart = dst;
    auto id = rtsoProps->GetShaderIdentifier(AppHitGroups::Floor);
    if (id == nullptr) {
        throw std::logic_error("Not found ShaderIdentifier");
    }
    dst += util::WriteShaderIdentifier(dst, id);
    dst += util::WriteGPUDescriptor(dst, mesh.descriptorIB);
    dst += util::WriteGPUDescriptor(dst, mesh.descriptorVB);
    dst += util::WriteGPUDescriptor(dst, m_groundTex.srv);

    dst = recordStart + hitgroupRecordSize;
    return dst;
}

uint8_t* MaterialScene::WriteHitgroupMaterial(uint8_t* dst, const PolygonMesh& mesh, UINT hitgroupRecordSize)
{
    ComPtr<ID3D12StateObjectProperties> rtsoProps;
    m_rtState.As(&rtsoProps);
    auto recordStart = dst;
    auto id = rtsoProps->GetShaderIdentifier(AppHitGroups::NoPhongMaterial);
    if (id == nullptr) {
        throw std::logic_error("Not found ShaderIdentifier");
    }
    dst += util::WriteShaderIdentifier(dst, id);
    dst += util::WriteGPUDescriptor(dst, mesh.descriptorIB);
    dst += util::WriteGPUDescriptor(dst, mesh.descriptorVB);

    dst = recordStart + hitgroupRecordSize;
    return dst;
}

uint8_t* MaterialScene::WriteHitgroupPhong(uint8_t* dst, const PolygonMesh& mesh, D3D12_GPU_VIRTUAL_ADDRESS address, UINT hitgroupRecordSize)
{
    ComPtr<ID3D12StateObjectProperties> rtsoProps;
    m_rtState.As(&rtsoProps);
    auto recordStart = dst;
    auto id = rtsoProps->GetShaderIdentifier(AppHitGroups::PhongMaterial);
    if (id == nullptr) {
        throw std::logic_error("Not found ShaderIdentifier");
    }
    dst += util::WriteShaderIdentifier(dst, id);
    dst += util::WriteGPUDescriptor(dst, mesh.descriptorIB);
    dst += util::WriteGPUDescriptor(dst, mesh.descriptorVB);
    dst += util::WriteGpuResourceAddr(dst, address);

    dst = recordStart + hitgroupRecordSize;
    return dst;
}

void MaterialScene::OnMouseDown(MouseButton button, int x, int y)
{
    float fdx = float(x) / GetWidth();
    float fdy = float(y) / GetHeight();
    m_camera.OnMouseButtonDown(int(button), fdx, fdy);
}

void MaterialScene::OnMouseUp(MouseButton button, int x, int y)
{
    m_camera.OnMouseButtonUp();
}

void MaterialScene::OnMouseMove(int dx, int dy)
{
    float fdx = float(dx) / GetWidth();
    float fdy = float(dy) / GetHeight();
    m_camera.OnMouseMove(-fdx, fdy);
}

void MaterialScene::CreateSceneObjects()
{
    const auto flags = D3D12_RESOURCE_FLAG_NONE;
    const auto heapType = D3D12_HEAP_TYPE_DEFAULT;

    std::vector<util::primitive::VertexPNT> verticesPlane;
    std::vector<UINT> indices;
    util::primitive::GetPlane(verticesPlane, indices, 10.0f);

    auto vstride = UINT(sizeof(util::primitive::VertexPNT));
    auto istride = UINT(sizeof(UINT));
    auto vbPlaneSize = vstride * verticesPlane.size();
    auto ibPlaneSize = istride * indices.size();
    m_meshPlane.vertexBuffer = util::CreateBuffer(
        m_device, vbPlaneSize, verticesPlane.data(), heapType, flags, L"PlaneVB");
    m_meshPlane.indexBuffer = util::CreateBuffer(
        m_device, ibPlaneSize, indices.data(), heapType, flags, L"PlaneIB");
    m_meshPlane.vertexCount = UINT(verticesPlane.size());
    m_meshPlane.indexCount = UINT(indices.size());
    m_meshPlane.vertexStride = vstride;

    // ディスクリプタの生成.
    m_meshPlane.descriptorVB = util::CreateStructuredSRV(
        m_device,
        m_meshPlane.vertexBuffer.Get(),
        m_meshPlane.vertexCount, 0, vstride);
    m_meshPlane.descriptorIB = util::CreateStructuredSRV(
        m_device,
        m_meshPlane.indexBuffer.Get(),
        m_meshPlane.indexCount, 0, istride);
    m_meshPlane.shaderName = L"hgFloor";


    // スフィアを生成する.
    //  形状(BLASやポリゴンデータ)は共通として１つを参照する形とする.
    std::vector<util::primitive::VertexPN> vertices;
    util::primitive::GetSphere(vertices, indices, 0.5f, 32, 48);
    vstride = sizeof(util::primitive::VertexPN);
    auto vbSphereSize = vertices.size() * vstride;
    auto ibSphereSize = indices.size() * istride;
 
    m_meshSphere.vertexBuffer = util::CreateBuffer(
        m_device, vbSphereSize, vertices.data(), heapType, flags, L"SphereVB");
    m_meshSphere.indexBuffer = util::CreateBuffer(
        m_device, ibSphereSize, indices.data(), heapType, flags, L"SphereIB");
    m_meshSphere.shaderName = L"";
    m_meshSphere.vertexCount = UINT(vertices.size());
    m_meshSphere.indexCount = UINT(indices.size());
    m_meshSphere.vertexStride = vstride;

    // ディスクリプタの生成.
    m_meshSphere.descriptorVB = util::CreateStructuredSRV(
        m_device,
        m_meshSphere.vertexBuffer.Get(),
        m_meshSphere.vertexCount, 0, vstride);
    m_meshSphere.descriptorIB = util::CreateStructuredSRV(
        m_device,
        m_meshSphere.indexBuffer.Get(),
        m_meshSphere.indexCount, 0, istride);
    m_meshSphere.shaderName = L""; // 本サンプルでは使用しない.

    auto spheresCollection = { &m_spheresReflect, &m_spheresRefract, &m_spheresNormal };

    // スフィアを適当に配置する.
    std::mt19937 mt;
    std::uniform_int_distribution rnd(-9, 9);
    for (auto type : spheresCollection) {
        for (auto& spherePos : *type) {
            float y = 0.5f;
            float x = rnd(mt) + 0.5f;
            float z = rnd(mt) + 0.5f;
            spherePos = XMMatrixTranslation(x, y, z);
        }
    }

    // マテリアル情報を準備する.
    MaterialParam defaultMaterial{};
    defaultMaterial.albedo = XMVectorSet(1.0f, 1.0f, 1.0f, 1.0f);
    defaultMaterial.specular = XMVectorSet(1.0f, 1.0f, 1.0f, 40.0f);

    UINT index = 0;
    for (auto& material : m_normalSphereMaterials) {
        material = defaultMaterial;
        material.albedo = colorTable[index % _countof(colorTable)];
        index++;
    }

    auto bufferSize = sizeof(MaterialParam) * m_normalSphereMaterials.size();
    m_normalSphereMaterialCB = util::CreateConstantBuffer(m_device, bufferSize);
    void* mapped = nullptr;
    m_normalSphereMaterialCB->Map(0, nullptr, &mapped);
    memcpy(mapped, m_normalSphereMaterials.data(), bufferSize);
    m_normalSphereMaterialCB->Unmap(0, nullptr);
}

void MaterialScene::CreateSceneBLAS()
{
    auto d3d12Device = m_device->GetDevice();

    D3D12_RAYTRACING_GEOMETRY_DESC planeGeomDesc{};
    planeGeomDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    planeGeomDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    {
        auto& triangles = planeGeomDesc.Triangles;
        triangles.VertexBuffer.StartAddress = m_meshPlane.vertexBuffer->GetGPUVirtualAddress();
        triangles.VertexBuffer.StrideInBytes = m_meshPlane.vertexStride;
        triangles.VertexCount = m_meshPlane.vertexCount;
        triangles.IndexBuffer = m_meshPlane.indexBuffer->GetGPUVirtualAddress();
        triangles.IndexCount = m_meshPlane.indexCount;
        triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
        triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
    }

    // BLAS の作成
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc{};
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs = asDesc.Inputs;
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;

    // BLAS を構築するためのバッファを準備 (Plane).
    inputs.NumDescs = 1;
    inputs.pGeometryDescs = &planeGeomDesc;
    //util::AccelerationStructureBuffers
    auto planeASB = util::CreateAccelerationStructure(m_device, asDesc);
    planeASB.asbuffer->SetName(L"Plane-Blas");
    asDesc.ScratchAccelerationStructureData = planeASB.scratch->GetGPUVirtualAddress();
    asDesc.DestAccelerationStructureData = planeASB.asbuffer->GetGPUVirtualAddress();

    // コマンドリストに積む.
    auto command = m_device->CreateCommandList();
    command->BuildRaytracingAccelerationStructure(
        &asDesc, 0, nullptr);

    // スフィアに関しての BLAS を生成.
    std::vector<util::AccelerationStructureBuffers> asbuffers;
    D3D12_RAYTRACING_GEOMETRY_DESC sphereGeomDesc{};
    sphereGeomDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    sphereGeomDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    {
        auto& triangles = sphereGeomDesc.Triangles;
        triangles.VertexBuffer.StartAddress = m_meshSphere.vertexBuffer->GetGPUVirtualAddress();
        triangles.VertexBuffer.StrideInBytes = m_meshSphere.vertexStride;
        triangles.VertexCount = m_meshSphere.vertexCount;
        triangles.IndexBuffer = m_meshSphere.indexBuffer->GetGPUVirtualAddress();
        triangles.IndexCount = m_meshSphere.indexCount;
        triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
        triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
    }

    // BLAS を構築するためのバッファを準備 (Sphere).
    inputs.NumDescs = 1;
    inputs.pGeometryDescs = &sphereGeomDesc;
    auto sphereASB = util::CreateAccelerationStructure(m_device, asDesc);
    sphereASB.asbuffer->SetName(L"Sphere-Blas");
    asDesc.ScratchAccelerationStructureData = sphereASB.scratch->GetGPUVirtualAddress();
    asDesc.DestAccelerationStructureData = sphereASB.asbuffer->GetGPUVirtualAddress();

    // コマンドリストに積む.
    command->BuildRaytracingAccelerationStructure(
        &asDesc, 0, nullptr);

    // BLAS のバッファに UAV バリアを設定する.
    std::vector<CD3DX12_RESOURCE_BARRIER> uavBarriers;
    uavBarriers.emplace_back(CD3DX12_RESOURCE_BARRIER::UAV(planeASB.asbuffer.Get()));
    uavBarriers.emplace_back(CD3DX12_RESOURCE_BARRIER::UAV(sphereASB.asbuffer.Get()));

    command->ResourceBarrier( UINT(uavBarriers.size()), uavBarriers.data());
    command->Close();

    // コマンドを実行して BLAS の構築を完了させる.
    m_device->ExecuteCommandList(command);

    // この先は BLAS のバッファのみ使うのでメンバ変数に代入しておく.
    m_meshPlane.blas = planeASB.asbuffer;
    m_meshSphere.blas = sphereASB.asbuffer;

    // 本関数を抜けるとスクラッチバッファ解放となるため待機.
    m_device->WaitForIdleGpu();
}

void MaterialScene::CreateSceneTLAS()
{
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs;
    DeployObjects(instanceDescs);

    // インスタンスの情報を記録したバッファを準備する.
    ComPtr<ID3D12Resource> instanceDescBuffer;
    size_t sizeOfInstanceDescs = instanceDescs.size() * sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
    instanceDescBuffer = m_device->CreateBuffer(
        sizeOfInstanceDescs,
        D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_HEAP_TYPE_UPLOAD
    );
    m_device->WriteToHostVisibleMemory(
        instanceDescBuffer, instanceDescs.data(), sizeOfInstanceDescs);


    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc{};
    auto& inputs = asDesc.Inputs;
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
    
    inputs.NumDescs = UINT(instanceDescs.size());
    inputs.InstanceDescs = instanceDescBuffer->GetGPUVirtualAddress();

    auto sceneASB = util::CreateAccelerationStructure(m_device, asDesc);
    sceneASB.asbuffer->SetName(L"Scene-Tlas");
    asDesc.ScratchAccelerationStructureData = sceneASB.scratch->GetGPUVirtualAddress();
    asDesc.DestAccelerationStructureData = sceneASB.asbuffer->GetGPUVirtualAddress();

    // コマンドリストに積む.
    auto command = m_device->CreateCommandList();
    command->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);

    // TLAS に対しての UAV バリアを設定.
    D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::UAV(
        sceneASB.asbuffer.Get()
    );
    command->Close();

    // コマンドを実行して TLAS の構築を完了させる.
    m_device->ExecuteCommandList(command);

    // この先は TLAS のバッファのみ使うのでメンバ変数に代入しておく.
    m_tlas = sceneASB.asbuffer;

    // ディスクリプタの準備.
    m_tlasDescriptor = m_device->AllocateDescriptor();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.RaytracingAccelerationStructure.Location = m_tlas->GetGPUVirtualAddress();
    m_device->GetDevice()->CreateShaderResourceView(
        nullptr, &srvDesc, m_tlasDescriptor.hCpu);

    // 本関数を抜けるとスクラッチバッファ解放となるため待機.
    m_device->WaitForIdleGpu();
}


void MaterialScene::CreateStateObject()
{
    // シェーダーファイルの読み込み.
    struct ShaderFileInfo {
        std::vector<char> binary;
        D3D12_SHADER_BYTECODE code;
    };

    const auto RayGenShader = L"raygen.dxlib";
    const auto MissShader = L"miss.dxlib";
    const auto FloorClosestHitShader = L"chsFloor.dxlib";
    const auto SphereMaterialsClosestHitShader = L"chsSphereNoLambert.dxlib";
    const auto SpherePhongMaterialClosestHitShader = L"chsSpherePhong.dxlib";
    
    std::unordered_map<std::wstring, ShaderFileInfo> shaders;
    const auto shaderFiles = {
        RayGenShader, MissShader, 
        FloorClosestHitShader, 
        SphereMaterialsClosestHitShader, 
        SpherePhongMaterialClosestHitShader };

    for (auto& filename : shaderFiles ) {
        std::vector<char> data;
        if (!util::LoadFile(data, filename)) {
            throw std::runtime_error("shader file not found");
        }
        shaders.emplace( filename, ShaderFileInfo() );
        shaders[filename].binary = std::move(data);
        shaders[filename].code = CD3DX12_SHADER_BYTECODE(shaders[filename].binary.data(), shaders[filename].binary.size());
    }

    const UINT MaxPayloadSize = sizeof(XMFLOAT3) + sizeof(UINT);
    const UINT MaxAttributeSize = sizeof(XMFLOAT2);
    const UINT MaxRecursionDepth = 16;

    CD3DX12_STATE_OBJECT_DESC subobjects;
    subobjects.SetStateObjectType(D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);

    // シェーダーの各関数エントリの登録.
    auto dxilRayGen = subobjects.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    dxilRayGen->SetDXILLibrary(&shaders[RayGenShader].code);
    dxilRayGen->DefineExport(L"mainRayGen");

    auto dxilMiss = subobjects.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    dxilMiss->SetDXILLibrary(&shaders[MissShader].code);
    dxilMiss->DefineExport(L"mainMiss");

    auto dxilChsFloor = subobjects.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    dxilChsFloor->SetDXILLibrary(&shaders[FloorClosestHitShader].code);
    dxilChsFloor->DefineExport(L"mainFloorCHS");

    auto dxilChsSphere0 = subobjects.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    dxilChsSphere0->SetDXILLibrary(&shaders[SphereMaterialsClosestHitShader].code);
    dxilChsSphere0->DefineExport(L"chsSphereMaterial");

    auto dxilChsSphere1 = subobjects.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    dxilChsSphere1->SetDXILLibrary(&shaders[SpherePhongMaterialClosestHitShader].code);
    dxilChsSphere1->DefineExport(L"chsSphereMaterialPhong");

    // ヒットグループの設定(球体に対する).
    auto hitgroupPhong = subobjects.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
    hitgroupPhong->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);
    hitgroupPhong->SetClosestHitShaderImport(L"chsSphereMaterialPhong");
    hitgroupPhong->SetHitGroupExport(AppHitGroups::PhongMaterial);

    // ヒットグループの設定(反射および屈折の球体に対する).
    auto hitgroupNoPhong = subobjects.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
    hitgroupNoPhong->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);
    hitgroupNoPhong->SetClosestHitShaderImport(L"chsSphereMaterial");
    hitgroupNoPhong->SetHitGroupExport(AppHitGroups::NoPhongMaterial);

    // ヒットグループの設定(床に対する).
    auto hitgroupFloor = subobjects.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
    hitgroupFloor->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);
    hitgroupFloor->SetClosestHitShaderImport(L"mainFloorCHS");
    hitgroupFloor->SetHitGroupExport(AppHitGroups::Floor);

    // グローバル Root Signature 設定.
    auto rootsig = subobjects.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
    rootsig->SetRootSignature(m_rootSignatureGlobal.Get());

    // ローカル Root Signature 設定.
    auto rsRayGen = subobjects.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
    rsRayGen->SetRootSignature(m_rsRGS.Get());
    auto lrsAssocRGS = subobjects.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
    lrsAssocRGS->AddExport(L"mainRayGen");
    lrsAssocRGS->SetSubobjectToAssociate(*rsRayGen);

    //    床用.
    auto rsFloor = subobjects.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
    rsFloor->SetRootSignature(m_rsFloor.Get());
    auto lrsAssocFloor = subobjects.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
    lrsAssocFloor->AddExport(AppHitGroups::Floor);
    lrsAssocFloor->SetSubobjectToAssociate(*rsFloor);

    //    非Phongの球体に対して.
    auto rsModel = subobjects.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
    rsModel->SetRootSignature(m_rsSphere1.Get());
    auto lrsAssocModel = subobjects.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
    lrsAssocModel->AddExport(AppHitGroups::NoPhongMaterial);
    lrsAssocModel->SetSubobjectToAssociate(*rsModel);

    //    Phongの球体に対して.
    auto rsPhongSphere = subobjects.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
    rsPhongSphere->SetRootSignature(m_rsSphere2.Get());
    auto lrsAssocPhongSphere = subobjects.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
    lrsAssocPhongSphere->AddExport(AppHitGroups::PhongMaterial);
    lrsAssocPhongSphere->SetSubobjectToAssociate(*rsPhongSphere);

    // シェーダー設定.
    auto shaderConfig = subobjects.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
    shaderConfig->Config(MaxPayloadSize, MaxAttributeSize);

    // パイプライン設定.
    auto pipelineConfig = subobjects.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
    pipelineConfig->Config(MaxRecursionDepth);

    auto device = m_device->GetDevice();
    HRESULT hr = device->CreateStateObject(
        subobjects, IID_PPV_ARGS(m_rtState.ReleaseAndGetAddressOf())
    );
    if (FAILED(hr)) {
        throw std::runtime_error("CreateStateObject failed.");
    }
}

void MaterialScene::CreateResultBuffer()
{
    auto width = GetWidth();
    auto height = GetHeight();

    m_dxrOutput = m_device->CreateTexture2D(
        width, height, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE,
        D3D12_HEAP_TYPE_DEFAULT
    );

    // ディスクリプタの準備.
    m_outputDescriptor = m_device->AllocateDescriptor();
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    auto device = m_device->GetDevice();
    device->CreateUnorderedAccessView(m_dxrOutput.Get(), nullptr, &uavDesc, m_outputDescriptor.hCpu);
}

void MaterialScene::CreateRootSignatureGlobal()
{
    util::RootSignatureHelper rshelper;
    // [0] : ディスクリプタ, t0, TLAS 用.
    // [1] : 定数バッファ, b0, シーン情報用.
    // [2] : ディスクリプタ, t1, 背景テクスチャ用.
    rshelper.Add(util::RootSignatureHelper::RangeType::SRV, 0);
    rshelper.Add(util::RootSignatureHelper::RootType::CBV, 0);
    rshelper.Add(util::RootSignatureHelper::RangeType::SRV, 1);

    // スタティックサンプラーを用意.
    rshelper.AddStaticSampler(0);

    // グローバルルートシグネチャを生成.
    const bool isLocal = false;
    m_rootSignatureGlobal = rshelper.Create(m_device, isLocal, L"RootSignatureGlobal");
}

void MaterialScene::CreateLocalRootSignatureRayGen()
{
    util::RootSignatureHelper rshelper;
    // [0] : ディスクリプタ, u0, 出力用.
    rshelper.Add(util::RootSignatureHelper::RangeType::UAV, 0);

    // ローカルルートシグネチャ生成.
    const bool isLocal = true;
    m_rsRGS = rshelper.Create(m_device, isLocal, L"lrsRayGen");
}

void MaterialScene::CreateSphereLocalRootSignature()
{
    const UINT regSpace = 1;
    util::RootSignatureHelper rshelper;

    // [0]: ディスクリプタ, t0, インデックスバッファ.
    // [1]: ディスクリプタ, t1, 頂点バッファ.
    // [2]: 定数バッファ, b0, 定数バッファ.

    rshelper.Add(util::RootSignatureHelper::RangeType::SRV, 0, regSpace);
    rshelper.Add(util::RootSignatureHelper::RangeType::SRV, 1, regSpace);

    // 反射/屈折用ルートシグネチャ生成.
    const bool isLocal = true;
    m_rsSphere1 = rshelper.Create(m_device, isLocal, L"lrsSphere(Reflect/Refract)");

    rshelper.Clear();
    rshelper.Add(util::RootSignatureHelper::RangeType::SRV, 0, regSpace);
    rshelper.Add(util::RootSignatureHelper::RangeType::SRV, 1, regSpace);
    rshelper.Add(util::RootSignatureHelper::RootType::CBV, 0, regSpace);
    m_rsSphere2 = rshelper.Create(m_device, isLocal, L"lrsSphere(Phong)");
}

void MaterialScene::CreateFloorLocalRootSignature()
{
    const UINT regSpace = 1;
    util::RootSignatureHelper rshelper;

    // [0]: ディスクリプタ, t0, インデックスバッファ.
    // [1]: ディスクリプタ, t1, 頂点バッファ.
    // [2]: ディスクリプタ, t2, テクスチャ.
    rshelper.Add(util::RootSignatureHelper::RangeType::SRV, 0, regSpace);
    rshelper.Add(util::RootSignatureHelper::RangeType::SRV, 1, regSpace);
    rshelper.Add(util::RootSignatureHelper::RangeType::SRV, 2, regSpace);

    // ローカルルートシグネチャ生成.
    const bool isLocal = true;
    m_rsFloor = rshelper.Create(m_device, isLocal, L"lrsFloor");
}

void MaterialScene::CreateShaderTable()
{
    const auto ShaderRecordAlignment = D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT;
    // RayGeneration シェーダーでは、 Shader Identifier と
    // ローカルルートシグネチャによる u0 のディスクリプタを使用.
    UINT raygenRecordSize = 0;
    raygenRecordSize += D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    raygenRecordSize += sizeof(D3D12_GPU_DESCRIPTOR_HANDLE);
    raygenRecordSize = util::RoundUp(raygenRecordSize, ShaderRecordAlignment);

    // ヒットグループでは、 Shader Identifier の他に
    // ローカルルートシグネチャによる VB/IB のディスクリプタを使用.
    UINT hitgroupRecordSize = 0;
    hitgroupRecordSize += D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    hitgroupRecordSize += sizeof(D3D12_GPU_DESCRIPTOR_HANDLE);
    hitgroupRecordSize += sizeof(D3D12_GPU_DESCRIPTOR_HANDLE);
    hitgroupRecordSize = util::RoundUp(hitgroupRecordSize, ShaderRecordAlignment);

    // Missシェーダーではローカルルートシグネチャ未使用.
    UINT missRecordSize = 0;
    missRecordSize += D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    missRecordSize = util::RoundUp(missRecordSize, ShaderRecordAlignment);

    // シェーダーテーブルのサイズを求める.
    UINT raygenSize = 1 * raygenRecordSize; // 今1つの Ray Generation シェーダー.
    UINT missSize = 1 * missRecordSize;  // 今1つの Miss シェーダー.
    UINT hitgroupCount = 1 + 1 + NormalSpheres;// (床/反射・屈折共用/Phong描画用) によるヒットグループ.
    UINT hitGroupSize = hitgroupCount * hitgroupRecordSize;

    // 各テーブルごとにアライメント制約があるので調整する.
    auto tableAlign = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
    auto raygenRegion = util::RoundUp(raygenSize, tableAlign);
    auto missRegion = util::RoundUp(missSize, tableAlign);
    auto hitgroupRegion = util::RoundUp(hitGroupSize, tableAlign);

    // シェーダーテーブル確保.
    auto tableSize = raygenRegion + missRegion + hitgroupRegion;
    m_shaderTable = util::CreateBuffer(
        m_device, tableSize, nullptr,
        D3D12_HEAP_TYPE_UPLOAD,
        D3D12_RESOURCE_FLAG_NONE,
        L"ShaderTable"
    );

    ComPtr<ID3D12StateObjectProperties> rtsoProps;
    m_rtState.As(&rtsoProps);

    // 各シェーダーエントリを書き込んでいく.
    void* mapped = nullptr;
    m_shaderTable->Map(0, nullptr, &mapped);
    uint8_t* pStart = static_cast<uint8_t*>(mapped);

    // RayGeneration 用のシェーダーエントリを書き込み.
    auto rgsStart = pStart;
    {
        uint8_t* p = rgsStart;
        auto id = rtsoProps->GetShaderIdentifier(L"mainRayGen");
        if (id == nullptr) {
            throw std::logic_error("Not found ShaderIdentifier");
        }
        p += util::WriteShaderIdentifier(p, id);

        // ローカルルートシグネチャで u0 (出力先) を設定しているため
        // 対応するディスクリプタを書き込む.
        p += util::WriteGPUDescriptor(p, m_outputDescriptor);
    }

    // Miss Shader 用のシェーダーエントリを書き込み.
    auto missStart = pStart + raygenRegion;
    {
        uint8_t* p = missStart;
        auto id = rtsoProps->GetShaderIdentifier(L"mainMiss");
        if (id == nullptr) {
            throw std::logic_error("Not found ShaderIdentifier");
        }
        p += util::WriteShaderIdentifier(p, id);
        // ローカルルートシグネチャ使用時には他のデータを書き込む.
    }

    // Hit Group 用のシェーダーエントリを書き込み.
    auto hitgroupStart = pStart + raygenRegion + missRegion;
    {
        auto recordStart = hitgroupStart;
        
        auto id = rtsoProps->GetShaderIdentifier(L"hgFloor");
        if (id == nullptr) {
            throw std::logic_error("Not found ShaderIdentifier");
        }
        // plane に対応するシェーダーエントリを書き込む.
        recordStart = WriteHitgroupFloor(recordStart, m_meshPlane, hitgroupRecordSize);

        // sphere に対応するシェーダーエントリを書き込む.
        id = rtsoProps->GetShaderIdentifier(L"hgNoPhongSpheres");
        if (id == nullptr) {
            throw std::logic_error("Not found ShaderIdentifier");
        }
        recordStart = WriteHitgroupMaterial(recordStart, m_meshSphere, hitgroupRecordSize);

        // sphere (定数バッファ使用) に対応するエントリを書き込む.
        id = rtsoProps->GetShaderIdentifier(L"hgPhongSpheres");

        // マテリアル情報を格納した定数バッファのアドレス位置をずらし別のマテリアルを参照させる.
        auto cbAddress = m_normalSphereMaterialCB->GetGPUVirtualAddress();
        auto stride = sizeof(MaterialParam);
        for (auto& sphere : m_spheresNormal) {
            recordStart = WriteHitgroupPhong(recordStart, m_meshSphere, cbAddress, hitgroupRecordSize);
            cbAddress += stride;
        }
    }

    m_shaderTable->Unmap(0, nullptr);

    // DispatchRays のために情報をセットしておく.
    auto startAddress = m_shaderTable->GetGPUVirtualAddress();
    auto& shaderRecordRG = m_dispatchRayDesc.RayGenerationShaderRecord;
    shaderRecordRG.StartAddress = startAddress;
    shaderRecordRG.SizeInBytes = raygenRecordSize;
    startAddress += raygenRegion;

    auto& shaderRecordMS = m_dispatchRayDesc.MissShaderTable;
    shaderRecordMS.StartAddress = startAddress;
    shaderRecordMS.SizeInBytes = missSize;
    shaderRecordMS.StrideInBytes = missRecordSize;
    startAddress += missRegion;

    auto& shaderRecordHG = m_dispatchRayDesc.HitGroupTable;
    shaderRecordHG.StartAddress = startAddress;
    shaderRecordHG.SizeInBytes = hitGroupSize;
    shaderRecordHG.StrideInBytes = hitgroupRecordSize;
    startAddress += hitGroupSize;

    m_dispatchRayDesc.Width = GetWidth();
    m_dispatchRayDesc.Height = GetHeight();
    m_dispatchRayDesc.Depth = 1;
}
