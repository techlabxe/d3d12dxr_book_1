#include "ShadowScene.h"

#include "Win32Application.h"

#include <fstream>
#include <random>
#include <DirectXTex.h>
#include "d3dx12.h"
#include "imgui.h"
#include "backends/imgui_impl_dx12.h"
#include "backends/imgui_impl_win32.h"

#include "util/DxrBookUtility.h"

using namespace DirectX;

ShadowScene::ShadowScene(UINT width, UINT height) : DxrBookFramework(width, height, L"ShadowScene"),
m_meshPlane(), m_meshSphere(), m_meshLightSphere(), m_dispatchRayDesc(),
m_sceneParam(),m_lightPos(), m_spheres()
{
}

void ShadowScene::OnInit()
{
    if (!InitializeGraphicsDevice(Win32Application::GetHWND()))
    {
        throw std::runtime_error("Failed Initialize GraphicsDevice.");
    }

    // シーンに配置するオブジェクトの生成.
    CreateSceneObjects();

    // 床とキューブの BLAS を構築する.
    CreateSceneBLAS();

    CreateSceneTLAS();

    // グローバル Root Signature を用意.
    CreateRootSignatureGlobal();

    // ローカル Root Signature を用意.
    CreateLocalRootSignatureRayGen();
    CreateFloorLocalRootSignature();
    CreateSphereLocalRootSignature();

    // コンパイル済みシェーダーよりステートオブジェクトを用意.
    CreateStateObject();

    // シーン用コンスタントバッファの確保.
    m_sceneCB.Initialize(m_device, sizeof(SceneParam), L"sceneCB");

    // レイトレーシング結果格納のためのバッファ(UAV)を用意.
    CreateResultBuffer();

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
    XMFLOAT3 target(0.0f, 1.0f, 0.0f);
    m_camera.SetLookAt(eyePos, target);

    m_camera.SetPerspective(
        XM_PIDIV4, GetAspect(), 0.1f, 100.0f
    );

    m_sceneParam.flags.x = 0; // 並行光源 or ポイントライトによる影描画.
    m_sceneParam.flags.y = 1; // ポイントライト位置の描画ON
    m_sceneParam.shadowRayCount = 1;

    XMFLOAT3 lightDir{ 0.25f,-0.5f, -0.6f }; // ワールド座標系での光源の向き.
    m_sceneParam.lightDirection = XMLoadFloat3(&lightDir);
}

void ShadowScene::OnDestroy()
{
    m_device->WaitForIdleGpu();

    m_device->DeallocateDescriptor(m_meshLightSphere.descriptorIB);
    m_device->DeallocateDescriptor(m_meshLightSphere.descriptorVB);

    m_device->DeallocateDescriptor(m_meshSphere.descriptorIB);
    m_device->DeallocateDescriptor(m_meshSphere.descriptorVB);

    m_device->DeallocateDescriptor(m_meshPlane.descriptorIB);
    m_device->DeallocateDescriptor(m_meshPlane.descriptorVB);

    ImGui_ImplDX12_Shutdown();
    TerminateGraphicsDevice();
}

