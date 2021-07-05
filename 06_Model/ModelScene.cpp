#include "ModelScene.h"

#include "Win32Application.h"

#include <fstream>
#include <random>
#include <DirectXTex.h>
#include "d3dx12.h"
#include "imgui.h"
#include "backends/imgui_impl_dx12.h"
#include "backends/imgui_impl_win32.h"

#include "util/DxrBookUtility.h"
#include "util/DxrModel.h"

using namespace DirectX;


ModelScene::ModelScene(UINT width, UINT height) : DxrBookFramework(width, height, L"ModelScene"),
m_meshPlane(), m_dispatchRayDesc(), m_sceneParam(), m_guiParams()
{
}

void ModelScene::OnInit()
{
    if (!InitializeGraphicsDevice(Win32Application::GetHWND()))     {
        throw std::runtime_error("Failed Initialize GraphicsDevice.");
    }

    // スキニング計算のためのコンピュートシェーダーを準備する.
    CreateSkinningPipeline();

    // シーンに配置するオブジェクトの生成.
    CreateSceneObjects();

    // 床の BLAS を構築する.
    CreateSceneBLAS();

    PrepareModels();

    CreateSceneTLAS();

    // グローバル Root Signature を用意.
    CreateRootSignatureGlobal();

    // ローカル Root Signature を用意.
    CreateRayGenLocalRootSignature();
    CreateFloorLocalRootSignature();
    CreateModelLocalRootSignature();

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
    XMFLOAT3 target(0.0f, 1.0f, 0.0f);
    m_camera.SetLookAt(eyePos, target);

    m_camera.SetPerspective(
        XM_PIDIV4, GetAspect(), 0.1f, 100.0f
    );

    XMFLOAT3 lightDir{ -0.25f,-0.5f, -0.6f }; // ワールド座標系での光源の向き.
    m_sceneParam.lightDirection = XMLoadFloat3(&lightDir);
}

void ModelScene::OnDestroy()
{
    m_device->WaitForIdleGpu();
    m_actorChara.reset();
    m_actorPot1.reset();
    m_actorPot2.reset();
    m_actorTable.reset();

    m_modelChara.Destroy(m_device);
    m_modelPot.Destroy(m_device);
    m_modelTable.Destroy(m_device);

    m_device->DeallocateDescriptor(m_meshPlane.descriptorVB);
    m_device->DeallocateDescriptor(m_meshPlane.descriptorIB);
    
    m_device->DeallocateDescriptor(m_outputDescriptor);
    m_device->DeallocateDescriptor(m_tlasDescriptor);

    ImGui_ImplDX12_Shutdown();
    TerminateGraphicsDevice();
}

void ModelScene::OnUpdate()
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

    ImGui::SliderFloat("Elbow L", &m_guiParams.elbowL, 0.0f, 150.0f, "%.1f");
    ImGui::SliderFloat("Elbow R", &m_guiParams.elbowR, 0.0f, 150.0f, "%.1f");
    ImGui::SliderFloat("Neck", &m_guiParams.neck, -30.0f, 60.0f, "%.1f");

    ImGui::End();

    m_sceneParam.mtxView = m_camera.GetViewMatrix();
    m_sceneParam.mtxProj = m_camera.GetProjectionMatrix();
    m_sceneParam.mtxViewInv = XMMatrixInverse(nullptr, m_sceneParam.mtxView);
    m_sceneParam.mtxProjInv = XMMatrixInverse(nullptr, m_sceneParam.mtxProj);

    m_sceneParam.lightColor = XMVectorSet(1.0f, 1.0f, 1.0f, 0.0f);
    m_sceneParam.lightDirection = XMVector3Normalize(XMLoadFloat3(&lightDir));
    m_sceneParam.ambientColor = XMVectorSet(0.2f, 0.2f, 0.2f, 0.0f);
    m_sceneParam.eyePosition = m_camera.GetPosition();
 
    // スキニングモデルの行列を更新.
    if (m_actorChara->IsSkinned()) {
        std::shared_ptr<util::DxrModelActor::Node> node;
        node = m_actorChara->SearchNode(L"ひじ.L");
        if (node) {
            auto r = XMQuaternionRotationAxis(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), XMConvertToRadians(m_guiParams.elbowL));
            node->SetRotation(r);
        }
        node = m_actorChara->SearchNode(L"ひじ.R");
        if (node) {
            auto r = XMQuaternionRotationAxis(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), XMConvertToRadians(m_guiParams.elbowR));
            node->SetRotation(r);
        }
        node = m_actorChara->SearchNode(L"首");
        if (node) {
            auto r = XMQuaternionRotationAxis(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMConvertToRadians(m_guiParams.neck));
            node->SetRotation(r);
        }
    }

    // モデルを配置する場所をセット.
    auto x = 0.75f * sinf(m_frameCount * 0.01f);
    auto z = 0.25f * cosf(m_frameCount * 0.01f) + 0.5f;
    auto mtxTrans = XMMatrixTranslation(x, 0.0f, z);
    m_actorChara->SetWorldMatrix(mtxTrans);
    // 各関節の行列も更新.
    m_actorChara->UpdateMatrices();

    mtxTrans = XMMatrixRotationY(XMConvertToRadians(90.0f))* XMMatrixTranslation(0.0f, 0, -1.0);
    m_actorTable->SetWorldMatrix(mtxTrans);
    m_actorTable->UpdateMatrices();

    mtxTrans = XMMatrixTranslation(1.0f, 1.04f, -1.0f);;
    m_actorPot1->SetWorldMatrix(mtxTrans);
    m_actorPot1->UpdateMatrices();

    mtxTrans = XMMatrixTranslation(-1.0, 1.04f, -1.0f);
    m_actorPot2->SetWorldMatrix(mtxTrans);
    m_actorPot2->UpdateMatrices();
}

