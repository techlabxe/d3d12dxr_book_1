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

    // �V�[���ɔz�u����I�u�W�F�N�g�̐���.
    CreateSceneObjects();

    // ���ƃL���[�u�� BLAS ���\�z����.
    CreateSceneBLAS();

    CreateSceneTLAS();

    // �O���[�o�� Root Signature ��p��.
    CreateRootSignatureGlobal();

    // ���[�J�� Root Signature ��p��.
    CreateLocalRootSignatureRayGen();
    CreateFloorLocalRootSignature();
    CreateSphereLocalRootSignature();

    // �R���p�C���ς݃V�F�[�_�[���X�e�[�g�I�u�W�F�N�g��p��.
    CreateStateObject();

    // �V�[���p�R���X�^���g�o�b�t�@�̊m��.
    m_sceneCB.Initialize(m_device, sizeof(SceneParam), L"sceneCB");

    // ���C�g���[�V���O���ʊi�[�̂��߂̃o�b�t�@(UAV)��p��.
    CreateResultBuffer();

    // �`��Ŏg�p���� Shader Table ��p��.
    CreateShaderTable();

    // �R�}���h���X�g�p��.
    //  �`�掞�ɐςނ̂ł����ł̓N���[�Y���Ă���.
    m_commandList = m_device->CreateCommandList();
    m_commandList->Close();

    // ImGui ���� DX12 �ݒ�.
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

    m_sceneParam.flags.x = 0; // ���s���� or �|�C���g���C�g�ɂ��e�`��.
    m_sceneParam.flags.y = 1; // �|�C���g���C�g�ʒu�̕`��ON
    m_sceneParam.shadowRayCount = 1;

    XMFLOAT3 lightDir{ 0.25f,-0.5f, -0.6f }; // ���[���h���W�n�ł̌����̌���.
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

    // �_�����̈ʒu���X�V.
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

    // ���C�g���[�V���O���ʃo�b�t�@�� UAV ��Ԃ�.
    auto barrierToUAV = CD3DX12_RESOURCE_BARRIER::Transition(
        m_dxrOutput.Get(),
        D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS
    );
    m_commandList->ResourceBarrier(1, &barrierToUAV);

    m_commandList->SetPipelineState1(m_rtState.Get());
    m_commandList->DispatchRays(&m_dispatchRayDesc);

    // ���C�g���[�V���O���ʂ��o�b�N�o�b�t�@�փR�s�[����.
    // �o���A��ݒ肵�e���\�[�X�̏�Ԃ�J�ڂ�����.
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

    // �o�b�N�o�b�t�@�������_�[�^�[�Q�b�g��Ԃɂ��� UI ���������߂�悤�ɂ���.
    auto barrierToRT = CD3DX12_RESOURCE_BARRIER::Transition(
        renderTarget.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET
    );
    m_commandList->ResourceBarrier(1, &barrierToRT);
    auto rtv = m_device->GetRenderTargetView();
    auto viewport = m_device->GetDefaultViewport();
    m_commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    m_commandList->RSSetViewports(1, &viewport);

    // ImGui �̕`��.
    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_commandList.Get());

    // Present �\�Ȃ悤�Ƀo���A���Z�b�g.
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

    // �����ʂ𐶐�����.
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
    //   �f�B�X�N���v�^�̏���.
    m_meshPlane.descriptorIB = util::CreateStructuredSRV(
        m_device,
        m_meshPlane.indexBuffer,
        m_meshPlane.indexCount, 0, istride);
    m_meshPlane.descriptorVB = util::CreateStructuredSRV(
        m_device, 
        m_meshPlane.vertexBuffer, 
        m_meshPlane.vertexCount, 0, vstride);
    // �g�p����q�b�g�O���[�v��ݒ�.
    m_meshPlane.shaderName = AppHitGroups::Floor;
    vertices.clear();
    indices.clear();
    
    // �X�t�B�A�𐶐�����.
    //  �`��(BLAS��|���S���f�[�^)�͋��ʂƂ��ĂP���Q�Ƃ���`�Ƃ���.
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
    //  �f�B�X�N���v�^�̏���.
    m_meshSphere.descriptorIB = util::CreateStructuredSRV(
        m_device,
        m_meshSphere.indexBuffer,
        m_meshSphere.indexCount, 0, istride);
    m_meshSphere.descriptorVB = util::CreateStructuredSRV(
        m_device,
        m_meshSphere.vertexBuffer,
        m_meshSphere.vertexCount, 0, vstride);
    // �g�p����q�b�g�O���[�v��ݒ�.
    m_meshSphere.shaderName = AppHitGroups::Sphere;
    vertices.clear();
    indices.clear();

    // ���C�g�ʒu�̃X�t�B�A��ʍ쐬.
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
    //  �f�B�X�N���v�^�̏���.
    m_meshLightSphere.descriptorIB = util::CreateStructuredSRV(
        m_device,
        m_meshLightSphere.indexBuffer,
        m_meshLightSphere.indexCount, 0, istride);
    m_meshLightSphere.descriptorVB = util::CreateStructuredSRV(
        m_device,
        m_meshLightSphere.vertexBuffer,
        m_meshLightSphere.vertexCount, 0, vstride);
    // �g�p����q�b�g�O���[�v��ݒ�.
    m_meshLightSphere.shaderName = AppHitGroups::Light;
    vertices.clear();
    indices.clear();
    
    //// �X�t�B�A��K���ɔz�u����.
    std::mt19937 mt;
    std::uniform_real_distribution rnd(.5f, 0.75f);
    std::uniform_int_distribution rnd2(-9, 9);
    for (auto& sphere : m_spheres) {
        float x = rnd2(mt) + 0.5f;
        float z = rnd2(mt) + 0.5f;
        sphere.mtxWorld = XMMatrixTranslation(x, 0.5, z);
    }

    // �|�C���g���C�g�ʒu�p.
    m_lightPos = XMFLOAT3(0, 2.5, 2);
    m_pointLight.mtxWorld = XMMatrixTranslationFromVector(XMLoadFloat3(&m_lightPos));
}

