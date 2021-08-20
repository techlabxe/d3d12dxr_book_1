#include "ShaderSampleScene.h"

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


ShadersSampleScene::ShadersSampleScene(UINT width, UINT height) : DxrBookFramework(width, height, L"ShadersSample"),
m_dispatchRayDescs(),m_sceneParam()
{
}

void ShadersSampleScene::OnInit()
{
    if (!InitializeGraphicsDevice(Win32Application::GetHWND()))
    {
        throw std::runtime_error("Failed Initialize GraphicsDevice.");
    }

    // シーンに配置するオブジェクトの生成.
    CreateSceneObjects();

    // シーンに配置するオブジェクトの BLAS を構築する.
    CreateSceneBLAS();

    // シーンの TLAS を構築する.
    CreateSceneTLAS();

    // グローバル Root Signature を用意.
    CreateRootSignatureGlobal();

    // ローカル Root Signature を用意.
    CreateRayGenLocalRootSignature();
    CreateFloorLocalRootSignature();
    CreateFenceLocalRootSignature();
    CreateAnalyticPrimsLocalRootSignature();

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

    XMFLOAT3 eyePos(-0.5f, 1.6f, 2.5f);
    eyePos = XMFLOAT3(-1.85f, 2.0f, 2.5f);
    XMFLOAT3 target(0.0f, 1.25f, 0.0f);
    m_camera.SetLookAt(eyePos, target);

    m_camera.SetPerspective(
        XM_PIDIV4, GetAspect(), 0.1f, 100.0f
    );

    XMFLOAT3 lightDir{ -0.25f,-0.5f, -0.6f }; // ワールド座標系での光源の向き.
    m_sceneParam.lightDirection = XMLoadFloat3(&lightDir);
}

void ShadersSampleScene::OnDestroy()
{
    m_device->WaitForIdleGpu();

    m_device->DeallocateDescriptor(m_meshFence.descriptorVB);
    m_device->DeallocateDescriptor(m_meshFence.descriptorIB);

    m_device->DeallocateDescriptor(m_meshPlane.descriptorVB);
    m_device->DeallocateDescriptor(m_meshPlane.descriptorIB);
    
    m_device->DeallocateDescriptor(m_outputDescriptor);
    m_device->DeallocateDescriptor(m_tlasDescriptor);

    ImGui_ImplDX12_Shutdown();
    TerminateGraphicsDevice();
}