void ModelScene::OnRender()
{
    auto device = m_device->GetDevice();
    auto renderTarget = m_device->GetRenderTarget();
    auto allocator = m_device->GetCurrentCommandAllocator();
    allocator->Reset();
    m_commandList->Reset(allocator.Get(), nullptr);
    auto frameIndex = m_device->GetCurrentFrameIndex();
    m_sceneParam.frameIndex = frameIndex;

    m_sceneCB.Write(frameIndex, &m_sceneParam, sizeof(m_sceneParam));
    auto sceneConstantBuffer = m_sceneCB.Get(frameIndex);

    ID3D12DescriptorHeap* descriptorHeaps[] = {
    m_device->GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV).Get(),
    };
    m_commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    // スキニング用データ構造更新.
    if (m_actorChara->IsSkinned()) {
        // 行列データを GPU のバッファに書き込む.
        m_actorChara->ApplyTransform();

        auto model = m_actorChara->GetModel();
        auto srcPosition = model->GetPositionBuffer();
        auto srcNormal = model->GetNormalBuffer();
        auto srcJointWeights = model->GetJointWeightsBuffer();
        auto srcJointIndices = model->GetJointIndicesBuffer();
        auto srcJointMatrices = m_actorChara->GetJointMatrixDescriptor();
        auto dstPosition = m_actorChara->GetDestPositionBuffer();
        auto dstNormal = m_actorChara->GetDestNormalBuffer();
        // 
        m_commandList->SetComputeRootSignature(m_rsSkinningCompute.Get());
        m_commandList->SetPipelineState(m_psoSkinCompute.Get());
        m_commandList->SetComputeRootShaderResourceView(0, srcPosition->GetGPUVirtualAddress());
        m_commandList->SetComputeRootShaderResourceView(1, srcNormal->GetGPUVirtualAddress());
        m_commandList->SetComputeRootShaderResourceView(2, srcJointWeights->GetGPUVirtualAddress());
        m_commandList->SetComputeRootShaderResourceView(3, srcJointIndices->GetGPUVirtualAddress());
        m_commandList->SetComputeRootDescriptorTable(4, srcJointMatrices.hGpu);

        m_commandList->SetComputeRootUnorderedAccessView(5, dstPosition->GetGPUVirtualAddress());
        m_commandList->SetComputeRootUnorderedAccessView(6, dstNormal->GetGPUVirtualAddress());

        auto vertexCount = m_actorChara->GetSkinVertexCount();
        m_commandList->Dispatch(vertexCount, 1, 1);

        // バッファ更新のためバリアを設定する.
        CD3DX12_RESOURCE_BARRIER barriers[] = {
            CD3DX12_RESOURCE_BARRIER::UAV(dstPosition.Get()),
            CD3DX12_RESOURCE_BARRIER::UAV(dstNormal.Get())
        };
        m_commandList->ResourceBarrier(_countof(barriers), barriers);
        m_actorChara->UpdateBLAS(m_commandList);
    }

    // 非スキニングモデルも行列更新を行いBLASに渡す行列を準備する.
    for (auto& model : { m_actorTable, m_actorPot1, m_actorPot2 }) {
        model->UpdateMatrices();
        model->ApplyTransform();
        model->UpdateBLAS(m_commandList);
    }

    // 各モデルの現在の状態で TLAS を更新する.
    UpdateSceneTLAS(frameIndex);

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

    m_frameCount++;
}

