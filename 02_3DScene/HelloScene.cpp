#include "HelloScene.h"

#include "Win32Application.h"

#include "d3dx12.h"
#include <fstream>

#include "imgui.h"
#include "backends/imgui_impl_dx12.h"
#include "backends/imgui_impl_win32.h"

using namespace DirectX;

// #define USE_UTILS (1)
// #define USE_RUNTIME_SHADER_COMPILE (1)

HelloScene::HelloScene(UINT width, UINT height) : DxrBookFramework(width, height, L"3DScene"),
m_dispatchRayDesc(), m_sceneParam()
{
}

void HelloScene::OnInit()
{
    if (!InitializeGraphicsDevice(Win32Application::GetHWND()))
    {
        throw std::runtime_error("Failed Initialize GraphicsDevice.");
    }

    // ���ƃL���[�u�̃I�u�W�F�N�g�𐶐�����.
    CreateSceneObjects();

    // ���ƃL���[�u�� BLAS ���\�z����.
    CreateSceneBLAS();

    // �V�[���ɔz�u���� TLAS ���\�z����.
    CreateSceneTLAS();

    // �O���[�o�� Root Signature ��p��.
    CreateRootSignatureGlobal();

    // ���[�J�� Root Signature ��p��.
    CreateLocalRootSignatureRayGen();
    CreateLocalRootSignatureCHS();

    // �R���p�C���ς݃V�F�[�_�[���X�e�[�g�I�u�W�F�N�g��p��.
    CreateStateObject();

    // �V�[���p�R���X�^���g�o�b�t�@�̊m��.
    m_sceneCB.Initialize(m_device, sizeof(SceneParam), L"SceneCB");

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
    XMFLOAT3 target(0.0f, 0.0f, 0.0f);
    m_camera.SetLookAt(eyePos, target);

    m_camera.SetPerspective(
        XM_PIDIV4, GetAspect(), 0.1f, 100.0f
    );
}

void HelloScene::OnDestroy()
{
    ImGui_ImplDX12_Shutdown();
    TerminateGraphicsDevice();
}

void HelloScene::OnUpdate()
{
    XMFLOAT3 lightDir{ -0.5f,-1.0f, -0.5f }; // ���[���h���W�n�ł̌����̌���.

    m_sceneParam.mtxView = m_camera.GetViewMatrix();
    m_sceneParam.mtxProj = m_camera.GetProjectionMatrix();
    m_sceneParam.mtxViewInv = XMMatrixInverse(nullptr, m_sceneParam.mtxView);
    m_sceneParam.mtxProjInv = XMMatrixInverse(nullptr, m_sceneParam.mtxProj);

    m_sceneParam.lightColor = XMVectorSet(1.0f, 1.0f, 1.0f, 0.0f);
    m_sceneParam.lightDirection = XMVector3Normalize(XMLoadFloat3(&lightDir));
    m_sceneParam.ambientColor = XMVectorSet(0.2f, 0.2f, 0.2f, 0.0f);

    UpdateHUD();
}

void HelloScene::OnRender()
{
    auto device = m_device->GetDevice();
    auto renderTarget = m_device->GetRenderTarget();
    auto allocator = m_device->GetCurrentCommandAllocator();
    allocator->Reset();
    m_commandList->Reset(allocator.Get(), nullptr);
    auto frameIndex = m_device->GetCurrentFrameIndex();

    m_sceneCB.Write(frameIndex, &m_sceneParam, sizeof(m_sceneParam));
    auto sceneConstantBuffer = m_sceneCB.Get(frameIndex);

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

    // UI �̕`��.
    auto rtv = m_device->GetRenderTargetView();
    auto viewport = m_device->GetDefaultViewport();
    m_commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    m_commandList->RSSetViewports(1, &viewport);
    RenderHUD();

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

void HelloScene::UpdateHUD()
{
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    auto framerate = ImGui::GetIO().Framerate;
    ImGui::Begin("Information");
    ImGui::Text("Framerate %.3f ms", 1000.0f / framerate);

    XMFLOAT3 camPos;
    XMStoreFloat3(&camPos, m_camera.GetPosition());
    ImGui::Text("CameraPos (%.2f, %.2f, %.2f)", camPos.x, camPos.y, camPos.z);
    ImGui::End();
}

void HelloScene::RenderHUD()
{
    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_commandList.Get());
}