void ShadersSampleScene::OnUpdate()
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

    if (ImGui::CollapsingHeader("Analytic Geometry", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Combo("Type", (int*)&m_analyticGeomParam.type, "Cube\0Sphere\0\0");
        float* diffuse = reinterpret_cast<float*>(&m_analyticGeomParam.diffuse);
        ImGui::InputFloat3("Diffuse(AABB)", diffuse);

        float* objCenter = reinterpret_cast<float*>(&m_analyticGeomParam.center);
        ImGui::InputFloat3("Center(local)", objCenter);
        ImGui::SliderFloat("Radius", &m_analyticGeomParam.radius, 0.1f, 0.5f, "%.2f");
    }
    if (ImGui::CollapsingHeader("SDF Geometry", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Combo("Shape", (int*)&m_sdfGeomParam.type, "Cube\0Sphere\0Torus\0\0");
        float* diffuse = reinterpret_cast<float*>(&m_sdfGeomParam.diffuse);
        ImGui::InputFloat3("Diffuse(SDF)", diffuse);

        float* extent = reinterpret_cast<float*>(&m_sdfGeomParam.extent);
        ImGui::InputFloat3("Box Extent", extent);
        ImGui::SliderFloat("Radius(SDF)", &m_sdfGeomParam.radius, 0.1f, 0.4f, "%.2f");
    }

    ImGui::End();

    m_sceneParam.mtxView = m_camera.GetViewMatrix();
    m_sceneParam.mtxProj = m_camera.GetProjectionMatrix();
    m_sceneParam.mtxViewInv = XMMatrixInverse(nullptr, m_sceneParam.mtxView);
    m_sceneParam.mtxProjInv = XMMatrixInverse(nullptr, m_sceneParam.mtxProj);

    m_sceneParam.lightColor = XMVectorSet(1.0f, 1.0f, 1.0f, 0.0f);
    m_sceneParam.lightDirection = XMVector3Normalize(XMLoadFloat3(&lightDir));
    m_sceneParam.ambientColor = XMVectorSet(0.2f, 0.2f, 0.2f, 0.0f);
    m_sceneParam.eyePosition = m_camera.GetPosition();
}

void ShadersSampleScene::OnRender()
{
    auto device = m_device->GetDevice();
    auto renderTarget = m_device->GetRenderTarget();
    auto allocator = m_device->GetCurrentCommandAllocator();
    allocator->Reset();
    m_commandList->Reset(allocator.Get(), nullptr);
    auto frameIndex = m_device->GetCurrentFrameIndex();

    m_sceneCB.Write(frameIndex, &m_sceneParam, sizeof(m_sceneParam));
    auto sceneConstantBuffer = m_sceneCB.Get(frameIndex);

    m_analyticCB.Write(frameIndex, &m_analyticGeomParam, sizeof(m_analyticGeomParam));
    m_sdfParamCB.Write(frameIndex, &m_sdfGeomParam, sizeof(m_sdfGeomParam));

    ID3D12DescriptorHeap* descriptorHeaps[] = {
        m_device->GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV).Get(),
    };
    m_commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);


    // レイトレーシング結果バッファを UAV 状態へ.
    auto barrierToUAV = CD3DX12_RESOURCE_BARRIER::Transition(
        m_dxrOutput.Get(),
        D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS
    );
    m_commandList->ResourceBarrier(1, &barrierToUAV);

    m_commandList->SetComputeRootSignature(m_rootSignatureGlobal.Get());
    m_commandList->SetComputeRootDescriptorTable(0, m_tlasDescriptor.hGpu);
    m_commandList->SetComputeRootConstantBufferView(1, sceneConstantBuffer->GetGPUVirtualAddress());

    m_commandList->SetPipelineState1(m_rtState.Get());
    m_commandList->DispatchRays(&m_dispatchRayDescs[frameIndex]);

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

    m_writeBufferIndex = (++m_writeBufferIndex) % m_device->BackBufferCount;
}


void ShadersSampleScene::CreateSceneTLAS()
{
    // オブジェクトを配置.
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs;
    DeployObjects(instanceDescs);

    auto sizeOfInstanceDescs = instanceDescs.size();
    sizeOfInstanceDescs *= sizeof(D3D12_RAYTRACING_INSTANCE_DESC);

    for (auto& buffer : m_instanceDescsBuffers) {
        buffer = util::CreateBuffer(
            m_device,
            sizeOfInstanceDescs, instanceDescs.data(), D3D12_HEAP_TYPE_UPLOAD
        );
    }

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc{};
    auto& inputs = asDesc.Inputs;
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.NumDescs = UINT(instanceDescs.size());
    inputs.InstanceDescs = m_instanceDescsBuffers[0]->GetGPUVirtualAddress();
    // 更新を処理するために許可フラグを設定する.
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;

    // AS を生成する.
    auto asb = util::CreateAccelerationStructure(m_device, asDesc);
    m_tlas = asb.asbuffer;
    m_tlasScratch = asb.scratch;

    // ディスクリプタを準備.
    m_tlasDescriptor = m_device->AllocateDescriptor();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.RaytracingAccelerationStructure.Location = m_tlas->GetGPUVirtualAddress();
    m_device->GetDevice()->CreateShaderResourceView(
        nullptr, &srvDesc, m_tlasDescriptor.hCpu
    );

    // TLAS の構築のためにバッファをセット.
    asDesc.DestAccelerationStructureData = m_tlas->GetGPUVirtualAddress();
    asDesc.ScratchAccelerationStructureData = m_tlasScratch->GetGPUVirtualAddress();

    // コマンドリストに積む.
    auto command = m_device->CreateCommandList();
    command->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);
    auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(m_tlas.Get());
    command->ResourceBarrier(1, &barrier);
    command->Close();
    m_device->ExecuteCommandList(command);
    m_device->WaitForIdleGpu();
}