void ShadowScene::OnUpdate()
{
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    auto framerate = ImGui::GetIO().Framerate;
    ImGui::Begin("Information");
    ImGui::BeginGroup();
    ImGui::Text("GPU: %s", m_device->GetAdapterName().c_str());
    ImGui::Text("Framerate %.3f ms", 1000.0f / framerate);
    XMFLOAT3 lightDir;
    XMStoreFloat3(&lightDir, m_sceneParam.lightDirection);
    ImGui::Text("LightDir(World)  (%.2f, %.2f, %.2f)", lightDir.x, lightDir.y, lightDir.z);

    XMFLOAT3 camPos;
    XMStoreFloat3(&camPos, m_camera.GetPosition());
    ImGui::Text("CameraPos (%.2f, %.2f, %.2f)", camPos.x, camPos.y, camPos.z);
    ImGui::EndGroup();
    ImGui::Separator();
    bool shadowType = m_sceneParam.flags.x ? true : false;
    if (ImGui::Checkbox("Use PointLightShadow", &shadowType)) {
        m_sceneParam.flags.x = shadowType ? 1 : 0;
    }
    bool dispPointLight = m_sceneParam.flags.y ? true : false;
    if (ImGui::Checkbox("Draw PointLight Position", &dispPointLight)) {
        m_sceneParam.flags.y = dispPointLight ? 1 : 0;
    }

    ImGui::SliderFloat("LightDistanceFactor", &m_lightDistanceFactor, 1.0f, 20.0f, "%.2f");

    float* lightPos = reinterpret_cast<float*>(&m_lightPos);
    ImGui::InputFloat3("LightPos(Point)", lightPos, "%.2f");

    ImGui::SliderInt("ShadowRays", (int*)&m_sceneParam.shadowRayCount, 1, 8);
    ImGui::End();

    XMFLOAT3 lightPosition;
    lightPosition.x = m_lightDistanceFactor * m_lightPos.x;
    lightPosition.y = m_lightDistanceFactor * m_lightPos.y;
    lightPosition.z = m_lightDistanceFactor * m_lightPos.z;

    m_sceneParam.mtxView = m_camera.GetViewMatrix();
    m_sceneParam.mtxProj = m_camera.GetProjectionMatrix();
    m_sceneParam.mtxViewInv = XMMatrixInverse(nullptr, m_sceneParam.mtxView);
    m_sceneParam.mtxProjInv = XMMatrixInverse(nullptr, m_sceneParam.mtxProj);

    m_sceneParam.lightColor = XMVectorSet(1.0f, 1.0f, 1.0f, 0.0f);
    m_sceneParam.lightDirection = XMVector3Normalize(XMLoadFloat3(&lightDir));
    m_sceneParam.ambientColor = XMVectorSet(0.2f, 0.2f, 0.2f, 0.0f);
    m_sceneParam.eyePosition = m_camera.GetPosition();

    // 点光源の位置を更新.
    m_pointLight.mtxWorld = XMMatrixTranslationFromVector(XMLoadFloat3(&lightPosition));
    m_sceneParam.pointLightPosition = lightPosition;
    
}

void ShadowScene::OnRender()
{
    auto device = m_device->GetDevice();
    auto renderTarget = m_device->GetRenderTarget();
    auto allocator = m_device->GetCurrentCommandAllocator();
    allocator->Reset();
    m_commandList->Reset(allocator.Get(), nullptr);
    auto frameIndex = m_device->GetCurrentFrameIndex();

    m_sceneCB.Write(frameIndex, &m_sceneParam, sizeof(m_sceneParam));
    auto sceneConstantBuffer = m_sceneCB.Get(frameIndex);

    UpdateSceneTLAS(frameIndex);

    ID3D12DescriptorHeap* descriptorHeaps[] = {
        m_device->GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV).Get(),
    };
    m_commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
    m_commandList->SetComputeRootSignature(m_rootSignatureGlobal.Get());
    m_commandList->SetComputeRootDescriptorTable(0, m_tlasDescriptor.hGpu);
    m_commandList->SetComputeRootConstantBufferView(1, sceneConstantBuffer->GetGPUVirtualAddress());

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

    // ImGui の描画.
    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_commandList.Get());

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


D3D12_RAYTRACING_GEOMETRY_DESC ShadowScene::GetGeometryDescFromPolygonMesh(const PolygonMesh& mesh)
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

uint8_t* ShadowScene::WriteHitgroupShaderEntry(uint8_t* dst, const PolygonMesh& mesh, UINT hgEntrySize)
{
    ComPtr<ID3D12StateObjectProperties> rtsoProps;
    m_rtState.As(&rtsoProps);
    auto entryBegin = dst;
    auto shader = mesh.shaderName;
    auto id = rtsoProps->GetShaderIdentifier(shader.c_str());
    if (id == nullptr) {
        throw std::logic_error("Not found ShaderIdentifier");
    }
    dst += util::WriteShaderIdentifier(dst, id);
    dst += util::WriteGPUDescriptor(dst, mesh.descriptorIB);
    dst += util::WriteGPUDescriptor(dst, mesh.descriptorVB);

    dst = entryBegin + hgEntrySize;
    return dst;
}

