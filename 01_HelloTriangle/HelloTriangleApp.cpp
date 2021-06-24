#define GRAPHICSDEVICE_IMPLEMENTATION
#include "HelloTriangleApp.h"

#include "Win32Application.h"

#include "d3dx12.h"
#include <fstream>

using namespace DirectX;

static const wchar_t* DefaultHitgroup = L"DefaultHitGroup";

void HelloTriangle::OnInit()
{
    if (!InitializeGraphicsDevice(Win32Application::GetHWND()))
    {
        throw std::runtime_error("Failed Initialize GraphicsDevice.");
    }
    
    // �ŏ���3�p�`�� BLAS ���\�z.
    CreateTriangleBLAS();

    // TLAS ���\�z.
    CreateSceneTLAS();

    // �O���[�o�� Root Signature ��p��.
    CreateRootSignatureGlobal();

    // �R���p�C���ς݃V�F�[�_�[���X�e�[�g�I�u�W�F�N�g��p��.
    CreateStateObject();

    // ���C�g���[�V���O���ʊi�[�̂��߂̃o�b�t�@(UAV)��p��.
    CreateResultBuffer();

    // �`��Ŏg�p���� Shader Table ��p��.
    CreateShaderTable();

    // �R�}���h���X�g�p��.
    //  �`�掞�ɐςނ̂ł����ł̓N���[�Y���Ă���.
    m_commandList = m_device->CreateCommandList();
    m_commandList->Close();
}

void HelloTriangle::OnDestroy()
{
    TerminateGraphicsDevice();
}

void HelloTriangle::OnUpdate()
{
}

void HelloTriangle::OnRender()
{
    auto device = m_device->GetDevice();
    auto renderTarget = m_device->GetRenderTarget();
    auto allocator = m_device->GetCurrentCommandAllocator();
    allocator->Reset();
    m_commandList->Reset(allocator.Get(), nullptr);

    ID3D12DescriptorHeap* descriptorHeaps[] = {
        m_device->GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV).Get(),
    };
    m_commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
    m_commandList->SetComputeRootSignature(m_rootSignatureGlobal.Get());

    m_commandList->SetComputeRootDescriptorTable(0, m_tlasDescriptor.hGpu);
    m_commandList->SetComputeRootDescriptorTable(1, m_outputDescriptor.hGpu);

    // ���C�g���[�V���O���ʃo�b�t�@�� UAV ��Ԃ�.
    auto barrierToUAV = CD3DX12_RESOURCE_BARRIER::Transition(
        m_dxrOutput.Get(),
        D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS
    );
    m_commandList->ResourceBarrier(1, &barrierToUAV);

    // ���C�g���[�V���O���J�n.
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

    // Present �\�Ȃ悤�Ƀo���A���Z�b�g.
    auto barrierToPresent = CD3DX12_RESOURCE_BARRIER::Transition(
        renderTarget.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT
    );
    m_commandList->ResourceBarrier(1, &barrierToPresent);

    m_commandList->Close();

    m_device->ExecuteCommandList(m_commandList);
    m_device->Present(1);
}