void HelloScene::DeployObjects(std::vector<D3D12_RAYTRACING_INSTANCE_DESC>& instanceDescs)
{
    D3D12_RAYTRACING_INSTANCE_DESC templateDesc{};
    templateDesc.InstanceMask = 0xFF;
    templateDesc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;

    // �V�[���ɏ�, Cube 2��ݒu����.
    instanceDescs.resize(3);
    auto& floor = instanceDescs[0];
    auto& cube1 = instanceDescs[1];
    auto& cube2 = instanceDescs[2];

    // ����ݒu.
    floor = templateDesc;
    auto mtx = XMMatrixIdentity();
    XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(&floor.Transform), mtx);
    floor.InstanceContributionToHitGroupIndex = 0;
    floor.AccelerationStructure = m_meshPlane.blas->GetGPUVirtualAddress();

    // Cube(1) �ݒu.
    cube1 = templateDesc;
    mtx = XMMatrixTranslation(-3.0f, 1.0f, 0.0f);
    XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(&cube1.Transform), mtx);
    cube1.InstanceContributionToHitGroupIndex = 1;
    cube1.AccelerationStructure = m_meshCube.blas->GetGPUVirtualAddress();

    // Cube(2) �ݒu.
    cube2 = templateDesc;
    mtx = XMMatrixTranslation(+3.0f, 1.0f, 0.0f);
    XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(&cube2.Transform), mtx);
    cube2.InstanceContributionToHitGroupIndex = 1;
    cube2.AccelerationStructure = m_meshCube.blas->GetGPUVirtualAddress();
}

uint8_t* HelloScene::WriteShaderRecord(uint8_t* dst, const util::PolygonMesh& mesh, UINT recordSize)
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
    // ����̃v���O�����ł͈ȉ��̏����Ńf�B�X�N���v�^���L�^.
    // [0] : �C���f�b�N�X�o�b�t�@
    // [1] : ���_�o�b�t�@
    // �� ���[�J�����[�g�V�O�l�`���̏����ɍ��킹��K�v������.
    dst += util::WriteGPUDescriptor(dst, mesh.descriptorIB);
    dst += util::WriteGPUDescriptor(dst, mesh.descriptorVB);

    dst = entryBegin + recordSize;
    return dst;
}

void HelloScene::OnMouseDown(MouseButton button, int x, int y)
{
    float fdx = float(x) / GetWidth();
    float fdy = float(y) / GetHeight();
    m_camera.OnMouseButtonDown(int(button), fdx, fdy);
}

void HelloScene::OnMouseUp(MouseButton button, int x, int y)
{
    m_camera.OnMouseButtonUp();
}

void HelloScene::OnMouseMove(int dx, int dy)
{
    float fdx = float(dx) / GetWidth();
    float fdy = float(dy) / GetHeight();
    m_camera.OnMouseMove(-fdx, fdy);
}

void HelloScene::CreateSceneObjects()
{
    const auto flags = D3D12_RESOURCE_FLAG_NONE;
    const auto heapType = D3D12_HEAP_TYPE_DEFAULT;

    std::vector<util::primitive::VertexPNC> vertices;
    std::vector<UINT> indices;
    util::primitive::GetPlane(vertices, indices);

    auto vstride = UINT(sizeof(util::primitive::VertexPNC));
    auto istride = UINT(sizeof(UINT));
    auto vbPlaneSize = vstride * vertices.size();
    auto ibPlaneSize = istride * indices.size();

    m_meshPlane.vertexBuffer = util::CreateBuffer(m_device, vbPlaneSize, vertices.data(), heapType, flags, L"planeVB");
    m_meshPlane.indexBuffer = util::CreateBuffer(m_device, ibPlaneSize, indices.data(), heapType, flags, L"planeIB");
    m_meshPlane.vertexCount = UINT(vertices.size());
    m_meshPlane.indexCount = UINT(indices.size());
    m_meshPlane.vertexStride = vstride;

    // �f�B�X�N���v�^�̐���.
    m_meshPlane.descriptorVB = util::CreateStructuredSRV(
        m_device,
        m_meshPlane.vertexBuffer,
        m_meshPlane.vertexCount, 0, vstride);
    m_meshPlane.descriptorIB = util::CreateStructuredSRV(
        m_device,
        m_meshPlane.indexBuffer,
        m_meshPlane.indexCount, 0, istride);
    m_meshPlane.shaderName = AppHitGroups::Object;

    // Cube �̐���.
    vertices.clear(); indices.clear();
    util::primitive::GetColoredCube(vertices, indices);
    auto vbCubeSize = vstride * vertices.size();
    auto ibCubeSize = istride * indices.size();

    m_meshCube.vertexBuffer = util::CreateBuffer(m_device, vbCubeSize, vertices.data(), heapType, flags, L"cubeVB");
    m_meshCube.indexBuffer = util::CreateBuffer(m_device, ibCubeSize, indices.data(), heapType, flags, L"cubeIB");
    m_meshCube.vertexCount = UINT(vertices.size());
    m_meshCube.indexCount = UINT(indices.size());
    m_meshCube.vertexStride = vstride;

    // �f�B�X�N���v�^�̐���.
    m_meshCube.descriptorVB = util::CreateStructuredSRV(
        m_device,
        m_meshCube.vertexBuffer,
        m_meshCube.vertexCount, 0, vstride);
    m_meshCube.descriptorIB = util::CreateStructuredSRV(
        m_device,
        m_meshCube.indexBuffer,
        m_meshCube.indexCount, 0, istride);
    m_meshCube.shaderName = AppHitGroups::Object;
}