void ShadersSampleScene::UpdateSceneTLAS(UINT frameIndex)
{
    // オブジェクトを配置.
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs;
    DeployObjects(instanceDescs);

    auto sizeOfInstanceDescs = instanceDescs.size();
    sizeOfInstanceDescs *= sizeof(D3D12_RAYTRACING_INSTANCE_DESC);

    auto& instanceDescBuffer = m_instanceDescsBuffers[frameIndex];
    m_device->WriteToHostVisibleMemory(
        instanceDescBuffer.Get(), instanceDescs.data(), sizeOfInstanceDescs);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc{};
    auto& inputs = asDesc.Inputs;
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.NumDescs = UINT(instanceDescs.size());
    inputs.InstanceDescs = instanceDescBuffer->GetGPUVirtualAddress();

    // TLAS の更新処理を行うためのフラグを設定する.
    inputs.Flags =
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE |
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;

    // インプレース更新を実行する.
    asDesc.SourceAccelerationStructureData = m_tlas->GetGPUVirtualAddress();
    asDesc.DestAccelerationStructureData = m_tlas->GetGPUVirtualAddress();
    asDesc.ScratchAccelerationStructureData = m_tlasScratch->GetGPUVirtualAddress();

    // コマンドリストに積む.
    m_commandList->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);
    auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(m_tlas.Get());
    m_commandList->ResourceBarrier(1, &barrier);

}


void ShadersSampleScene::DeployObjects(std::vector<D3D12_RAYTRACING_INSTANCE_DESC>& instanceDescs)
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
    // 網オブジェクト配置.
    {
        auto trans = XMMatrixTranslation(0.0f, 1.0f, 1.0f);
        D3D12_RAYTRACING_INSTANCE_DESC desc{};
        XMStoreFloat3x4(
            reinterpret_cast<XMFLOAT3X4*>(&desc.Transform), trans);
        desc.InstanceID = 0;
        desc.InstanceMask = 0xFF;
        desc.InstanceContributionToHitGroupIndex = 1;
        desc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
        desc.AccelerationStructure = m_meshFence.blas->GetGPUVirtualAddress();
        instanceDescs.push_back(desc);
    }
    // AABB によるジオメトリタイプを配置.
    {
        XMMATRIX mtx = XMMatrixRotationY(XMConvertToRadians(45.0f)) * XMMatrixTranslation(1.5f, 1.0f, 0.0f);
        D3D12_RAYTRACING_INSTANCE_DESC desc{};
        XMStoreFloat3x4(
            reinterpret_cast<XMFLOAT3X4*>(&desc.Transform), mtx);
        desc.InstanceID = 0;
        desc.InstanceMask = 0xFF;
        desc.InstanceContributionToHitGroupIndex = 2;
        desc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
        desc.AccelerationStructure = m_meshAABB.blas->GetGPUVirtualAddress();
        instanceDescs.push_back(desc);
    }
    // Signed Distance Field によるジオメトリを配置.
    {
        XMMATRIX mtx = XMMatrixRotationY(XMConvertToRadians(20.0f)) * XMMatrixTranslation(-.5f, 0.5f, 0.0f);
        D3D12_RAYTRACING_INSTANCE_DESC desc{};
        XMStoreFloat3x4(
            reinterpret_cast<XMFLOAT3X4*>(&desc.Transform), mtx);
        desc.InstanceID = 0;
        desc.InstanceMask = 0xFF;
        desc.InstanceContributionToHitGroupIndex = 3;
        desc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
        desc.AccelerationStructure = m_meshSDF.blas->GetGPUVirtualAddress();
        instanceDescs.push_back(desc);
    }

}

void ShadersSampleScene::OnMouseDown(MouseButton button, int x, int y)
{
    float fdx = float(x) / GetWidth();
    float fdy = float(y) / GetHeight();
    m_camera.OnMouseButtonDown(int(button), fdx, fdy);
}

void ShadersSampleScene::OnMouseUp(MouseButton button, int x, int y)
{
    m_camera.OnMouseButtonUp();
}

void ShadersSampleScene::OnMouseMove(int dx, int dy)
{
    float fdx = float(dx) / GetWidth();
    float fdy = float(dy) / GetHeight();
    m_camera.OnMouseMove(-fdx, fdy);
}