void ShadowScene::CreateSceneBLAS()
{
    auto d3d12Device = m_device->GetDevice();
    auto floorGeomDesc = GetGeometryDescFromPolygonMesh(m_meshPlane);
    auto sphereGeomDesc = GetGeometryDescFromPolygonMesh(m_meshSphere);
    auto lightSphereGeomDesc = GetGeometryDescFromPolygonMesh(m_meshLightSphere);

    // BLAS �̍쐬
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc{};
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs = asDesc.Inputs;
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;

    // BLAS ���\�z���邽�߂̃o�b�t�@������ (Plane).
    inputs.NumDescs = 1;
    inputs.pGeometryDescs = &floorGeomDesc;
    //util::AccelerationStructureBuffers
    auto planeASB = util::CreateAccelerationStructure(m_device, asDesc);
    planeASB.asbuffer->SetName(L"Plane-Blas");
    asDesc.ScratchAccelerationStructureData = planeASB.scratch->GetGPUVirtualAddress();
    asDesc.DestAccelerationStructureData = planeASB.asbuffer->GetGPUVirtualAddress();

    // �R�}���h���X�g�ɐς�.
    auto command = m_device->CreateCommandList();
    command->BuildRaytracingAccelerationStructure(
        &asDesc, 0, nullptr);

    // BLAS ���\�z���邽�߂̃o�b�t�@������ (Sphere).
    inputs.NumDescs = 1;
    inputs.pGeometryDescs = &sphereGeomDesc;
    auto sphereASB = util::CreateAccelerationStructure(m_device, asDesc);
    sphereASB.asbuffer->SetName(L"Sphere-Blas");
    asDesc.ScratchAccelerationStructureData = sphereASB.scratch->GetGPUVirtualAddress();
    asDesc.DestAccelerationStructureData = sphereASB.asbuffer->GetGPUVirtualAddress();

    // �R�}���h���X�g�ɐς�.
    command->BuildRaytracingAccelerationStructure(
        &asDesc, 0, nullptr);

    // BLAS ���\�z���邽�߂̃o�b�t�@������ (LightSphere).
    inputs.NumDescs = 1;
    inputs.pGeometryDescs = &lightSphereGeomDesc;
    auto lightSphereASB = util::CreateAccelerationStructure(m_device, asDesc);
    lightSphereASB.asbuffer->SetName(L"Light-Blas");
    asDesc.ScratchAccelerationStructureData = lightSphereASB.scratch->GetGPUVirtualAddress();
    asDesc.DestAccelerationStructureData = lightSphereASB.asbuffer->GetGPUVirtualAddress();

    // �R�}���h���X�g�ɐς�.
    command->BuildRaytracingAccelerationStructure(
        &asDesc, 0, nullptr);

    // BLAS �̃o�b�t�@�� UAV �o���A��ݒ肷��.
    std::vector<CD3DX12_RESOURCE_BARRIER> uavBarriers;
    uavBarriers.emplace_back(CD3DX12_RESOURCE_BARRIER::UAV(planeASB.asbuffer.Get()));
    uavBarriers.emplace_back(CD3DX12_RESOURCE_BARRIER::UAV(sphereASB.asbuffer.Get()));
    uavBarriers.emplace_back(CD3DX12_RESOURCE_BARRIER::UAV(lightSphereASB.asbuffer.Get()));

    command->ResourceBarrier(UINT(uavBarriers.size()), uavBarriers.data());
    command->Close();

    // �R�}���h�����s���� BLAS �̍\�z������������.
    m_device->ExecuteCommandList(command);

    // ���̐�� BLAS �̃o�b�t�@�̂ݎg���̂Ń����o�ϐ��ɑ�����Ă���.
    m_meshPlane.blas = planeASB.asbuffer;
    m_meshSphere.blas = sphereASB.asbuffer;
    m_meshLightSphere.blas = lightSphereASB.asbuffer;

    // �{�֐��𔲂���ƃX�N���b�`�o�b�t�@����ƂȂ邽�ߑҋ@.
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
    // �X�V���������邽�߂ɋ��t���O��ݒ肷��.
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;

    // AS �𐶐�����.
    auto sceneASB = util::CreateAccelerationStructure(m_device, asDesc);
    sceneASB.asbuffer->SetName(L"Scene-Tlas");
    m_tlas = sceneASB.asbuffer;
    m_tlasUpdate = sceneASB.update;

    // �f�B�X�N���v�^������.
    m_tlasDescriptor = m_device->AllocateDescriptor();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.RaytracingAccelerationStructure.Location = m_tlas->GetGPUVirtualAddress();
    m_device->GetDevice()->CreateShaderResourceView(
        nullptr, &srvDesc, m_tlasDescriptor.hCpu);

    // TLAS �\�z�̂��߂̃o�b�t�@���Z�b�g.
    asDesc.DestAccelerationStructureData = sceneASB.asbuffer->GetGPUVirtualAddress();
    asDesc.ScratchAccelerationStructureData = sceneASB.scratch->GetGPUVirtualAddress();

    // �R�}���h���X�g�ɐς�.
    auto command = m_device->CreateCommandList();
    command->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);

    // TLAS �ɑ΂��Ă� UAV �o���A��ݒ�.
    D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::UAV(
        sceneASB.asbuffer.Get()
    );
    command->ResourceBarrier(1, &barrier);
    command->Close();

    // �R�}���h�����s���� TLAS �̍\�z������������.
    m_device->ExecuteCommandList(command);

    // �{�֐��𔲂���ƃX�N���b�`�o�b�t�@����ƂȂ邽�ߑҋ@.
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

    // TLAS �̍X�V�������s�����߂̃t���O��ݒ肷��.
    inputs.Flags =
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE |
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;

    // �C���v���[�X�X�V�����s����.
    asDesc.SourceAccelerationStructureData = m_tlas->GetGPUVirtualAddress();
    asDesc.DestAccelerationStructureData = m_tlas->GetGPUVirtualAddress();
    asDesc.ScratchAccelerationStructureData = m_tlasUpdate->GetGPUVirtualAddress();

    // �R�}���h���X�g�ɐς�.
    m_commandList->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);
    auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(m_tlas.Get());
    m_commandList->ResourceBarrier(1, &barrier);
}