void HelloScene::CreateSceneBLAS()
{
    auto planeGetometryDesc = util::GetGeometryDesc(m_meshPlane);
    auto cubeGeometryDesc = util::GetGeometryDesc(m_meshCube);

    // BLAS �̍쐬
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc{};
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs = asDesc.Inputs;
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
    
    // BLAS ���\�z���邽�߂̃o�b�t�@������ (Plane).
    inputs.NumDescs = 1;
    inputs.pGeometryDescs = &planeGetometryDesc;
    //util::AccelerationStructureBuffers
    auto planeASB = util::CreateAccelerationStructure(m_device, asDesc);
    planeASB.asbuffer->SetName(L"Plane-Blas");
    asDesc.ScratchAccelerationStructureData = planeASB.scratch->GetGPUVirtualAddress();
    asDesc.DestAccelerationStructureData = planeASB.asbuffer->GetGPUVirtualAddress();

    // �R�}���h���X�g�ɐς�.
    auto command = m_device->CreateCommandList();
    command->BuildRaytracingAccelerationStructure(
        &asDesc, 0, nullptr);

    // BLAS ���\�z���邽�߂̃o�b�t�@������ (Cube).
    inputs.NumDescs = 1;
    inputs.pGeometryDescs = &cubeGeometryDesc;
    auto cubeASB = util::CreateAccelerationStructure(m_device, asDesc);
    cubeASB.asbuffer->SetName(L"Cube-Blas");
    asDesc.ScratchAccelerationStructureData = cubeASB.scratch->GetGPUVirtualAddress();
    asDesc.DestAccelerationStructureData = cubeASB.asbuffer->GetGPUVirtualAddress();

    // �R�}���h���X�g�ɐς�.
    command->BuildRaytracingAccelerationStructure(
        &asDesc, 0, nullptr);

    // Plane,Cube ���ꂼ��� BLAS �ɂ��ăo���A��ݒ肷��.
    D3D12_RESOURCE_BARRIER uavBarriers[] = {
        CD3DX12_RESOURCE_BARRIER::UAV(planeASB.asbuffer.Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(cubeASB.asbuffer.Get())
    };
    command->ResourceBarrier(_countof(uavBarriers), uavBarriers);
    command->Close();

    // �R�}���h�����s���� BLAS �̍\�z������������.
    m_device->ExecuteCommandList(command);

    // ���̐�� BLAS �̃o�b�t�@�̂ݎg���̂Ń����o�ϐ��ɑ�����Ă���.
    m_meshPlane.blas = planeASB.asbuffer;
    m_meshCube.blas = cubeASB.asbuffer;

    // �{�֐��𔲂���ƃX�N���b�`�o�b�t�@����ƂȂ邽�ߑҋ@.
    m_device->WaitForIdleGpu();
}

void HelloScene::CreateSceneTLAS()
{
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs;
    DeployObjects(instanceDescs);

    // �C���X�^���X�̏����L�^�����o�b�t�@����������.
    size_t sizeOfInstanceDescs = instanceDescs.size() * sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
    auto instanceDescBuffer = util::CreateBuffer(
        m_device,
        sizeOfInstanceDescs,
        instanceDescs.data(),
        D3D12_HEAP_TYPE_UPLOAD,
        D3D12_RESOURCE_FLAG_NONE,
        L"InstanceDescBuffer");

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

    // �R�}���h���X�g�ɐς�.
    auto command = m_device->CreateCommandList();
    command->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);

    // TLAS �ɑ΂��Ă� UAV �o���A��ݒ�.
    D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::UAV(
        sceneASB.asbuffer.Get()
    );
    command->Close();

    // �R�}���h�����s���� TLAS �̍\�z������������.
    m_device->ExecuteCommandList(command);

    // ���̐�� TLAS �̃o�b�t�@�̂ݎg���̂Ń����o�ϐ��ɑ�����Ă���.
    m_tlas = sceneASB.asbuffer;

    // �f�B�X�N���v�^�̏���.
    m_tlasDescriptor = m_device->AllocateDescriptor();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.RaytracingAccelerationStructure.Location = m_tlas->GetGPUVirtualAddress();
    m_device->GetDevice()->CreateShaderResourceView(
        nullptr, &srvDesc, m_tlasDescriptor.hCpu);

    // �{�֐��𔲂���ƃX�N���b�`�o�b�t�@����ƂȂ邽�ߑҋ@.
    m_device->WaitForIdleGpu();
}