void ShadersSampleScene::CreateSceneObjects()
{  
    auto d3d12Device = m_device->GetDevice();
    const auto flags = D3D12_RESOURCE_FLAG_NONE;
    const auto heapType = D3D12_HEAP_TYPE_DEFAULT;

    std::vector<util::primitive::VertexPN> vertices;
    std::vector<UINT> indices;
    util::primitive::GetPlane(vertices, indices, 10.0f);

    
    auto vstride = UINT(sizeof(util::primitive::VertexPN));
    auto istride = UINT(sizeof(UINT));
    auto vbPlaneSize = vstride * vertices.size();
    auto ibPlaneSize = sizeof(UINT) * indices.size();
    m_meshPlane.vertexBuffer = util::CreateBuffer(
        m_device, vbPlaneSize, vertices.data(), heapType, flags, L"PlaneVB");
    m_meshPlane.indexBuffer = util::CreateBuffer(
        m_device, ibPlaneSize, indices.data(), heapType, flags, L"PlaneIB");
    m_meshPlane.vertexCount = UINT(vertices.size());
    m_meshPlane.indexCount = UINT(indices.size());
    m_meshPlane.vertexStride = vstride;

    // ディスクリプタの生成.
    m_meshPlane.descriptorVB = util::CreateStructuredSRV(
        m_device,
        m_meshPlane.vertexBuffer,
        m_meshPlane.vertexCount, 0, vstride);
    m_meshPlane.descriptorIB = util::CreateStructuredSRV(
        m_device,
        m_meshPlane.indexBuffer,
        m_meshPlane.indexCount, 0, istride);
    m_meshPlane.shaderName = AppHitGroups::Floor;


    std::vector<util::primitive::VertexPNT> planeXYVerts;
    util::primitive::GetPlaneXY(planeXYVerts, indices);
    for (auto& v : planeXYVerts) {
        v.Position.x *= 4.0f;
    }
    vstride = UINT(sizeof(util::primitive::VertexPNT));
    istride = UINT(sizeof(UINT));
    auto vbFenceSize = vstride * planeXYVerts.size();
    auto ibFenceSize = istride * indices.size();
    m_meshFence.vertexBuffer = util::CreateBuffer(
        m_device, vbFenceSize, planeXYVerts.data(), 
        heapType, flags, L"FenceVB");
    m_meshFence.indexBuffer = util::CreateBuffer(
        m_device, ibFenceSize, indices.data(), 
        heapType, flags, L"FenceIB");
    m_meshFence.vertexCount = UINT(planeXYVerts.size());
    m_meshFence.indexCount = UINT(indices.size());
    m_meshFence.vertexStride = vstride;

    m_meshFence.descriptorVB = util::CreateStructuredSRV(
        m_device,
        m_meshFence.vertexBuffer,
        m_meshFence.vertexCount, 0, vstride);
    m_meshFence.descriptorIB = util::CreateStructuredSRV(
        m_device,
        m_meshFence.indexBuffer,
        m_meshFence.indexCount, 0, istride);
    m_meshFence.shaderName = AppHitGroups::AnyHitModel;

    m_texture = util::LoadTextureFromFile(L"texture.png", m_device);
    m_whiteTex = util::LoadTextureFromFile(L"white.png", m_device);
        
    D3D12_RAYTRACING_AABB aabbData{};
    aabbData.MinX = -0.5f;
    aabbData.MinY = -0.5f;
    aabbData.MinZ = -0.5f;
    aabbData.MaxX = 0.5f;
    aabbData.MaxY = 0.5f;
    aabbData.MaxZ = 0.5f;
    m_meshAABB.aabbBuffer = util::CreateBuffer(m_device, sizeof(aabbData), &aabbData, heapType, flags, L"meshAABB");
    m_meshAABB.shaderName = AppHitGroups::IntersectAABB;

    m_meshSDF.aabbBuffer = util::CreateBuffer(m_device, sizeof(aabbData), &aabbData, heapType, flags, L"meshSDF");
    m_meshSDF.shaderName = AppHitGroups::IntersectSDF;

    m_analyticCB.Initialize(m_device, sizeof(AnalyticGeometryParam), L"analyticGeometryParam");
    m_sdfParamCB.Initialize(m_device, sizeof(SDFGeometryParam), L"SDFGeometryParam");
}