void ShadowScene::CreateStateObject()
{
    // �V�F�[�_�[�t�@�C���̓ǂݍ���.
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

    // �V�F�[�_�[�̊e�֐��G���g���̓o�^.
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

    // �q�b�g�O���[�v�̐ݒ�(���ɑ΂���).
    auto hitgroupFloor = subobjects.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
    hitgroupFloor->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);
    hitgroupFloor->SetClosestHitShaderImport(L"mainFloorCHS");
    hitgroupFloor->SetHitGroupExport(AppHitGroups::Floor);

    // �q�b�g�O���[�v�̐ݒ�(���C�g).
    auto hitgroupLight = subobjects.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
    hitgroupLight->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);
    hitgroupLight->SetClosestHitShaderImport(L"mainLightCHS");
    hitgroupLight->SetHitGroupExport(AppHitGroups::Light);

    // �q�b�g�O���[�v�̐ݒ�(�X�t�B�A).
    auto hitgroupSphere = subobjects.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
    hitgroupSphere->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);
    hitgroupSphere->SetClosestHitShaderImport(L"mainSphereCHS");
    hitgroupSphere->SetHitGroupExport(AppHitGroups::Sphere);

    // �O���[�o�� Root Signature �ݒ�.
    auto rootsig = subobjects.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
    rootsig->SetRootSignature(m_rootSignatureGlobal.Get());

    // ���[�J�� Root Signature �ݒ�.
    auto rsRayGen = subobjects.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
    rsRayGen->SetRootSignature(m_rsRGS.Get());
    auto lrsAssocRGS = subobjects.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
    lrsAssocRGS->AddExport(L"mainRayGen");
    lrsAssocRGS->SetSubobjectToAssociate(*rsRayGen);

    //    ���p.
    auto rsFloor = subobjects.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
    rsFloor->SetRootSignature(m_rsFloor.Get());
    auto lrsAssocFloor = subobjects.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
    lrsAssocFloor->AddExport(AppHitGroups::Floor);
    lrsAssocFloor->SetSubobjectToAssociate(*rsFloor);

    //    ���C�g�p.
    auto rsLight = subobjects.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
    rsLight->SetRootSignature(m_rsSphere.Get());
    auto lrsAssocLight = subobjects.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
    lrsAssocLight->AddExport(AppHitGroups::Light);
    lrsAssocLight->SetSubobjectToAssociate(*rsLight);

    //    �X�t�B�A�p.
    auto rsModel = subobjects.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
    rsModel->SetRootSignature(m_rsSphere.Get());
    auto lrsAssocModel = subobjects.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
    lrsAssocModel->AddExport(AppHitGroups::Sphere);
    lrsAssocModel->SetSubobjectToAssociate(*rsModel);

    // �V�F�[�_�[�ݒ�.
    auto shaderConfig = subobjects.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
    shaderConfig->Config(MaxPayloadSize, MaxAttributeSize);

    // �p�C�v���C���ݒ�.
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

    // �f�B�X�N���v�^�̏���.
    m_outputDescriptor = m_device->AllocateDescriptor();
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    auto device = m_device->GetDevice();
    device->CreateUnorderedAccessView(m_dxrOutput.Get(), nullptr, &uavDesc, m_outputDescriptor.hCpu);
}