void HelloScene::CreateStateObject()
{
    // �V�F�[�_�[�t�@�C���̓ǂݍ���.
    std::ifstream infile("scene-shaders.dxlib", std::ios::binary);
    if (!infile) {
        throw std::runtime_error("Shader file not found.");
    }
    std::vector<char> shaderBin;
#if !defined(USE_RUNTIME_SHADER_COMPILE)
    shaderBin.resize(infile.seekg(0, std::ios::end).tellg());
    infile.seekg(0, std::ios::beg).read(shaderBin.data(), shaderBin.size());
#else
    shaderBin = util::CompileShader(L"scene-shaders.hlsl");
#endif
    const UINT MaxPayloadSize = sizeof(XMFLOAT3);
    const UINT MaxAttributeSize = sizeof(XMFLOAT2);
    const UINT MaxRecursionDepth = 1;

#if !defined(USE_UTILS)
    std::vector<D3D12_STATE_SUBOBJECT> subobjects;
    subobjects.reserve(9); // �����f�[�^�̈����z�����������Ȃ��悤���O�m��.

    // �V�F�[�_�[����e�֐����R�[�h.
    D3D12_EXPORT_DESC exports[] = {
        { L"mainRayGen", nullptr, D3D12_EXPORT_FLAG_NONE },
        { L"mainMS", nullptr, D3D12_EXPORT_FLAG_NONE },
        { L"mainCHS", nullptr, D3D12_EXPORT_FLAG_NONE },
    };
    D3D12_DXIL_LIBRARY_DESC dxilLibDesc{};
    dxilLibDesc.DXILLibrary = D3D12_SHADER_BYTECODE{ shaderBin.data(), shaderBin.size() };
    dxilLibDesc.NumExports = _countof(exports);
    dxilLibDesc.pExports = exports;

    subobjects.emplace_back(
        D3D12_STATE_SUBOBJECT{
            D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &dxilLibDesc
        }
    );

    // �q�b�g�O���[�v�̐ݒ�.
    D3D12_HIT_GROUP_DESC hitGroupDesc{};
    hitGroupDesc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
    hitGroupDesc.ClosestHitShaderImport = L"mainCHS";
    hitGroupDesc.HitGroupExport = AppHitGroups::Object;
    subobjects.emplace_back(
        D3D12_STATE_SUBOBJECT{
            D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, &hitGroupDesc
        }
    );

    // �O���[�o�� Root Signature �ݒ�.
    D3D12_GLOBAL_ROOT_SIGNATURE rootSignatureGlobal{};
    rootSignatureGlobal.pGlobalRootSignature = m_rootSignatureGlobal.Get();
    subobjects.emplace_back(
        D3D12_STATE_SUBOBJECT{
            D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, &rootSignatureGlobal
        }
    );

    // ���[�J�� Root Signature �ݒ�.
    D3D12_LOCAL_ROOT_SIGNATURE rootSignatureLocal{};
    rootSignatureLocal.pLocalRootSignature = m_rsModel.Get();
    subobjects.emplace_back(
        D3D12_STATE_SUBOBJECT{
            D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE, &rootSignatureLocal
        }
    );

    const wchar_t* symbols[] = { AppHitGroups::Object };
    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION assoc{};
    assoc.NumExports = _countof(symbols);
    assoc.pExports = symbols;
    assoc.pSubobjectToAssociate = &subobjects.back();
    subobjects.emplace_back(
        D3D12_STATE_SUBOBJECT{
            D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION,
            &assoc
        }
    );

    D3D12_LOCAL_ROOT_SIGNATURE lrsRayGen{};
    lrsRayGen.pLocalRootSignature = m_rsRGS.Get();
    subobjects.emplace_back(
        D3D12_STATE_SUBOBJECT{
            D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE, &lrsRayGen
        }
    );
    const wchar_t* symbolsRGS[] = { L"mainRayGen" };
    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION assoc2{};
    assoc2.NumExports = _countof(symbolsRGS);
    assoc2.pExports = symbolsRGS;
    assoc2.pSubobjectToAssociate = &subobjects.back();
    subobjects.emplace_back(
        D3D12_STATE_SUBOBJECT{
            D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION,
            & assoc2
        }
    );


    // �V�F�[�_�[�ݒ�.
    D3D12_RAYTRACING_SHADER_CONFIG shaderConfig{};
    shaderConfig.MaxPayloadSizeInBytes = MaxPayloadSize;
    shaderConfig.MaxAttributeSizeInBytes = MaxAttributeSize;
    subobjects.emplace_back(
        D3D12_STATE_SUBOBJECT{
            D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, &shaderConfig
        }
    );

    // �p�C�v���C���ݒ�.
    D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig{};
    pipelineConfig.MaxTraceRecursionDepth = MaxRecursionDepth;
    subobjects.emplace_back(
        D3D12_STATE_SUBOBJECT{
            D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, &pipelineConfig
        }
    );

    // �X�e�[�g�I�u�W�F�N�g�̐���.
    D3D12_STATE_OBJECT_DESC stateObjDesc{};
    stateObjDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    stateObjDesc.NumSubobjects = UINT(subobjects.size());
    stateObjDesc.pSubobjects = subobjects.data();

    auto device = m_device->GetDevice();
    HRESULT hr = device->CreateStateObject(
        &stateObjDesc, IID_PPV_ARGS(m_rtState.ReleaseAndGetAddressOf())
    );
    if (FAILED(hr)) {
        throw std::runtime_error("CreateStateObject failed.");
    }
#else
    CD3DX12_STATE_OBJECT_DESC subobjects;
    subobjects.SetStateObjectType(D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);

    D3D12_SHADER_BYTECODE shadercode{ shaderBin.data(), shaderBin.size() };
    // �V�F�[�_�[�̊e�֐����R�[�h�̓o�^.
    auto dxilLib = subobjects.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    dxilLib->SetDXILLibrary(&shadercode);
    dxilLib->DefineExport(L"mainRayGen");
    dxilLib->DefineExport(L"mainMS");
    dxilLib->DefineExport(L"mainCHS");

    // �q�b�g�O���[�v�̐ݒ�.
    auto hitgroup = subobjects.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
    hitgroup->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);
    hitgroup->SetClosestHitShaderImport(L"mainCHS");
    hitgroup->SetHitGroupExport(AppHitGroups::Object);

    // �O���[�o�� Root Signature �ݒ�.
    auto rootsig = subobjects.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
    rootsig->SetRootSignature(m_rootSignatureGlobal.Get());

    // ���[�J�� Root Signature �ݒ�.
    auto rsModel = subobjects.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
    rsModel->SetRootSignature(m_rsModel.Get());
    auto lrsAssocModel = subobjects.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
    lrsAssocModel->AddExport(AppHitGroups::Object);
    lrsAssocModel->SetSubobjectToAssociate(*rsModel);
        
    auto rsRayGen = subobjects.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
    rsRayGen->SetRootSignature(m_rsRGS.Get());
    auto lrsAssocRGS = subobjects.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
    lrsAssocRGS->AddExport(L"mainRayGen");
    lrsAssocRGS->SetSubobjectToAssociate(*rsRayGen);

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