void ShadowScene::OnMouseDown(MouseButton button, int x, int y)
{
    float fdx = float(x) / GetWidth();
    float fdy = float(y) / GetHeight();
    m_camera.OnMouseButtonDown(int(button), fdx, fdy);
}

void ShadowScene::OnMouseUp(MouseButton button, int x, int y)
{
    m_camera.OnMouseButtonUp();
}

void ShadowScene::OnMouseMove(int dx, int dy)
{
    float fdx = float(dx) / GetWidth();
    float fdy = float(dy) / GetHeight();
    m_camera.OnMouseMove(-fdx, fdy);
}

void ShadowScene::CreateSceneObjects()
{
    const auto flags = D3D12_RESOURCE_FLAG_NONE;
    const auto heapType = D3D12_HEAP_TYPE_DEFAULT;

    std::vector<util::primitive::VertexPN> vertices;
    std::vector<UINT> indices;
    auto vstride = UINT(sizeof(util::primitive::VertexPN));
    auto istride = UINT(sizeof(UINT));

    // 床平面を生成する.
    util::primitive::GetPlane(vertices, indices, 10.0f);
    auto vbPlaneSize = vstride * vertices.size();
    auto ibPlaneSize = istride * indices.size();
    m_meshPlane.vertexBuffer = util::CreateBuffer(
        m_device, vbPlaneSize, vertices.data(), heapType, flags, L"PlaneVB");
    m_meshPlane.indexBuffer = util::CreateBuffer(
        m_device, ibPlaneSize, indices.data(), heapType, flags, L"PlaneIB");
    m_meshPlane.vertexCount = UINT(vertices.size());
    m_meshPlane.indexCount = UINT(indices.size());
    m_meshPlane.vertexStride = vstride;
    //   ディスクリプタの準備.
    m_meshPlane.descriptorIB = util::CreateStructuredSRV(
        m_device,
        m_meshPlane.indexBuffer,
        m_meshPlane.indexCount, 0, istride);
    m_meshPlane.descriptorVB = util::CreateStructuredSRV(
        m_device, 
        m_meshPlane.vertexBuffer, 
        m_meshPlane.vertexCount, 0, vstride);
    // 使用するヒットグループを設定.
    m_meshPlane.shaderName = AppHitGroups::Floor;
    vertices.clear();
    indices.clear();
    
    // スフィアを生成する.
    //  形状(BLASやポリゴンデータ)は共通として１つを参照する形とする.
    util::primitive::GetSphere(vertices, indices, 0.5f, 32, 48);
    auto vbSphereSize = vstride * vertices.size();
    auto ibSphereSize = istride * indices.size();
    m_meshSphere.vertexBuffer = util::CreateBuffer(
        m_device, vbSphereSize, vertices.data(), heapType, flags, L"SphereVB");
    m_meshSphere.indexBuffer = util::CreateBuffer(
        m_device, ibSphereSize, indices.data(), heapType, flags, L"SphereIB");
    m_meshSphere.vertexCount = UINT(vertices.size());
    m_meshSphere.indexCount = UINT(indices.size());
    m_meshSphere.vertexStride = vstride;
    //  ディスクリプタの準備.
    m_meshSphere.descriptorIB = util::CreateStructuredSRV(
        m_device,
        m_meshSphere.indexBuffer,
        m_meshSphere.indexCount, 0, istride);
    m_meshSphere.descriptorVB = util::CreateStructuredSRV(
        m_device,
        m_meshSphere.vertexBuffer,
        m_meshSphere.vertexCount, 0, vstride);
    // 使用するヒットグループを設定.
    m_meshSphere.shaderName = AppHitGroups::Sphere;
    vertices.clear();
    indices.clear();

    // ライト位置のスフィアを別作成.
    util::primitive::GetSphere(vertices, indices, 1.0f, 32, 48);
    auto vbLightSphereSize = vstride * vertices.size();
    auto ibLightSphereSize = istride * indices.size();
    m_meshLightSphere.vertexBuffer = util::CreateBuffer(
        m_device, vbLightSphereSize, vertices.data(), heapType, flags, L"LightSphereVB");
    m_meshLightSphere.indexBuffer = util::CreateBuffer(
        m_device, ibLightSphereSize, indices.data(), heapType, flags, L"LightSphereIB");
    m_meshLightSphere.vertexCount = UINT(vertices.size());
    m_meshLightSphere.indexCount = UINT(indices.size());
    m_meshLightSphere.vertexStride = vstride;
    //  ディスクリプタの準備.
    m_meshLightSphere.descriptorIB = util::CreateStructuredSRV(
        m_device,
        m_meshLightSphere.indexBuffer,
        m_meshLightSphere.indexCount, 0, istride);
    m_meshLightSphere.descriptorVB = util::CreateStructuredSRV(
        m_device,
        m_meshLightSphere.vertexBuffer,
        m_meshLightSphere.vertexCount, 0, vstride);
    // 使用するヒットグループを設定.
    m_meshLightSphere.shaderName = AppHitGroups::Light;
    vertices.clear();
    indices.clear();
    
    //// スフィアを適当に配置する.
    std::mt19937 mt;
    std::uniform_real_distribution rnd(.5f, 0.75f);
    std::uniform_int_distribution rnd2(-9, 9);
    for (auto& sphere : m_spheres) {
        float x = rnd2(mt) + 0.5f;
        float z = rnd2(mt) + 0.5f;
        sphere.mtxWorld = XMMatrixTranslation(x, 0.5, z);
    }

    // ポイントライト位置用.
    m_lightPos = XMFLOAT3(0, 2.5, 2);
    m_pointLight.mtxWorld = XMMatrixTranslationFromVector(XMLoadFloat3(&m_lightPos));
}