void ShadowScene::CreateRootSignatureGlobal()
{
    // [0] : �f�B�X�N���v�^, t0, TLAS �o�b�t�@.
    // [1] : �萔�o�b�t�@, b0, �V�[�����ʒ萔�o�b�t�@.
    util::RootSignatureHelper rshelper;
    rshelper.Add(util::RootSignatureHelper::RangeType::SRV, 0);
    rshelper.Add(util::RootSignatureHelper::RootType::CBV, 0);

    // ���[�g�V�O�l�`����������.
    auto isLocal = false;
    m_rootSignatureGlobal = rshelper.Create(m_device, isLocal, L"RootSignatureGlobal");
}

void ShadowScene::CreateLocalRootSignatureRayGen()
{
    // [0] : �f�B�X�N���v�^, u0, ���C�g���[�V���O���ʏ������ݗp.
    util::RootSignatureHelper rshelper;
    rshelper.Add(util::RootSignatureHelper::RangeType::UAV, 0);

    // ���[�J�����[�g�V�O�l�`������.
    auto isLocal = true;
    m_rsRGS = rshelper.Create(m_device, isLocal, L"lrsRayGen");
}

void ShadowScene::CreateSphereLocalRootSignature()
{
    const UINT regSpace = 1;
    // [0] : �f�B�X�N���v�^, t0(space1), �C���f�b�N�X�o�b�t�@.
    // [1] : �f�B�X�N���v�^, t1(space1), ���_�o�b�t�@.
    util::RootSignatureHelper rshelper;
    rshelper.Add(util::RootSignatureHelper::RangeType::SRV, 0, regSpace);
    rshelper.Add(util::RootSignatureHelper::RangeType::SRV, 1, regSpace);

    // ���[�J�����[�g�V�O�l�`������.
    auto isLocal = true;
    m_rsSphere = rshelper.Create(m_device, isLocal, L"lrsSphere");
}