#endif
}

void HelloScene::CreateResultBuffer()
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

void HelloScene::CreateRootSignatureGlobal()
{
#if !defined(USE_UTILS)
    std::array<CD3DX12_ROOT_PARAMETER, 2> rootParams;

    // TLAS �� t0 ���W�X�^�Ɋ��蓖�ĂĎg�p����ݒ�.
    CD3DX12_DESCRIPTOR_RANGE descRangeTLAS;
    descRangeTLAS.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

    rootParams[0].InitAsDescriptorTable(1, &descRangeTLAS);
    rootParams[1].InitAsConstantBufferView(0); // b0

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc{};
    rootSigDesc.NumParameters = UINT(rootParams.size());
    rootSigDesc.pParameters = rootParams.data();

    m_rootSignatureGlobal = CreateRootSignature(rootSigDesc);
    m_rootSignatureGlobal->SetName(L"RootSignatureGlobal");
#else
    // [0] : �f�B�X�N���v�^, t0, TLAS �o�b�t�@.
    // [1] : �萔�o�b�t�@, b0, �V�[�����ʒ萔�o�b�t�@.
    util::RootSignatureHelper rshelper;
    rshelper.Add(util::RootSignatureHelper::RangeType::SRV, 0);
    rshelper.Add(util::RootSignatureHelper::RootType::CBV, 0);

    // ���[�g�V�O�l�`����������.
    auto isLocal = false;
    m_rootSignatureGlobal = rshelper.Create(m_device, isLocal, L"RootSignatureGlobal");
#endif
}