void HelloTriangle::CreateTriangleBLAS()
{
    auto d3d12Device = m_device->GetDevice();

    // ���_�o�b�t�@(�ŏ���3�p�`�p) ��p��.
    Vertex tri[] = {
        XMFLOAT3(-0.5f, -0.5f, 0.0f), 
        XMFLOAT3(+0.5f, -0.5f, 0.0f),
        XMFLOAT3( 0.0f, +0.75f, 0.0f)
    };
    auto vbSize = sizeof(tri);
    m_vertexBuffer = m_device->CreateBuffer(
        vbSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_HEAP_TYPE_UPLOAD);
    if (m_vertexBuffer == nullptr) {
        throw std::runtime_error("Create vertexBuffer failed.");
    }
    m_device->WriteToHostVisibleMemory(m_vertexBuffer, tri, vbSize);
    

    // BLAS ���쐬.
    D3D12_RAYTRACING_GEOMETRY_DESC geomDesc{};
    geomDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geomDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geomDesc.Triangles.VertexBuffer.StartAddress = m_vertexBuffer->GetGPUVirtualAddress();
    geomDesc.Triangles.VertexBuffer.StrideInBytes = sizeof(Vertex);
    geomDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geomDesc.Triangles.VertexCount = _countof(tri);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildASDesc{};
    auto& inputs = buildASDesc.Inputs; // D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
    inputs.NumDescs = 1;
    inputs.pGeometryDescs = &geomDesc;

    // �K�v�ȃ������ʂ����߂�.
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO blasPrebuild{};
    d3d12Device->GetRaytracingAccelerationStructurePrebuildInfo(
        &inputs, &blasPrebuild
    );

    // �X�N���b�`�o�b�t�@���m��.
    ComPtr<ID3D12Resource> blasScratch;
    blasScratch = m_device->CreateBuffer(
        blasPrebuild.ScratchDataSizeInBytes,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_HEAP_TYPE_DEFAULT
    );
    
    // BLAS �p������(�o�b�t�@)���m��.
    // ���\�[�X�X�e�[�g�� D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE
    m_blas = m_device->CreateBuffer(
        blasPrebuild.ResultDataMaxSizeInBytes,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
        D3D12_HEAP_TYPE_DEFAULT
    );
    if (m_blas == nullptr || blasScratch == nullptr) {
        throw std::runtime_error("BLAS Creation failed.");
    }
    m_blas->SetName(L"Triangle-Blas");

    // Acceleration Structure �\�z.
    buildASDesc.ScratchAccelerationStructureData = blasScratch->GetGPUVirtualAddress();
    buildASDesc.DestAccelerationStructureData = m_blas->GetGPUVirtualAddress();

    // �R�}���h���X�g�ɐς�Ŏ��s����.
    auto command = m_device->CreateCommandList();
    command->BuildRaytracingAccelerationStructure(
        &buildASDesc, 0, nullptr /* pPostBuildInfoDescs */
    );

    // ���\�[�X�o���A�̐ݒ�.
    D3D12_RESOURCE_BARRIER uavBarrier{};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = m_blas.Get();
    command->ResourceBarrier(1, &uavBarrier);
    command->Close();

    // BLAS �\�z.
    m_device->ExecuteCommandList(command);
    
    // �{�֐��𔲂���ƃR�}���h��X�N���b�`�o�b�t�@������ƂȂ邽�ߑҋ@.
    m_device->WaitForIdleGpu();
}

void HelloTriangle::CreateSceneTLAS()
{
    auto d3d12Device = m_device->GetDevice();

    D3D12_RAYTRACING_INSTANCE_DESC instanceDesc{};
    // �ϊ��s��� float m[3][4]�̂��� XMFLOAT3X4 ���g�p.
    XMStoreFloat3x4(
        reinterpret_cast<XMFLOAT3X4*>(&instanceDesc.Transform),
        XMMatrixIdentity());
    instanceDesc.InstanceID = 0;
    instanceDesc.InstanceMask = 0xFF;
    instanceDesc.InstanceContributionToHitGroupIndex = 0;
    instanceDesc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
    instanceDesc.AccelerationStructure = m_blas->GetGPUVirtualAddress();

    // �C���X�^���X�̏����L�^�����o�b�t�@����������.
    ComPtr<ID3D12Resource> instanceDescBuffer;
    instanceDescBuffer = m_device->CreateBuffer(
        sizeof(instanceDesc),
        D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_HEAP_TYPE_UPLOAD
    );
    m_device->WriteToHostVisibleMemory(
        instanceDescBuffer,
        &instanceDesc, sizeof(instanceDesc)
    );

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildASDesc{};
    auto& inputs = buildASDesc.Inputs; // D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
    inputs.NumDescs = 1;
    
    // �K�v�ȃ������ʂ����߂�.
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO tlasPrebuild{};
    d3d12Device->GetRaytracingAccelerationStructurePrebuildInfo(
        &inputs, &tlasPrebuild
    );

    // �X�N���b�`�o�b�t�@���m��.
    ComPtr<ID3D12Resource> tlasScratch;
    tlasScratch = m_device->CreateBuffer(
        tlasPrebuild.ScratchDataSizeInBytes,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_HEAP_TYPE_DEFAULT
    );

    // TLAS �p������(�o�b�t�@)���m��.
    // ���\�[�X�X�e�[�g�� D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE
    m_tlas = m_device->CreateBuffer(
        tlasPrebuild.ResultDataMaxSizeInBytes,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
        D3D12_HEAP_TYPE_DEFAULT
    );
    if (m_tlas == nullptr || tlasScratch == nullptr) {
        throw std::runtime_error("TLAS Creation failed.");
    }
    m_tlas->SetName(L"Triangle-Tlas");

    // Acceleration Structure �\�z.
    buildASDesc.Inputs.InstanceDescs = instanceDescBuffer->GetGPUVirtualAddress();
    buildASDesc.ScratchAccelerationStructureData = tlasScratch->GetGPUVirtualAddress();
    buildASDesc.DestAccelerationStructureData = m_tlas->GetGPUVirtualAddress();

    // �R�}���h���X�g�ɐς�Ŏ��s����.
    auto command = m_device->CreateCommandList();
    command->BuildRaytracingAccelerationStructure(
        &buildASDesc, 0, nullptr /* pPostBuildInfoDescs */
    );

    // ���\�[�X�o���A�̐ݒ�.
    D3D12_RESOURCE_BARRIER uavBarrier{};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = m_tlas.Get();
    command->ResourceBarrier(1, &uavBarrier);
    command->Close();

    // TLAS �\�z.
    m_device->ExecuteCommandList(command);

    // �f�B�X�N���v�^�̏���.
    auto device = m_device->GetDevice();
    m_tlasDescriptor = m_device->AllocateDescriptor();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.RaytracingAccelerationStructure.Location = m_tlas->GetGPUVirtualAddress();
    device->CreateShaderResourceView(nullptr, &srvDesc, m_tlasDescriptor.hCpu);

    // �{�֐��𔲂���ƃX�N���b�`�o�b�t�@��InstanceDesc�̃o�b�t�@������ƂȂ邽�ߑҋ@.
    m_device->WaitForIdleGpu();
}