void ShadersSampleScene::CreateSceneBLAS()
{
    auto floorGeomDesc = util::GetGeometryDesc(m_meshPlane);
    auto fenceGeomDesc = util::GetGeometryDesc(m_meshFence);
    fenceGeomDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;

    auto aabbGeomDesc = util::GetGeometryDesc(m_meshAABB);
    auto sdfGeomDesc = util::GetGeometryDesc(m_meshSDF);

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
    auto floorASB = util::CreateAccelerationStructure(m_device, asDesc);
    floorASB.asbuffer->SetName(L"Floor-Blas");
    asDesc.ScratchAccelerationStructureData = floorASB.scratch->GetGPUVirtualAddress();
    asDesc.DestAccelerationStructureData = floorASB.asbuffer->GetGPUVirtualAddress();
    // コマンドリストに積む.
    auto command = m_device->CreateCommandList();
    command->BuildRaytracingAccelerationStructure(
        &asDesc, 0, nullptr);

    // Fence の分.
    inputs.NumDescs = 1;
    inputs.pGeometryDescs = &fenceGeomDesc;
    auto fenceASB = util::CreateAccelerationStructure(m_device, asDesc);
    asDesc.ScratchAccelerationStructureData = fenceASB.scratch->GetGPUVirtualAddress();
    asDesc.DestAccelerationStructureData = fenceASB.asbuffer->GetGPUVirtualAddress();
    command->BuildRaytracingAccelerationStructure(
        &asDesc, 0, nullptr);

    // AABB に対しても同様に BLAS を構築する.
    inputs.NumDescs = 1;
    inputs.pGeometryDescs = &aabbGeomDesc;
    auto aabbASB = util::CreateAccelerationStructure(m_device, asDesc);
    asDesc.ScratchAccelerationStructureData = aabbASB.scratch->GetGPUVirtualAddress();
    asDesc.DestAccelerationStructureData = aabbASB.asbuffer->GetGPUVirtualAddress();
    command->BuildRaytracingAccelerationStructure(
        &asDesc, 0, nullptr);

    // SDF ジオメトリに対しても同様に BLAS を構築する.
    inputs.NumDescs = 1;
    inputs.pGeometryDescs = &sdfGeomDesc;
    auto sdfASB = util::CreateAccelerationStructure(m_device, asDesc);
    asDesc.ScratchAccelerationStructureData = sdfASB.scratch->GetGPUVirtualAddress();
    asDesc.DestAccelerationStructureData = sdfASB.asbuffer->GetGPUVirtualAddress();
    command->BuildRaytracingAccelerationStructure(
        &asDesc, 0, nullptr);

    // BLAS のバッファに UAV バリアを設定する.
    std::vector<CD3DX12_RESOURCE_BARRIER> uavBarriers;
    uavBarriers.emplace_back(CD3DX12_RESOURCE_BARRIER::UAV(floorASB.asbuffer.Get()));
    uavBarriers.emplace_back(CD3DX12_RESOURCE_BARRIER::UAV(fenceASB.asbuffer.Get()));
    uavBarriers.emplace_back(CD3DX12_RESOURCE_BARRIER::UAV(aabbASB.asbuffer.Get()));
    uavBarriers.emplace_back(CD3DX12_RESOURCE_BARRIER::UAV(sdfASB.asbuffer.Get()));

    command->ResourceBarrier(UINT(uavBarriers.size()), uavBarriers.data());
    command->Close();

    // コマンドを実行して BLAS の構築を完了させる.
    m_device->ExecuteCommandList(command);

    // この先は BLAS のバッファのみ使うのでメンバ変数に代入しておく.
    m_meshPlane.blas = floorASB.asbuffer;
    m_meshFence.blas = fenceASB.asbuffer;
    m_meshAABB.blas = aabbASB.asbuffer;
    m_meshSDF.blas = sdfASB.asbuffer;

    // 本関数を抜けるとスクラッチバッファ解放となるため待機.
    m_device->WaitForIdleGpu();
}