void ModelScene::RenderHUD()
{
    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_commandList.Get());
}

void ModelScene::CreateSceneTLAS()
{
    // オブジェクトを配置.
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
    auto asb = util::CreateAccelerationStructure(m_device, asDesc);
    m_tlas = asb.asbuffer;
    m_tlasUpdate = asb.update;

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
    asDesc.ScratchAccelerationStructureData = asb.scratch->GetGPUVirtualAddress();

    // コマンドリストに積む.
    auto command = m_device->CreateCommandList();
    command->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);
    auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(m_tlas.Get());
    command->ResourceBarrier(1, &barrier);
    command->Close();
    m_device->ExecuteCommandList(command);
    m_device->WaitForIdleGpu();
}

void ModelScene::UpdateSceneTLAS(UINT frameIndex)
{
    // オブジェクトを配置.
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

void ModelScene::PrepareModels()
{
    if (m_modelTable.LoadFromGltf(L"table.glb", m_device) == false) {
        throw std::runtime_error("Failed load model data.");
    }

    if (m_modelPot.LoadFromGltf(L"teapot.glb", m_device) == false) {
        throw std::runtime_error("Failed load model data.");
    }
    if (m_modelChara.LoadFromGltf(L"alicia.glb", m_device) == false) {
        throw std::runtime_error("Failed load model data.");
    }
    m_actorTable = m_modelTable.Create(m_device);
    m_actorPot1 = m_modelPot.Create(m_device);
    m_actorPot2 = m_modelPot.Create(m_device);
    m_actorChara = m_modelChara.Create(m_device);

    auto assignFunc = [](auto actor, const wchar_t* hitgroup) {
        for (UINT i = 0; i < actor->GetMaterialCount(); ++i) {
            auto material = actor->GetMaterial(i);
            material->SetHitgroup(hitgroup);
        }
    };

    // 各描画用オブジェクト(Actor)で使用するヒットグループを割り当てる.
    assignFunc(m_actorTable, AppHitGroups::StaticModel);
    assignFunc(m_actorPot1, AppHitGroups::StaticModel);
    assignFunc(m_actorPot2, AppHitGroups::StaticModel);
    assignFunc(m_actorChara, AppHitGroups::CharaModel);
}

void ModelScene::DeployObjects(std::vector<D3D12_RAYTRACING_INSTANCE_DESC>& instanceDescs)
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
    auto index = m_device->GetCurrentFrameIndex();
    UINT instanceHitGroupOffset = 1;
    {
        auto mtxTransform = m_actorTable->GetWorldMatrix();
        D3D12_RAYTRACING_INSTANCE_DESC desc{};
        XMStoreFloat3x4(
            reinterpret_cast<XMFLOAT3X4*>(&desc.Transform), mtxTransform);
        desc.InstanceID = 0;
        desc.InstanceMask = 0xFF;
        desc.InstanceContributionToHitGroupIndex = instanceHitGroupOffset;
        desc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
        desc.AccelerationStructure = m_actorTable->GetBLAS()->GetGPUVirtualAddress();
        instanceDescs.push_back(desc);

        instanceHitGroupOffset += m_actorTable->GetMeshCountAll();
    }
    {
        auto mtxTransform = m_actorPot1->GetWorldMatrix();
        D3D12_RAYTRACING_INSTANCE_DESC desc{};
        XMStoreFloat3x4(
            reinterpret_cast<XMFLOAT3X4*>(&desc.Transform), mtxTransform);
        desc.InstanceID = 0;
        desc.InstanceMask = 0xFF;
        desc.InstanceContributionToHitGroupIndex = instanceHitGroupOffset;
        desc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
        desc.AccelerationStructure = m_actorPot1->GetBLAS()->GetGPUVirtualAddress();
        instanceDescs.push_back(desc);

        instanceHitGroupOffset += m_actorPot1->GetMeshCountAll();
    }
    {
        auto mtxTransform = m_actorPot2->GetWorldMatrix();
        D3D12_RAYTRACING_INSTANCE_DESC desc{};
        XMStoreFloat3x4(
            reinterpret_cast<XMFLOAT3X4*>(&desc.Transform), mtxTransform);
        desc.InstanceID = 0;
        desc.InstanceMask = 0xFF;
        desc.InstanceContributionToHitGroupIndex = instanceHitGroupOffset;
        desc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
        desc.AccelerationStructure = m_actorPot2->GetBLAS()->GetGPUVirtualAddress();
        instanceDescs.push_back(desc);

        instanceHitGroupOffset += m_actorPot2->GetMeshCountAll();
    }
    {
        auto mtxTransform = m_actorChara->GetWorldMatrix();
        D3D12_RAYTRACING_INSTANCE_DESC desc{};
        XMStoreFloat3x4(
            reinterpret_cast<XMFLOAT3X4*>(&desc.Transform), mtxTransform);
        desc.InstanceID = 0;
        desc.InstanceMask = 0xFF;
        desc.InstanceContributionToHitGroupIndex = instanceHitGroupOffset;
        desc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
        desc.AccelerationStructure = m_actorChara->GetBLAS()->GetGPUVirtualAddress();
        instanceDescs.push_back(desc);

        instanceHitGroupOffset += m_actorChara->GetMeshCountAll();
    }
}