void HelloTriangle::CreateStateObject()
{
    // �V�F�[�_�[�t�@�C���̓ǂݍ���.
    std::ifstream infile("triangle-shaders.dxlib", std::ios::binary);
    if (!infile) {
        throw std::runtime_error("Shader file not found.");
    }
    std::vector<char> shaderBin;
    shaderBin.resize(infile.seekg(0, std::ios::end).tellg());
    infile.seekg(0, std::ios::beg).read(shaderBin.data(), shaderBin.size());

    std::vector<D3D12_STATE_SUBOBJECT> subobjects;
    subobjects.reserve(32);

    // DXIL Library
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
    hitGroupDesc.HitGroupExport = DefaultHitgroup;
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

    // �V�F�[�_�[�ݒ�.
    D3D12_RAYTRACING_SHADER_CONFIG shaderConfig{};
    shaderConfig.MaxPayloadSizeInBytes = sizeof(XMFLOAT3);
    shaderConfig.MaxAttributeSizeInBytes = sizeof(XMFLOAT2);
    subobjects.emplace_back(
        D3D12_STATE_SUBOBJECT{
            D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, &shaderConfig
        }
    );

    // �p�C�v���C���ݒ�.
    D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig{};
    pipelineConfig.MaxTraceRecursionDepth = 1;
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
}

void HelloTriangle::CreateRootSignatureGlobal()
{
    std::array<D3D12_ROOT_PARAMETER, 2> rootParams;
    
    // TLAS �� t0 ���W�X�^�Ɋ��蓖�ĂĎg�p����ݒ�.
    D3D12_DESCRIPTOR_RANGE descRangeTLAS{};
    descRangeTLAS.BaseShaderRegister = 0;
    descRangeTLAS.NumDescriptors = 1;
    descRangeTLAS.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;

    // �o�̓o�b�t�@(UAV) �� u0 ���W�X�^�Ɋ��蓖�ĂĎg�p����ݒ�.
    D3D12_DESCRIPTOR_RANGE descRangeOutput{};
    descRangeOutput.BaseShaderRegister = 0;
    descRangeOutput.NumDescriptors = 1;
    descRangeOutput.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;

    rootParams[0] = D3D12_ROOT_PARAMETER{};
    rootParams[1] = D3D12_ROOT_PARAMETER{};

    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[0].DescriptorTable.pDescriptorRanges = &descRangeTLAS;

    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[1].DescriptorTable.pDescriptorRanges = &descRangeOutput;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc{};
    rootSigDesc.NumParameters = UINT(rootParams.size());
    rootSigDesc.pParameters = rootParams.data();

    HRESULT hr;
    ComPtr<ID3DBlob> blob, errBlob;
    hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &blob, &errBlob);
    if (FAILED(hr)) {
        throw std::runtime_error("RootSignature(global) failed.");
    }

    hr = m_device->GetDevice()->CreateRootSignature(
        0, blob->GetBufferPointer(), blob->GetBufferSize(),
        IID_PPV_ARGS(m_rootSignatureGlobal.ReleaseAndGetAddressOf())
    );
    if (FAILED(hr)) {
        throw std::runtime_error("RootSignature(global) failed.");
    }
    m_rootSignatureGlobal->SetName(L"RootSignatureGlobal");
}