void ShadowScene::CreateSceneBLAS()
{
    auto d3d12Device = m_device->GetDevice();
    auto floorGeomDesc = GetGeometryDescFromPolygonMesh(m_meshPlane);
    auto sphereGeomDesc = GetGeometryDescFromPolygonMesh(m_meshSphere);
    auto lightSphereGeomDesc = GetGeometryDescFromPolygonMesh(m_meshLightSphere);

    // BLAS の作成
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc{};
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs = asDesc.Inputs;
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;

    // BLAS を構築するためのバッファを準備 (Plane).
    inputs.NumDescs = 1;
    inputs.pGeometryDescs = &floorGeomDesc;
    //util::AccelerationStructureBuffers
    auto planeASB = util::CreateAccelerationStructure(m_device, asDesc);
    planeASB.asbuffer->SetName(L"Plane-Blas");
    asDesc.ScratchAccelerationStructureData = planeASB.scratch->GetGPUVirtualAddress();
    asDesc.DestAccelerationStructureData = planeASB.asbuffer->GetGPUVirtualAddress();

    // コマンドリストに積む.
    auto command = m_device->CreateCommandList();
    command->BuildRaytracingAccelerationStructure(
        &asDesc, 0, nullptr);

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

    // BLAS を構築するためのバッファを準備 (LightSphere).
    inputs.NumDescs = 1;
    inputs.pGeometryDescs = &lightSphereGeomDesc;
    auto lightSphereASB = util::CreateAccelerationStructure(m_device, asDesc);
    lightSphereASB.asbuffer->SetName(L"Light-Blas");
    asDesc.ScratchAccelerationStructureData = lightSphereASB.scratch->GetGPUVirtualAddress();
    asDesc.DestAccelerationStructureData = lightSphereASB.asbuffer->GetGPUVirtualAddress();

    // コマンドリストに積む.
    command->BuildRaytracingAccelerationStructure(
        &asDesc, 0, nullptr);

    // BLAS のバッファに UAV バリアを設定する.
    std::vector<CD3DX12_RESOURCE_BARRIER> uavBarriers;
    uavBarriers.emplace_back(CD3DX12_RESOURCE_BARRIER::UAV(planeASB.asbuffer.Get()));
    uavBarriers.emplace_back(CD3DX12_RESOURCE_BARRIER::UAV(sphereASB.asbuffer.Get()));
    uavBarriers.emplace_back(CD3DX12_RESOURCE_BARRIER::UAV(lightSphereASB.asbuffer.Get()));

    command->ResourceBarrier(UINT(uavBarriers.size()), uavBarriers.data());
    command->Close();

    // コマンドを実行して BLAS の構築を完了させる.
    m_device->ExecuteCommandList(command);

    // この先は BLAS のバッファのみ使うのでメンバ変数に代入しておく.
    m_meshPlane.blas = planeASB.asbuffer;
    m_meshSphere.blas = sphereASB.asbuffer;
    m_meshLightSphere.blas = lightSphereASB.asbuffer;

    // 本関数を抜けるとスクラッチバッファ解放となるため待機.
    m_device->WaitForIdleGpu();
}