void ModelScene::OnMouseDown(MouseButton button, int x, int y)
{
    float fdx = float(x) / GetWidth();
    float fdy = float(y) / GetHeight();
    m_camera.OnMouseButtonDown(int(button), fdx, fdy);
}

void ModelScene::OnMouseUp(MouseButton button, int x, int y)
{
    m_camera.OnMouseButtonUp();
}

void ModelScene::OnMouseMove(int dx, int dy)
{
    float fdx = float(dx) / GetWidth();
    float fdy = float(dy) / GetHeight();
    m_camera.OnMouseMove(-fdx, fdy);
}

void ModelScene::CreateSceneObjects()
{  
    const auto flags = D3D12_RESOURCE_FLAG_NONE;
    const auto heapType = D3D12_HEAP_TYPE_DEFAULT;
    auto d3d12Device = m_device->GetDevice();

    std::vector<util::primitive::VertexPN> vertices;
    std::vector<UINT> indices;
    util::primitive::GetPlane(vertices, indices, 10.0f);

    auto vstride = UINT(sizeof(util::primitive::VertexPN));
    auto istride = UINT(sizeof(UINT));
    auto vbPlaneSize = vstride * vertices.size();
    auto ibPlaneSize = istride * indices.size();
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
        m_meshPlane.vertexBuffer.Get(),
        m_meshPlane.vertexCount, 0, vstride);
    m_meshPlane.descriptorIB = util::CreateStructuredSRV(
        m_device,
        m_meshPlane.indexBuffer.Get(),
        m_meshPlane.indexCount, 0, istride);
    m_meshPlane.shaderName = L"hgFloor";
}