void HelloTriangle::CreateResultBuffer()
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


void HelloTriangle::CreateShaderTable()
{
    // �e�V�F�[�_�[���R�[�h�� Shader Identifier ��ێ�����.
    UINT recordSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;

    // �O���[�o���̃��[�g�V�O�l�`���ȊO�̏��������Ȃ��̂Ń��R�[�h�T�C�Y�͂��ꂾ��.

    // ���Ƃ̓A���C�����g�����ۂ悤�ɂ���.
    recordSize = util::RoundUp(recordSize, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);

    // �V�F�[�_�[�e�[�u���̃T�C�Y�����߂�.
    UINT raygenSize = 1 * recordSize; // ��1�� Ray Generation �V�F�[�_�[.
    UINT missSize = 1 * recordSize;  // ��1�� Miss �V�F�[�_�[.
    UINT hitGroupSize = 1 * recordSize; // ��1�� HitGroup ���g�p.

    // �e�e�[�u���̊J�n�ʒu�ɃA���C�����g���񂪂���̂Œ�������.
    auto tableAlign = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
    UINT raygenRegion = util::RoundUp(raygenSize, tableAlign);
    UINT missRegion = util::RoundUp(missSize, tableAlign);
    UINT hitgroupRegion = util::RoundUp(hitGroupSize, tableAlign);

    // �V�F�[�_�[�e�[�u���m��.
    auto tableSize = raygenRegion + missRegion + hitgroupRegion;
    m_shaderTable = m_device->CreateBuffer(
        tableSize,
        D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_HEAP_TYPE_UPLOAD
    );

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
        memcpy(p, id, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        p += D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;

        // ���[�J�����[�g�V�O�l�`���g�p���ɂ͑��̃f�[�^����������.
    }

    // Miss Shader �p�̃V�F�[�_�[���R�[�h����������.
    auto missStart = pStart + raygenRegion;
    {
        uint8_t* p = missStart;
        auto id = rtsoProps->GetShaderIdentifier(L"mainMS");
        if (id == nullptr) {
            throw std::logic_error("Not found ShaderIdentifier");
        }
        memcpy(p, id, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        p += D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;

        // ���[�J�����[�g�V�O�l�`���g�p���ɂ͑��̃f�[�^����������.
    }

    // Hit Group �p�̃V�F�[�_�[���R�[�h����������.
    auto hitgroupStart = pStart + raygenRegion + missRegion;
    {
        uint8_t* p = hitgroupStart;
        auto id = rtsoProps->GetShaderIdentifier(DefaultHitgroup);
        if (id == nullptr) {
            throw std::logic_error("Not found ShaderIdentifier");
        }
        memcpy(p, id, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        p += D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;

        // ���[�J�����[�g�V�O�l�`���g�p���ɂ͑��̃f�[�^����������.
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
    shaderRecordMS.StrideInBytes = recordSize;
    startAddress += missRegion;

    auto& shaderRecordHG = m_dispatchRayDesc.HitGroupTable;
    shaderRecordHG.StartAddress = startAddress;
    shaderRecordHG.SizeInBytes = hitGroupSize;
    shaderRecordHG.StrideInBytes = recordSize;
    startAddress += hitgroupRegion;

    m_dispatchRayDesc.Width = GetWidth();
    m_dispatchRayDesc.Height = GetHeight();
    m_dispatchRayDesc.Depth = 1;
}