void HelloScene::CreateLocalRootSignatureRayGen()
{
#if !defined(USE_UTILS)
    // UAV (u0)
    D3D12_DESCRIPTOR_RANGE descUAV{};
    descUAV.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    descUAV.BaseShaderRegister = 0;
    descUAV.NumDescriptors = 1;

    std::array<D3D12_ROOT_PARAMETER, 1> rootParams;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[0].DescriptorTable.pDescriptorRanges = &descUAV;

    ComPtr<ID3DBlob> blob, errBlob;
    D3D12_ROOT_SIGNATURE_DESC rootSigDesc{};
    rootSigDesc.NumParameters = UINT(rootParams.size());
    rootSigDesc.pParameters = rootParams.data();
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
    m_rsRGS = CreateRootSignature(rootSigDesc);
    m_rsRGS->SetName(L"lrsRayGen");
#else
    // [0] : �f�B�X�N���v�^, u0, ���C�g���[�V���O���ʏ������ݗp.
    util::RootSignatureHelper rshelper;
    rshelper.Add(util::RootSignatureHelper::RangeType::UAV, 0);

    // ���[�J�����[�g�V�O�l�`������.
    auto isLocal = true;
    m_rsRGS = rshelper.Create(m_device, isLocal, L"lrsRayGen");
#endif
}

void HelloScene::CreateLocalRootSignatureCHS()
{
#if !defined(USE_UTILS)
    // ���_�E�C���f�b�N�X�o�b�t�@�ɃA�N�Z�X���邽�߂̃��[�J�����[�g�V�O�l�`�������.
    D3D12_DESCRIPTOR_RANGE rangeIB{};
    rangeIB.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rangeIB.BaseShaderRegister = 0;
    rangeIB.NumDescriptors = 1;
    rangeIB.RegisterSpace = 1;

    D3D12_DESCRIPTOR_RANGE rangeVB{};
    rangeVB.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rangeVB.BaseShaderRegister = 1;
    rangeVB.NumDescriptors = 1;
    rangeVB.RegisterSpace = 1;


    std::array<D3D12_ROOT_PARAMETER, 2> rootParams;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[0].DescriptorTable.pDescriptorRanges = &rangeIB;

    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[1].DescriptorTable.pDescriptorRanges = &rangeVB;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc{};
    rootSigDesc.NumParameters = UINT(rootParams.size());
    rootSigDesc.pParameters = rootParams.data();
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
    m_rsModel = CreateRootSignature(rootSigDesc);
    m_rsModel->SetName(L"lrsModel");
#else
    const UINT space = 1;
    // [0] : �f�B�X�N���v�^, t0(space1), �C���f�b�N�X�o�b�t�@.
    // [1] : �f�B�X�N���v�^, t1(space1), ���_�o�b�t�@.
    util::RootSignatureHelper rshelper;
    rshelper.Add(util::RootSignatureHelper::RangeType::SRV, 0, space);
    rshelper.Add(util::RootSignatureHelper::RangeType::SRV, 1, space);

    // ���[�J�����[�g�V�O�l�`������.
    auto isLocal = true;
    m_rsModel = rshelper.Create(m_device, isLocal, L"lrsModel");
#endif
}