void ModelScene::CreateSceneBLAS()
{
    D3D12_RAYTRACING_GEOMETRY_DESC planeGeomDesc{};
    planeGeomDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    planeGeomDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    {
        auto& triangles = planeGeomDesc.Triangles;
        triangles.VertexBuffer.StartAddress = m_meshPlane.vertexBuffer->GetGPUVirtualAddress();
        triangles.VertexBuffer.StrideInBytes = sizeof(util::primitive::VertexPN);
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

    // BLAS のバッファに UAV バリアを設定する.
    std::vector<CD3DX12_RESOURCE_BARRIER> uavBarriers;
    uavBarriers.emplace_back(CD3DX12_RESOURCE_BARRIER::UAV(planeASB.asbuffer.Get()));

    command->ResourceBarrier(UINT(uavBarriers.size()), uavBarriers.data());
    command->Close();

    // コマンドを実行して BLAS の構築を完了させる.
    m_device->ExecuteCommandList(command);

    // この先は BLAS のバッファのみ使うのでメンバ変数に代入しておく.
    m_meshPlane.blas = planeASB.asbuffer;

    // 本関数を抜けるとスクラッチバッファ解放となるため待機.
    m_device->WaitForIdleGpu();
}

void ModelScene::CreateSkinningPipeline()
{
    // スキニング計算用.
    std::vector<char> shader;
    util::LoadFile(shader, L"SkinningCompute.cso");

    // ComputeShader 用のルートシグネチャ生成.
    using RootType = util::RootSignatureHelper::RootType;
    using RangeType = util::RootSignatureHelper::RangeType;
    util::RootSignatureHelper rshelper;
    rshelper.Add(RootType::SRV, 0); // t0: Position (in)
    rshelper.Add(RootType::SRV, 1); // t1: Normal (in)
    rshelper.Add(RootType::SRV, 2); // t2: Weights (in)
    rshelper.Add(RootType::SRV, 3); // t3: Indices (in)
    rshelper.Add(RangeType::SRV, 4); // t4: 行列のバッファ(in).
    rshelper.Add(RootType::UAV, 0); // u0: Position (out)
    rshelper.Add(RootType::UAV, 1); // u1: Normal (out)
    m_rsSkinningCompute = rshelper.Create(m_device, false, L"csRootSignature");
    
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc{};
    pipelineDesc.CS.BytecodeLength = shader.size();
    pipelineDesc.CS.pShaderBytecode = shader.data();
    pipelineDesc.pRootSignature = m_rsSkinningCompute.Get();
    m_device->GetDevice()->CreateComputePipelineState(
        &pipelineDesc, IID_PPV_ARGS(&m_psoSkinCompute)
    );
}

void ModelScene::CreateStateObject()
{
    // シェーダーファイルの読み込み.
    struct ShaderFileInfo {
        std::vector<char> binary;
        D3D12_SHADER_BYTECODE code;
    };

    const auto RayGenShader = L"raygen.dxlib";
    const auto MissShader = L"miss.dxlib";
    const auto FloorClosestHitShader = L"chsFloor.dxlib";
    const auto ModelClosestHitShader = L"chsModel.dxlib";

    std::unordered_map<std::wstring, ShaderFileInfo> shaders;
    const auto shaderFiles = {
        RayGenShader, MissShader,
        FloorClosestHitShader,
        ModelClosestHitShader,
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

    auto dxilChsModel = subobjects.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    dxilChsModel->SetDXILLibrary(&shaders[ModelClosestHitShader].code);
    dxilChsModel->DefineExport(L"mainModelCHS");
    dxilChsModel->DefineExport(L"mainModelCharaCHS");

    // ヒットグループの設定(床に対する).
    auto hitgroupFloor = subobjects.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
    hitgroupFloor->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);
    hitgroupFloor->SetClosestHitShaderImport(L"mainFloorCHS");
    hitgroupFloor->SetHitGroupExport(AppHitGroups::Floor);

    // ヒットグループの設定(モデル).
    auto hitgroupModel = subobjects.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
    hitgroupModel->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);
    hitgroupModel->SetClosestHitShaderImport(L"mainModelCHS");
    hitgroupModel->SetHitGroupExport(AppHitGroups::StaticModel);

    // ヒットグループの設定(キャラクターモデル).
    auto hitgroupCharaModel = subobjects.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
    hitgroupCharaModel->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);
    hitgroupCharaModel->SetClosestHitShaderImport(L"mainModelCharaCHS");
    hitgroupCharaModel->SetHitGroupExport(AppHitGroups::CharaModel);

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

    //    モデル用.
    auto rsModel2 = subobjects.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
    rsModel2->SetRootSignature(m_rsModel.Get());
    auto lrsAssocModel2 = subobjects.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
    lrsAssocModel2->AddExport(AppHitGroups::StaticModel);
    lrsAssocModel2->SetSubobjectToAssociate(*rsModel2);

    //    キャラクターモデル用.
    auto rsCharaModel = subobjects.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
    rsCharaModel->SetRootSignature(m_rsModel.Get());
    auto lrsAssocCharaModel = subobjects.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
    lrsAssocCharaModel->AddExport(AppHitGroups::CharaModel);
    lrsAssocCharaModel->SetSubobjectToAssociate(*rsCharaModel);

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

void ModelScene::CreateResultBuffer()
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

void ModelScene::CreateRootSignatureGlobal()
{
    using RootType = util::RootSignatureHelper::RootType;
    using RangeType = util::RootSignatureHelper::RangeType;
    util::RootSignatureHelper rshelper;
    rshelper.Add(RangeType::SRV, 0); // t0, TLAS
    rshelper.Add(RootType::CBV, 0); // b0, SceneCB

    rshelper.AddStaticSampler(0); // s0, sampler
    m_rootSignatureGlobal = rshelper.Create(m_device, false, L"RootSignatureGlobal");
}