void ShadowScene::CreateFloorLocalRootSignature()
{
    const UINT regSpace = 1;
    // [0] : �f�B�X�N���v�^, t0(space1), �C���f�b�N�X�o�b�t�@.
    // [1] : �f�B�X�N���v�^, t1(space1), ���_�o�b�t�@.
    util::RootSignatureHelper rshelper;
    rshelper.Add(util::RootSignatureHelper::RangeType::SRV, 0, regSpace);
    rshelper.Add(util::RootSignatureHelper::RangeType::SRV, 1, regSpace);

    // ���[�J�����[�g�V�O�l�`������.
    auto isLocal = true;
    m_rsFloor = rshelper.Create(m_device, isLocal, L"lrsFloor");
}


void ShadowScene::CreateShaderTable()
{
    const auto ShaderRecordAlignment = D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT;
    // RayGeneration �V�F�[�_�[�ł́A Shader Identifier ��
    // ���[�J�����[�g�V�O�l�`���ɂ�� u0 �̃f�B�X�N���v�^���g�p.
    UINT raygenRecordSize = 0;
    raygenRecordSize += D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    raygenRecordSize += sizeof(D3D12_GPU_DESCRIPTOR_HANDLE);
    raygenRecordSize = util::RoundUp(raygenRecordSize, ShaderRecordAlignment);

    // �q�b�g�O���[�v�ł́A Shader Identifier �̑���
    // ���[�J�����[�g�V�O�l�`���ɂ�� VB/IB �̃f�B�X�N���v�^���g�p.
    UINT hitgroupRecordSize = 0;
    hitgroupRecordSize += D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    hitgroupRecordSize += sizeof(D3D12_GPU_DESCRIPTOR_HANDLE);
    hitgroupRecordSize += sizeof(D3D12_GPU_DESCRIPTOR_HANDLE);
    hitgroupRecordSize = util::RoundUp(hitgroupRecordSize, ShaderRecordAlignment);

    // Miss�V�F�[�_�[�ł̓��[�J�����[�g�V�O�l�`�����g�p.
    UINT missRecordSize = 0;
    missRecordSize += D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    missRecordSize = util::RoundUp(missRecordSize, ShaderRecordAlignment);

    // �V�F�[�_�[�e�[�u���̃T�C�Y�����߂�.
    UINT hitgroupCount = 3; // ��,���C�g,�X�t�B�A.
    UINT raygenSize = 1 * raygenRecordSize; // ��1�� Ray Generation �V�F�[�_�[.
    UINT missSize = 2 * missRecordSize;  // �ʏ�`�掞�ƃV���h�E�łQ�� miss �V�F�[�_�[.
    UINT hitGroupSize = hitgroupCount * hitgroupRecordSize;

    // �e�e�[�u�����ƂɃA���C�����g���񂪂���̂Œ�������.
    auto tableAlign = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
    auto raygenRegion = util::RoundUp(raygenSize, tableAlign);
    auto missRegion = util::RoundUp(missSize, tableAlign);
    auto hitgroupRegion = util::RoundUp(hitGroupSize, tableAlign);

    // �V�F�[�_�[�e�[�u���m��.
    auto tableSize = raygenRegion + missRegion + hitgroupRegion;
    m_shaderTable = util::CreateBuffer(
        m_device, tableSize, nullptr,
        D3D12_HEAP_TYPE_UPLOAD,
        D3D12_RESOURCE_FLAG_NONE,
        L"ShaderTable"
    );

    ComPtr<ID3D12StateObjectProperties> rtsoProps;
    m_rtState.As(&rtsoProps);

    // �e�V�F�[�_�[�G���g������������ł���.
    void* mapped = nullptr;
    m_shaderTable->Map(0, nullptr, &mapped);
    uint8_t* pStart = static_cast<uint8_t*>(mapped);

    // RayGeneration �p�̃V�F�[�_�[�G���g������������.
    auto rgsStart = pStart;
    {
        uint8_t* p = pStart;
        auto id = rtsoProps->GetShaderIdentifier(L"mainRayGen");
        if (id == nullptr) {
            throw std::logic_error("Not found ShaderIdentifier");
        }
        p += util::WriteShaderIdentifier(p, id);

        // ���[�J�����[�g�V�O�l�`���� u0 (�o�͐�) ��ݒ肵�Ă��邽��
        // �Ή�����f�B�X�N���v�^����������.
        p += util::WriteGPUDescriptor(p, m_outputDescriptor);
    }

    // Miss Shader �p�̃V�F�[�_�[�G���g������������.
    auto missStart = pStart + raygenRegion;
    {
        auto recordStart = missStart;
        uint8_t* p = missStart;
        auto id = rtsoProps->GetShaderIdentifier(L"mainMiss");
        if (id == nullptr) {
            throw std::logic_error("Not found ShaderIdentifier");
        }
        p += util::WriteShaderIdentifier(p, id);

        // ���̊J�n�ʒu���Z�b�g.
        recordStart += missRecordSize;

        // �V���h�E���� Miss �V�F�[�_�[�̐ݒ�.
        p = recordStart;
        id = rtsoProps->GetShaderIdentifier(L"shadowMiss");
        if (id == nullptr) {
            throw std::logic_error("Not found ShaderIdentifier");
        }
        p += util::WriteShaderIdentifier(p, id);
    }

    // Hit Group �p�̃V�F�[�_�[�G���g������������.
    auto hitgroupStart = pStart + raygenRegion + missRegion;
    {
        auto recordStart = hitgroupStart;

        recordStart = WriteHitgroupShaderEntry(recordStart, m_meshPlane, hitgroupRecordSize);
        recordStart = WriteHitgroupShaderEntry(recordStart, m_meshLightSphere, hitgroupRecordSize);
        recordStart = WriteHitgroupShaderEntry(recordStart, m_meshSphere, hitgroupRecordSize);

        // ���̃e�[�u���������݂̂��߂ɃC���N�������g.
        pStart += hitGroupSize;
    }
    m_shaderTable->Unmap(0, nullptr);


    // DispatchRays �̂��߂ɏ����Z�b�g���Ă���.
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
    // ����z�u.
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

    // ���C�g��z�u.
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

    // �X�t�B�A��z�u.
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