void HelloScene::CreateShaderTable()
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

    // �g�p����e�V�F�[�_�[�̌����A�V�F�[�_�[�e�[�u���̃T�C�Y�����߂�.
    //  RayGen : 1
    //  Miss : 1
    //  HitGroup: 2 (�I�u�W�F�N�g��3�`�悷�邪�q�b�g�O���[�v��2�ŏ�������).
    UINT hitgroupCount = 2;
    UINT raygenSize = 1 * raygenRecordSize;
    UINT missSize = 1 * missRecordSize;
    UINT hitGroupSize = hitgroupCount * hitgroupRecordSize;

    // �e�e�[�u���̊J�n�ʒu�ɃA���C�����g���񂪂���̂Œ�������.
    auto tableAlign = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
    auto raygenRegion = util::RoundUp(raygenSize, tableAlign);
    auto missRegion = util::RoundUp(missSize, tableAlign);
    auto hitgroupRegion = util::RoundUp(hitGroupSize, tableAlign);

    // �V�F�[�_�[�e�[�u���m��.
    auto tableSize = raygenRegion + missRegion + hitgroupRegion;
    m_shaderTable = util::CreateBuffer(
        m_device, tableSize, nullptr,
        D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE,
        L"ShaderTable");

    ComPtr<ID3D12StateObjectProperties> rtsoProps;
    m_rtState.As(&rtsoProps);

    // �e�V�F�[�_�[���R�[�h����������ł���.
    void* mapped = nullptr;
    m_shaderTable->Map(0, nullptr, &mapped);
    uint8_t* pStart = static_cast<uint8_t*>(mapped);

    // RayGeneration �p�̃V�F�[�_�[���R�[�h����������.
    auto rgsStart = pStart;
    {
        uint8_t* p = rgsStart;
        auto id = rtsoProps->GetShaderIdentifier(L"mainRayGen");
        if (id == nullptr) {
            throw std::logic_error("Not found ShaderIdentifier");
        }
        p += util::WriteShaderIdentifier(p, id);

        // ���[�J�����[�g�V�O�l�`���� u0 (�o�͐�) ��ݒ肵�Ă��邽��
        // �Ή�����f�B�X�N���v�^����������.
        p += util::WriteGPUDescriptor(p, m_outputDescriptor);
    }

    // Miss Shader �p�̃V�F�[�_�[���R�[�h����������.
    auto missStart = pStart + raygenRegion;
    {
        uint8_t* p = missStart;
        auto id = rtsoProps->GetShaderIdentifier(L"mainMS");
        if (id == nullptr) {
            throw std::logic_error("Not found ShaderIdentifier");
        }
        p += util::WriteShaderIdentifier(p, id);

        // ���[�J�����[�g�V�O�l�`���g�p���ɂ͑��̃f�[�^����������.
    }

    // Hit Group �p�̃V�F�[�_�[���R�[�h����������.
    auto hitgroupStart = pStart + raygenRegion + missRegion;
    {
        uint8_t* pRecord = hitgroupStart;

        // plane �ɑΉ�����V�F�[�_�[���R�[�h����������.
        pRecord = WriteShaderRecord(pRecord, m_meshPlane, hitgroupRecordSize);

        // cube �ɑΉ�����V�F�[�_�[���R�[�h����������.
        pRecord = WriteShaderRecord(pRecord, m_meshCube, hitgroupRecordSize);
    }

    m_shaderTable->Unmap(0, nullptr);

    // DispatchRays �̂��߂ɏ����Z�b�g���Ă���.
    auto startAddress = m_shaderTable->GetGPUVirtualAddress();

    auto& shaderRecordRG = m_dispatchRayDesc.RayGenerationShaderRecord;
    shaderRecordRG.StartAddress = startAddress;
    shaderRecordRG.SizeInBytes = raygenSize;
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
    startAddress += hitgroupRegion;

    m_dispatchRayDesc.Width = GetWidth();
    m_dispatchRayDesc.Height = GetHeight();
    m_dispatchRayDesc.Depth = 1;
}