void ShadowScene::CreateSceneTLAS()
{
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs;
    DeployObjects(instanceDescs);

    auto sizeOfInstanceDescs = UINT(instanceDescs.size() * sizeof(D3D12_RAYTRACING_INSTANCE_DESC));
    m_instanceDescsBuffer.Initialize(m_device, sizeOfInstanceDescs, L"InstanceDescsBuffer");
    for (UINT i = 0; i < m_device->BackBufferCount; ++i) {
        void* dst = m_instanceDescsBuffer.Map(i);
        memcpy(dst, instanceDescs.data(), sizeOfInstanceDescs);
        m_instanceDescsBuffer.Unmap(i);
    }

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc{};
    auto& inputs = asDesc.Inputs;
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.NumDescs = UINT(instanceDescs.size());
    inputs.InstanceDescs = m_instanceDescsBuffer.Get(0)->GetGPUVirtualAddress();
    // 更新を処理するために許可フラグを設定する.
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;

    // AS を生成する.
    auto sceneASB = util::CreateAccelerationStructure(m_device, asDesc);
    sceneASB.asbuffer->SetName(L"Scene-Tlas");
    m_tlas = sceneASB.asbuffer;
    m_tlasUpdate = sceneASB.update;

    // ディスクリプタを準備.
    m_tlasDescriptor = m_device->AllocateDescriptor();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.RaytracingAccelerationStructure.Location = m_tlas->GetGPUVirtualAddress();
    m_device->GetDevice()->CreateShaderResourceView(
        nullptr, &srvDesc, m_tlasDescriptor.hCpu);

    // TLAS 構築のためのバッファをセット.
    asDesc.DestAccelerationStructureData = sceneASB.asbuffer->GetGPUVirtualAddress();
    asDesc.ScratchAccelerationStructureData = sceneASB.scratch->GetGPUVirtualAddress();

    // コマンドリストに積む.
    auto command = m_device->CreateCommandList();
    command->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);

    // TLAS に対しての UAV バリアを設定.
    D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::UAV(
        sceneASB.asbuffer.Get()
    );
    command->ResourceBarrier(1, &barrier);
    command->Close();

    // コマンドを実行して TLAS の構築を完了させる.
    m_device->ExecuteCommandList(command);

    // 本関数を抜けるとスクラッチバッファ解放となるため待機.
    m_device->WaitForIdleGpu();
}