void ModelScene::CreateRayGenLocalRootSignature()
{
    using RootType = util::RootSignatureHelper::RootType;
    using RangeType = util::RootSignatureHelper::RangeType;
    util::RootSignatureHelper rshelper;
    rshelper.Add(RangeType::UAV, 0); // u0, Range
    const auto isLocal = true;
    m_rsRGS = rshelper.Create(m_device, isLocal, L"lrsRayGen");
}

void ModelScene::CreateModelLocalRootSignature()
{
    using RootType = util::RootSignatureHelper::RootType;
    using RangeType = util::RootSignatureHelper::RangeType;
    const UINT spaceGeom = 1;
    const UINT spaceMate = 2;

    util::RootSignatureHelper rshelper;

    rshelper.Add(RangeType::SRV, 0, spaceGeom); // t0, インデックスバッファ.
    rshelper.Add(RangeType::SRV, 1, spaceGeom); // t1, 頂点位置.
    rshelper.Add(RangeType::SRV, 2, spaceGeom); // t2, 頂点法線.
    rshelper.Add(RangeType::SRV, 3, spaceGeom); // t3, 頂点UV.
    rshelper.Add(RangeType::SRV, 0, spaceMate); // t0, ディフューズテクスチャ.
    rshelper.Add(RootType::CBV, 0, spaceMate); // b0, メッシュ描画用パラメータ.
    rshelper.Add(RangeType::SRV, 4, spaceGeom); // t4,
    const auto isLocal = true;
    m_rsModel = rshelper.Create(m_device, isLocal, L"lrsModel");
}


void ModelScene::CreateFloorLocalRootSignature()
{
    using RootType = util::RootSignatureHelper::RootType;
    using RangeType = util::RootSignatureHelper::RangeType;
    const UINT spaceGeom = 1;

    util::RootSignatureHelper rshelper;
    rshelper.Add(RangeType::SRV, 0, spaceGeom); // t0, インデックスバッファ.
    rshelper.Add(RangeType::SRV, 1, spaceGeom); // t1, 頂点バッファ.
    const auto isLocal = true;
    m_rsFloor = rshelper.Create(m_device, isLocal, L"lrsFloor");
}