void ShadersSampleScene::CreateStateObject()
{
    // シェーダーファイルの読み込み.
    struct ShaderFileInfo {
        std::vector<char> binary;
        D3D12_SHADER_BYTECODE code;
    };

    const auto RayGenShader = L"raygen.dxlib";
    const auto MissShader = L"miss.dxlib";
    const auto FloorClosestHitShader = L"chsFloor.dxlib";
    const auto AnyHitShader = L"modelAnyHit.dxlib";
    const auto AABBIntersectionShader = L"AABBIntersection.dxlib";
    const auto SDFIntersectionShader = L"SDFIntersection.dxlib";

    std::unordered_map<std::wstring, ShaderFileInfo> shaders;
    const auto shaderFiles = {
        RayGenShader, MissShader,
        FloorClosestHitShader,
        AnyHitShader,
        AABBIntersectionShader,
        SDFIntersectionShader,
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
    const UINT MaxAttributeSize =  sizeof(XMFLOAT3);
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
    dxilMiss->DefineExport(L"shadowMiss");

    auto dxilChsFloor = subobjects.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    dxilChsFloor->SetDXILLibrary(&shaders[FloorClosestHitShader].code);
    dxilChsFloor->DefineExport(L"mainFloorCHS");

    auto dxilAnyHit = subobjects.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    dxilAnyHit->SetDXILLibrary(&shaders[AnyHitShader].code);
    dxilAnyHit->DefineExport(L"mainAnyHit");
    dxilAnyHit->DefineExport(L"mainAnyHitModel");

    auto dxilIntersectAABB = subobjects.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    dxilIntersectAABB->SetDXILLibrary(&shaders[AABBIntersectionShader].code);
    dxilIntersectAABB->DefineExport(L"mainClosestHitAABB");
    dxilIntersectAABB->DefineExport(L"mainIntersectAABB");

    auto dxilIntersectSDF = subobjects.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    dxilIntersectSDF->SetDXILLibrary(&shaders[SDFIntersectionShader].code);
    dxilIntersectSDF->DefineExport(L"mainClosestHitSDF");
    dxilIntersectSDF->DefineExport(L"mainIntersectSDF");
    
    // ヒットグループの設定(床に対する).
    auto hitgroupFloor = subobjects.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
    hitgroupFloor->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);
    hitgroupFloor->SetClosestHitShaderImport(L"mainFloorCHS");
    hitgroupFloor->SetHitGroupExport(AppHitGroups::Floor);

    // AnyHit シェーダーを使ったヒットグループを作る.
    auto hitgroupAH = subobjects.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
    hitgroupAH->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);
    hitgroupAH->SetClosestHitShaderImport(L"mainAnyHitModel");
    hitgroupAH->SetAnyHitShaderImport(L"mainAnyHit");
    hitgroupAH->SetHitGroupExport(AppHitGroups::AnyHitModel);

    // AABB Intersection シェーダーを使ったヒットグループを作る.
    auto hitgroupAABB = subobjects.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
    hitgroupAABB->SetHitGroupType(D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE);
    hitgroupAABB->SetClosestHitShaderImport(L"mainClosestHitAABB");
    hitgroupAABB->SetIntersectionShaderImport(L"mainIntersectAABB");
    hitgroupAABB->SetHitGroupExport(AppHitGroups::IntersectAABB);

    // SDF Intersection シェーダーを使ったヒットグループを作る.
    auto hitgroupSDF = subobjects.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
    hitgroupSDF->SetHitGroupType(D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE);
    hitgroupSDF->SetClosestHitShaderImport(L"mainClosestHitSDF");
    hitgroupSDF->SetIntersectionShaderImport(L"mainIntersectSDF");
    hitgroupSDF->SetHitGroupExport(AppHitGroups::IntersectSDF);

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

    // ローカルルートシグネチャ割当(AnyHitモデル用)
    auto lrsAnyHitModel = subobjects.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
    lrsAnyHitModel->SetRootSignature(m_rsModel.Get());
    auto assocAnyHitModel = subobjects.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
    assocAnyHitModel->AddExport(AppHitGroups::AnyHitModel);
    assocAnyHitModel->SetSubobjectToAssociate(*lrsAnyHitModel);


    // ローカルルートシグネチャ割当(IntersectAABB)
    auto lrsAnalyticPrims = subobjects.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
    lrsAnalyticPrims->SetRootSignature(m_rsAnalyticPrims.Get());
    auto assocAnalyticPrims = subobjects.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
    assocAnalyticPrims->AddExport(AppHitGroups::IntersectAABB);
    assocAnalyticPrims->SetSubobjectToAssociate(*lrsAnalyticPrims);

    // ローカルルートシグネチャ割当(IntersectSDF)
    auto lrsSdfPrims = subobjects.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
    lrsSdfPrims->SetRootSignature(m_rsSdfPrims.Get());
    auto assocSdfPrims = subobjects.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
    assocSdfPrims->AddExport(AppHitGroups::IntersectSDF);
    assocSdfPrims->SetSubobjectToAssociate(*lrsSdfPrims);

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