void ShadowScene::UpdateSceneTLAS(UINT frameIndex)
{
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs;
    DeployObjects(instanceDescs);

    auto sizeOfInstanceDescs = instanceDescs.size();
    sizeOfInstanceDescs *= sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
   
    void* mapped = m_instanceDescsBuffer.Map(frameIndex);
    if (mapped) {
        memcpy(mapped, instanceDescs.data(), sizeOfInstanceDescs);
        m_instanceDescsBuffer.Unmap(frameIndex);
    }

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc{};
    auto& inputs = asDesc.Inputs;
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.NumDescs = UINT(instanceDescs.size());
    inputs.InstanceDescs = m_instanceDescsBuffer.Get(frameIndex)->GetGPUVirtualAddress();

    // TLAS の更新処理を行うためのフラグを設定する.
    inputs.Flags =
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE |
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;

    // インプレース更新を実行する.
    asDesc.SourceAccelerationStructureData = m_tlas->GetGPUVirtualAddress();
    asDesc.DestAccelerationStructureData = m_tlas->GetGPUVirtualAddress();
    asDesc.ScratchAccelerationStructureData = m_tlasUpdate->GetGPUVirtualAddress();

    // コマンドリストに積む.
    m_commandList->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);
    auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(m_tlas.Get());
    m_commandList->ResourceBarrier(1, &barrier);
}

void ShadowScene::CreateStateObject()
{
    // シェーダーファイルの読み込み.
    struct ShaderFileInfo {
        std::vector<char> binary;
        D3D12_SHADER_BYTECODE code;
    };

    const auto RayGenShader = L"raygen.dxlib";
    const auto MissShader = L"miss.dxlib";
    const auto FloorClosestHitShader = L"chsFloor.dxlib";
    const auto LightClosestHitShader = L"chsLight.dxlib";
    const auto SphereClosestHitShader = L"chsSphere.dxlib";

    std::unordered_map<std::wstring, ShaderFileInfo> shaders;
    const auto shaderFiles = {
        RayGenShader, MissShader,
        FloorClosestHitShader,
        LightClosestHitShader,
        SphereClosestHitShader,
    };

    for (auto& filename : shaderFiles) {
        std::vector<char> data;
        if (!util::LoadFile(data, filename)) {
            throw std::runtime_error("shader file not found");
        }
        shaders.emplace(filename, ShaderFileInfo());
        shaders[filename].binary = std::move(data);
        shaders[filename].code = CD3DX12_SHADER_BYTECODE(shaders[filename].binary.data(), shaders[filename].binary.size());
    }

    const UINT MaxPayloadSize = sizeof(XMFLOAT3) + sizeof(UINT);
    const UINT MaxAttributeSize = sizeof(XMFLOAT2);
    const UINT MaxRecursionDepth = 2;

    CD3DX12_STATE_OBJECT_DESC subobjects;
    subobjects.SetStateObjectType(D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);

    // シェーダーの各関数エントリの登録.
    auto dxilRayGen = subobjects.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    dxilRayGen->SetDXILLibrary(&shaders[RayGenShader].code);
    dxilRayGen->DefineExport(L"mainRayGen");

    auto dxilMiss = subobjects.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    dxilMiss->SetDXILLibrary(&shaders[MissShader].code);
    dxilMiss->DefineExport(L"mainMiss");
    dxilMiss->DefineExport(L"shadowMiss");

    auto dxilChsFloor = subobjects.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    dxilChsFloor->SetDXILLibrary(&shaders[FloorClosestHitShader].code);
    dxilChsFloor->DefineExport(L"mainFloorCHS");

    auto dxilChsLight = subobjects.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    dxilChsLight->SetDXILLibrary(&shaders[LightClosestHitShader].code);
    dxilChsLight->DefineExport(L"mainLightCHS");

    auto dxilChsSphere = subobjects.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    dxilChsSphere->SetDXILLibrary(&shaders[SphereClosestHitShader].code);
    dxilChsSphere->DefineExport(L"mainSphereCHS");

    // ヒットグループの設定(床に対する).
    auto hitgroupFloor = subobjects.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
    hitgroupFloor->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);
    hitgroupFloor->SetClosestHitShaderImport(L"mainFloorCHS");
    hitgroupFloor->SetHitGroupExport(AppHitGroups::Floor);

    // ヒットグループの設定(ライト).
    auto hitgroupLight = subobjects.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
    hitgroupLight->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);
    hitgroupLight->SetClosestHitShaderImport(L"mainLightCHS");
    hitgroupLight->SetHitGroupExport(AppHitGroups::Light);

    // ヒットグループの設定(スフィア).
    auto hitgroupSphere = subobjects.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
    hitgroupSphere->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);
    hitgroupSphere->SetClosestHitShaderImport(L"mainSphereCHS");
    hitgroupSphere->SetHitGroupExport(AppHitGroups::Sphere);

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

    //    ライト用.
    auto rsLight = subobjects.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
    rsLight->SetRootSignature(m_rsSphere.Get());
    auto lrsAssocLight = subobjects.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
    lrsAssocLight->AddExport(AppHitGroups::Light);
    lrsAssocLight->SetSubobjectToAssociate(*rsLight);

    //    スフィア用.
    auto rsModel = subobjects.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
    rsModel->SetRootSignature(m_rsSphere.Get());
    auto lrsAssocModel = subobjects.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
    lrsAssocModel->AddExport(AppHitGroups::Sphere);
    lrsAssocModel->SetSubobjectToAssociate(*rsModel);

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