void ModelScene::CreateShaderTable()
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
    hitgroupRecordSize += sizeof(D3D12_GPU_DESCRIPTOR_HANDLE); // VB(POS)
    hitgroupRecordSize += sizeof(D3D12_GPU_DESCRIPTOR_HANDLE); // VB(NRM)
    hitgroupRecordSize += sizeof(D3D12_GPU_DESCRIPTOR_HANDLE); // VB(TEX)
    hitgroupRecordSize += sizeof(D3D12_GPU_DESCRIPTOR_HANDLE); // CB
    hitgroupRecordSize = util::RoundUp(hitgroupRecordSize, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);

    // Missシェーダーではローカルルートシグネチャ未使用.
    UINT missRecordSize = 0;
    missRecordSize += D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    missRecordSize = util::RoundUp(missRecordSize, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);

    UINT hitGroupCount = 0;
    hitGroupCount += 1; // 床.

    // モデルの中に持っている BLAS 内のメッシュ数も考慮してカウントをする.
    for (const auto& model : { m_actorTable, /*m_actorChara,*/ m_actorPot1, m_actorPot2 }) {
        for (UINT groupIndex = 0; groupIndex < model->GetMeshGroupCount(); ++groupIndex) {
            hitGroupCount += model->GetMeshCount(groupIndex);
        }
    }

    // シェーダーテーブルのサイズを求める.
    UINT raygenSize = 1 * raygenRecordSize; // 今1つの Ray Generation シェーダー.
    UINT missSize = 2 * missRecordSize;  // 通常描画時とシャドウで２つの miss シェーダー.
    UINT hitGroupSize = hitGroupCount * hitgroupRecordSize;

    // 各テーブルの開始位置にアライメント制約があるので調整する.
    auto tableAlign = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
    auto raygenRegion = util::RoundUp(raygenSize, tableAlign);
    auto missRegion = util::RoundUp(missSize, tableAlign);
    auto hitgroupRegion = util::RoundUp(hitGroupSize, tableAlign);

    // シェーダーテーブル確保.
    auto tableSize = raygenRegion + missRegion + hitgroupRegion;
    m_shaderTable = util::CreateBuffer(m_device, tableSize, nullptr, D3D12_HEAP_TYPE_UPLOAD);

    ComPtr<ID3D12StateObjectProperties> rtsoProps;
    m_rtState.As(&rtsoProps);


    // 各シェーダーレコードを書き込んでいく.
    void* mapped = nullptr;
    m_shaderTable->Map(0, nullptr, &mapped);
    uint8_t* pStart = static_cast<uint8_t*>(mapped);

    // RayGeneration 用のシェーダーレコードを書き込み.
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

    // Miss Shader 用のシェーダーレコードを書き込み.
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
        recordStart = WriteHitgroupShaderRecord(recordStart, m_meshPlane, hitgroupRecordSize);

        // 各モデルのエントリ分を書き込む.
        recordStart = WriteHitgroupShaderRecord(recordStart, m_actorTable, hitgroupRecordSize);
        recordStart = WriteHitgroupShaderRecord(recordStart, m_actorPot1, hitgroupRecordSize);
        recordStart = WriteHitgroupShaderRecord(recordStart, m_actorPot2, hitgroupRecordSize);
        recordStart = WriteHitgroupShaderRecord(recordStart, m_actorChara, hitgroupRecordSize);
    }

    m_shaderTable->Unmap(0, nullptr);

    // DispatchRays のために情報をセットしておく.
    auto& dispatchRayDesc = m_dispatchRayDesc;
    auto startAddress = m_shaderTable->GetGPUVirtualAddress();
    auto& shaderRecordRG = dispatchRayDesc.RayGenerationShaderRecord;
    shaderRecordRG.StartAddress = startAddress;
    shaderRecordRG.SizeInBytes = raygenSize;
    startAddress += raygenRegion;

    auto& shaderRecordMS = dispatchRayDesc.MissShaderTable;
    shaderRecordMS.StartAddress = startAddress;
    shaderRecordMS.SizeInBytes = missSize;
    shaderRecordMS.StrideInBytes = missRecordSize;
    startAddress += missRegion;

    auto& shaderRecordHG = dispatchRayDesc.HitGroupTable;
    shaderRecordHG.StartAddress = startAddress;
    shaderRecordHG.SizeInBytes = hitGroupSize;
    shaderRecordHG.StrideInBytes = hitgroupRecordSize;
    startAddress += hitgroupRegion;

    dispatchRayDesc.Width = GetWidth();
    dispatchRayDesc.Height = GetHeight();
    dispatchRayDesc.Depth = 1;

}

uint8_t* ModelScene::WriteHitgroupShaderRecord(uint8_t* dst, const PolygonMesh& mesh, UINT hgEntrySize) 
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

uint8_t* ModelScene::WriteHitgroupShaderRecord(uint8_t* dst, std::shared_ptr<util::DxrModelActor> actor, UINT hgRecordSize)
{
    ComPtr<ID3D12StateObjectProperties> rtsoProps;
    m_rtState.As(&rtsoProps);

    for (UINT group = 0; group < actor->GetMeshGroupCount(); ++group) {
        for (UINT meshIndex = 0; meshIndex < actor->GetMeshCount(group); ++meshIndex) {
            const auto& mesh = actor->GetMesh(group, meshIndex);
            auto material = mesh.GetMaterial();
            auto shader = material->GetHitgroup();
            auto id = rtsoProps->GetShaderIdentifier(shader.c_str());
            if (id == nullptr) {
                throw std::logic_error("Not found ShaderIdentifier");
            }
            auto recordStart = dst;
            dst += util::WriteShaderIdentifier(dst, id);
            dst += util::WriteGPUDescriptor(dst, mesh.GetIndexBuffer());
            dst += util::WriteGPUDescriptor(dst, mesh.GetPosition());
            dst += util::WriteGPUDescriptor(dst, mesh.GetNormal());
            dst += util::WriteGPUDescriptor(dst, mesh.GetTexcoord());
            dst += util::WriteGPUDescriptor(dst, material->GetTextureDescriptor());
            dst += util::WriteGpuResourceAddr(dst, mesh.GetMeshParametersCB());
            dst += util::WriteGPUDescriptor(dst, actor->GetBLASMatrixDescriptor());

            dst = recordStart + hgRecordSize;
        }
    }
    return dst;
}