void ShadersSampleScene::CreateResultBuffer()
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

void ShadersSampleScene::CreateRootSignatureGlobal()
{
    // [0] : ディスクリプタ, t0, TLASバッファ.
    // [1] : 定数バッファ, b0, シーン共通定数バッファ.
    util::RootSignatureHelper rshelper;
    rshelper.Add(util::RootSignatureHelper::RangeType::SRV, 0);
    rshelper.Add(util::RootSignatureHelper::RootType::CBV, 0);

    // s0 でスタティックサンプラーを使用.
    rshelper.AddStaticSampler(0);

    // グローバルルートシグネチャ生成.
    auto isLocal = false;
    m_rootSignatureGlobal = rshelper.Create(m_device, isLocal, L"RootSignatureGlobal");
}

void ShadersSampleScene::CreateRayGenLocalRootSignature()
{
    // [0] : ディスクリプタ, u0, レイトレーシング結果書き込み用.
    util::RootSignatureHelper rshelper;
    rshelper.Add(util::RootSignatureHelper::RangeType::UAV, 0);
    
    // ローカルルートシグネチャ生成.
    auto isLocal = true;
    m_rsRGS = rshelper.Create(m_device, isLocal, L"lrsRayGen");
}

void ShadersSampleScene::CreateFenceLocalRootSignature()
{
    const UINT space = 1;
    // [0] : ディスクリプタ, t0(space1), インデックスバッファ.
    // [1] : ディスクリプタ, t1(space1), 頂点バッファ.
    // [2] : ディスクリプタ, t2(space1), テクスチャ.
    util::RootSignatureHelper rshelper;
    rshelper.Add(util::RootSignatureHelper::RangeType::SRV, 0, space);
    rshelper.Add(util::RootSignatureHelper::RangeType::SRV, 1, space);
    rshelper.Add(util::RootSignatureHelper::RangeType::SRV, 2, space);

    // ローカルルートシグネチャ生成.
    auto isLocal = true;
    m_rsModel = rshelper.Create(m_device, isLocal, L"lrsModel");
}


void ShadersSampleScene::CreateFloorLocalRootSignature()
{
    const UINT space = 1;
    // [0] : ディスクリプタ, t0(space1), インデックスバッファ.
    // [1] : ディスクリプタ, t1(space1), 頂点バッファ.
    // [2] : ディスクリプタ, t2(space1), テクスチャ.
    util::RootSignatureHelper rshelper;
    rshelper.Add(util::RootSignatureHelper::RangeType::SRV, 0, space);
    rshelper.Add(util::RootSignatureHelper::RangeType::SRV, 1, space);
    rshelper.Add(util::RootSignatureHelper::RangeType::SRV, 2, space);

    // ローカルルートシグネチャ生成.
    auto isLocal = true;
    m_rsFloor = rshelper.Create(m_device, isLocal, L"lrsFloor");
}

void ShadersSampleScene::CreateAnalyticPrimsLocalRootSignature()
{
    const UINT space = 1;
    // [0] : 定数バッファ, b0(space1), 個別の定数バッファ
    util::RootSignatureHelper rshelper;
    rshelper.Add(util::RootSignatureHelper::RootType::CBV, 0, space);

    m_rsAnalyticPrims = rshelper.Create(m_device, true, L"lrsAnalyticPrims");
    // シグネチャは同じなので作成.
    m_rsSdfPrims = rshelper.Create(m_device, true, L"lrsSdfPrims");
}