void ShadowScene::CreateResultBuffer()
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

void ShadowScene::CreateRootSignatureGlobal()
{
    // [0] : ディスクリプタ, t0, TLAS バッファ.
    // [1] : 定数バッファ, b0, シーン共通定数バッファ.
    util::RootSignatureHelper rshelper;
    rshelper.Add(util::RootSignatureHelper::RangeType::SRV, 0);
    rshelper.Add(util::RootSignatureHelper::RootType::CBV, 0);

    // ルートシグネチャ生成生成.
    auto isLocal = false;
    m_rootSignatureGlobal = rshelper.Create(m_device, isLocal, L"RootSignatureGlobal");
}

void ShadowScene::CreateLocalRootSignatureRayGen()
{
    // [0] : ディスクリプタ, u0, レイトレーシング結果書き込み用.
    util::RootSignatureHelper rshelper;
    rshelper.Add(util::RootSignatureHelper::RangeType::UAV, 0);

    // ローカルルートシグネチャ生成.
    auto isLocal = true;
    m_rsRGS = rshelper.Create(m_device, isLocal, L"lrsRayGen");
}

void ShadowScene::CreateSphereLocalRootSignature()
{
    const UINT regSpace = 1;
    // [0] : ディスクリプタ, t0(space1), インデックスバッファ.
    // [1] : ディスクリプタ, t1(space1), 頂点バッファ.
    util::RootSignatureHelper rshelper;
    rshelper.Add(util::RootSignatureHelper::RangeType::SRV, 0, regSpace);
    rshelper.Add(util::RootSignatureHelper::RangeType::SRV, 1, regSpace);

    // ローカルルートシグネチャ生成.
    auto isLocal = true;
    m_rsSphere = rshelper.Create(m_device, isLocal, L"lrsSphere");
}


void ShadowScene::CreateFloorLocalRootSignature()
{
    const UINT regSpace = 1;
    // [0] : ディスクリプタ, t0(space1), インデックスバッファ.
    // [1] : ディスクリプタ, t1(space1), 頂点バッファ.
    util::RootSignatureHelper rshelper;
    rshelper.Add(util::RootSignatureHelper::RangeType::SRV, 0, regSpace);
    rshelper.Add(util::RootSignatureHelper::RangeType::SRV, 1, regSpace);

    // ローカルルートシグネチャ生成.
    auto isLocal = true;
    m_rsFloor = rshelper.Create(m_device, isLocal, L"lrsFloor");
}


void ShadowScene::CreateShaderTable()
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
    UINT hitgroupCount = 3; // 床,ライト,スフィア.
    UINT raygenSize = 1 * raygenRecordSize; // 今1つの Ray Generation シェーダー.
    UINT missSize = 2 * missRecordSize;  // 通常描画時とシャドウで２つの miss シェーダー.
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
        uint8_t* p = pStart;
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
        auto recordStart = missStart;
        uint8_t* p = missStart;
        auto id = rtsoProps->GetShaderIdentifier(L"mainMiss");
        if (id == nullptr) {
            throw std::logic_error("Not found ShaderIdentifier");
        }
        p += util::WriteShaderIdentifier(p, id);

        // 次の開始位置をセット.
        recordStart += missRecordSize;

        // シャドウ判定 Miss シェーダーの設定.
        p = recordStart;
        id = rtsoProps->GetShaderIdentifier(L"shadowMiss");
        if (id == nullptr) {
            throw std::logic_error("Not found ShaderIdentifier");
        }
        p += util::WriteShaderIdentifier(p, id);
    }

    // Hit Group 用のシェーダーエントリを書き込み.
    auto hitgroupStart = pStart + raygenRegion + missRegion;
    {
        auto recordStart = hitgroupStart;

        recordStart = WriteHitgroupShaderEntry(recordStart, m_meshPlane, hitgroupRecordSize);
        recordStart = WriteHitgroupShaderEntry(recordStart, m_meshLightSphere, hitgroupRecordSize);
        recordStart = WriteHitgroupShaderEntry(recordStart, m_meshSphere, hitgroupRecordSize);

        // 次のテーブル書き込みのためにインクリメント.
        pStart += hitGroupSize;
    }
    m_shaderTable->Unmap(0, nullptr);


    // DispatchRays のために情報をセットしておく.
    auto startAddress = m_shaderTable->GetGPUVirtualAddress();
    auto& desc = m_dispatchRayDesc;

    auto& shaderRecordRG = desc.RayGenerationShaderRecord;
    shaderRecordRG.StartAddress = startAddress;
    shaderRecordRG.SizeInBytes = raygenRecordSize;
    startAddress += raygenRegion;

    auto& shaderRecordMS = desc.MissShaderTable;
    shaderRecordMS.StartAddress = startAddress;
    shaderRecordMS.SizeInBytes = missSize;
    shaderRecordMS.StrideInBytes = missRecordSize;
    startAddress += missRegion;

    auto& shaderRecordHG = desc.HitGroupTable;
    shaderRecordHG.StartAddress = startAddress;
    shaderRecordHG.SizeInBytes = hitGroupSize;
    shaderRecordHG.StrideInBytes = hitgroupRecordSize;
    startAddress += hitgroupRegion;

    desc.Width = GetWidth();
    desc.Height = GetHeight();
    desc.Depth = 1;
}

void ShadowScene::DeployObjects(std::vector<D3D12_RAYTRACING_INSTANCE_DESC>& instanceDescs)
{
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

    // ライトを配置.
    {
        D3D12_RAYTRACING_INSTANCE_DESC desc{};
        XMStoreFloat3x4(
            reinterpret_cast<XMFLOAT3X4*>(&desc.Transform), m_pointLight.mtxWorld);
        desc.InstanceID = 0;
        desc.InstanceMask = 0x08;
        desc.InstanceContributionToHitGroupIndex = 1;
        desc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
        desc.AccelerationStructure = m_meshLightSphere.blas->GetGPUVirtualAddress();
        instanceDescs.push_back(desc);
    }

    // スフィアを配置.
    UINT instanceID = 0;
    for (const auto& sphere : m_spheres) {
        D3D12_RAYTRACING_INSTANCE_DESC desc{};
        XMStoreFloat3x4(
            reinterpret_cast<XMFLOAT3X4*>(&desc.Transform),
            sphere.mtxWorld);
        desc.InstanceID = instanceID;
        desc.InstanceMask = 0xFF;
        desc.InstanceContributionToHitGroupIndex = 2;
        desc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
        desc.AccelerationStructure = m_meshSphere.blas->GetGPUVirtualAddress();
        instanceDescs.push_back(desc);
        instanceID++;
    }
}