void ShadersSampleScene::CreateShaderTable()
{
    // RayGeneration シェーダーでは、 Shader Identifier と
    // ローカルルートシグネチャによる u0 のディスクリプタを使用.
    UINT raygenRecordSize = 0;
    raygenRecordSize += D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    raygenRecordSize += sizeof(D3D12_GPU_DESCRIPTOR_HANDLE);
    raygenRecordSize = util::RoundUp(raygenRecordSize, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);

    // ヒットグループでは、 Shader Identifier の他に
    // ローカルルートシグネチャによる VB/IB のディスクリプタを使用.
    UINT hitgroupRecordSize = 0;
    hitgroupRecordSize += D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    hitgroupRecordSize += sizeof(D3D12_GPU_DESCRIPTOR_HANDLE); // IB
    hitgroupRecordSize += sizeof(D3D12_GPU_DESCRIPTOR_HANDLE); // VB
    hitgroupRecordSize += sizeof(D3D12_GPU_DESCRIPTOR_HANDLE); // SRV
    hitgroupRecordSize = util::RoundUp(hitgroupRecordSize, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);


    // Missシェーダーではローカルルートシグネチャ未使用.
    UINT missRecordSize = 0;
    missRecordSize += D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    missRecordSize = util::RoundUp(missRecordSize, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);

    UINT hitGroupCount = 0;
    hitGroupCount += 1; // 床.
    hitGroupCount += 1; // フェンス.
    hitGroupCount += 2; // オブジェクト.

    // シェーダーテーブルのサイズを求める.
    UINT raygenSize = 1 * raygenRecordSize; // 今1つの Ray Generation シェーダー.
    UINT missSize = 2 * missRecordSize;  // 通常描画時とシャドウで２つの miss シェーダー.
    UINT hitGroupSize = hitGroupCount * hitgroupRecordSize;

    // 各テーブルごとにアライメント制約があるので調整する.
    auto tableAlign = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
    auto raygenRegion = util::RoundUp(raygenSize, tableAlign);
    auto missRegion = util::RoundUp(missSize, tableAlign);
    auto hitgroupRegion = util::RoundUp(hitGroupSize, tableAlign);

    // シェーダーテーブル確保.
    auto tableSize = raygenRegion + missRegion + hitgroupRegion;
    m_shaderTable.Initialize(m_device, tableSize, L"ShaderTable");

    ComPtr<ID3D12StateObjectProperties> rtsoProps;
    m_rtState.As(&rtsoProps);

    UINT count = m_device->BackBufferCount;
    // 各シェーダーエントリを書き込んでいく.
    for (UINT i = 0; i < count; ++i) {
        void* mapped = m_shaderTable.Map(i);
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
            auto recordStart = missStart;
            uint8_t* p = recordStart;
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

        // Hit Group 用のシェーダーレコードを書き込み.
        auto hitgroupStart = pStart + raygenRegion + missRegion;
        {
            auto recordStart = hitgroupStart;
            // 床のポリゴンメッシュ分を書き込む.
            recordStart = WriteHitgroupShaderRecord(recordStart, m_meshPlane, m_whiteTex.srv, hitgroupRecordSize);
            recordStart = WriteHitgroupShaderRecord(recordStart, m_meshFence, m_texture.srv, hitgroupRecordSize);
            recordStart = WriteHitgroupShaderRecord(
                recordStart, m_meshAABB, m_whiteTex.srv, m_analyticCB.Get(i), hitgroupRecordSize);
            recordStart = WriteHitgroupShaderRecord(
                recordStart, m_meshSDF, m_whiteTex.srv, m_sdfParamCB.Get(i), hitgroupRecordSize);
        }
        m_shaderTable.Unmap(i);

        // DispatchRays のために情報をセットしておく.
        auto startAddress = m_shaderTable.Get(i)->GetGPUVirtualAddress();
        auto& desc = m_dispatchRayDescs[i];

        auto& shaderRecordRG = desc.RayGenerationShaderRecord;
        shaderRecordRG.StartAddress = startAddress;
        shaderRecordRG.SizeInBytes = raygenSize;
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

}

uint8_t* ShadersSampleScene::WriteHitgroupShaderRecord(
    uint8_t* dst, const util::PolygonMesh& mesh,
    dx12::Descriptor texDescriptor, UINT hgEntrySize)
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
    dst += util::WriteGPUDescriptor(dst, texDescriptor);

    dst = entryBegin + hgEntrySize;
    return dst;
}

uint8_t* ShadersSampleScene::WriteHitgroupShaderRecord(
    uint8_t* dst, const util::ProcedualMesh& mesh, 
    dx12::Descriptor texDescriptor, 
    ComPtr<ID3D12Resource> cb,
    UINT hgEntrySize)
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
    dst += util::WriteGpuResourceAddr(dst, cb);

    dst = entryBegin + hgEntrySize;
    return dst;
}

