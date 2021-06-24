#include "util/DxrModel.h"
#include <DirectXMath.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <queue>

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE  // 書き出しを使用しないため.
#include "tiny_gltf.h"

#include "d3dx12.h"
#include "DxrBookFramework.h"
#include "util/DxrBookUtility.h"
#include "util/TextureResource.h"

namespace util {
    using namespace DirectX;
    using namespace tinygltf;
    static XMVECTOR makeFloat3(const double* in) {
        XMFLOAT3 v;
        v.x = static_cast<float>(in[0]);
        v.y = static_cast<float>(in[1]);
        v.z = static_cast<float>(in[2]);
        return XMLoadFloat3(&v);
    }
    static XMVECTOR makeQuat(const double* in) {
        XMFLOAT4 v;
        v.x = static_cast<float>(in[0]);
        v.y = static_cast<float>(in[1]);
        v.z = static_cast<float>(in[2]);
        v.w = static_cast<float>(in[3]);
        return XMLoadFloat4(&v);
    }

    DxrModel::Node::Node() {
        translation = XMVectorZero();
        scale = XMVectorSet(1.0f, 1.0f, 1.0f, 0.0f);
        rotation = XMQuaternionIdentity();
        mtxLocal = XMMatrixIdentity();
        mtxWorld = XMMatrixIdentity();
        name = L"";
        parent = nullptr;
    }
    DxrModel::Node::~Node() {
    }

    DxrModel::DxrModel() {
    }
    DxrModel::~DxrModel() {
    }
    void DxrModel::Destroy(std::unique_ptr<dx12::GraphicsDevice>& device) {
        for (auto& t : m_textures) {
            device->DeallocateDescriptor(t.srv);
        }
        device->DeallocateDescriptor(m_whiteTex.srv);
        m_textures.clear();
        m_nodes.clear();
    }

    bool DxrModel::LoadFromGltf(
        const std::wstring& fileName, std::unique_ptr<dx12::GraphicsDevice>& device)
    {
        std::filesystem::path filePath(fileName);
        std::vector<char> buffer;
        util::LoadFile(buffer, fileName);

        std::string baseDir;
        if (filePath.is_relative()) {
            auto current = std::filesystem::current_path();
            current /= filePath;
            current.swap(filePath);
        }
        baseDir = filePath.parent_path().string();

        std::string err, warn;
        TinyGLTF loader;
        Model model;
        bool result = false;
        if (filePath.extension() == L".glb") {
            result = loader.LoadBinaryFromMemory(&model, &err, &warn, 
                reinterpret_cast<const uint8_t*>(buffer.data()),
                uint32_t(buffer.size()), baseDir);
        }
        if (!warn.empty()) {
            OutputDebugStringA(warn.c_str());
        }
        if (!err.empty()) {
            OutputDebugStringA(err.c_str());
        }
        if (!result) {
            return false;
        }

        VertexAttributeVisitor visitor;
        const auto& scene = model.scenes[0];
        for (const auto& nodeIndex : scene.nodes) {
            m_rootNodes.push_back(nodeIndex);
        }

        LoadNode(model);
        LoadMesh(model, visitor);

        LoadSkin(model);
        LoadMaterial(model);

        auto heapType = D3D12_HEAP_TYPE_DEFAULT;
        auto flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        auto sizePos = sizeof(XMFLOAT3) * visitor.positionBuffer.size();
        auto sizeNrm = sizeof(XMFLOAT3) * visitor.normalBuffer.size();
        auto sizeTex = sizeof(XMFLOAT2) * visitor.texcoordBuffer.size();

        // 頂点データの生成.
        m_vertexAttrib.Position = util::CreateBuffer(device, sizePos, visitor.positionBuffer.data(), heapType, flags, L"PosBuf");
        m_vertexAttrib.Normal = util::CreateBuffer(device, sizeNrm, visitor.normalBuffer.data(), heapType, flags, L"NormalBuf");
        m_vertexAttrib.Texcoord = util::CreateBuffer(device, sizeTex, visitor.texcoordBuffer.data(), heapType, flags, L"TexBuf");

        // インデックスバッファ.
        auto sizeIdx = sizeof(UINT) * visitor.indexBuffer.size();
        m_indexBuffer = util::CreateBuffer(device, sizeIdx, visitor.indexBuffer.data(), heapType, flags, L"IndexBuf");

        // スキニングモデル用.
        if ( m_hasSkin ) {
            auto sizeJoint = sizeof(XMUINT4) * visitor.jointBuffer.size();
            auto sizeWeight = sizeof(XMFLOAT4) * visitor.weightBuffer.size();
            m_vertexAttrib.JointIndices = util::CreateBuffer(device, sizeJoint, visitor.jointBuffer.data(), heapType, flags, L"JointIndices");
            m_vertexAttrib.JointWeights = util::CreateBuffer(device, sizeWeight, visitor.weightBuffer.data(), heapType, flags, L"JointWeights");
            // 個数をスキニングで使用する頂点数とする.
            //   (Position と同じ個数となっているものを対象としているのでこれでよい)
            m_skinInfo.skinVertexCount = UINT(visitor.jointBuffer.size());

        }

        for (auto& texture : model.textures) {
            auto image = model.images[texture.source];
            auto fileName = util::ConvertFromUTF8(image.name);
            auto view = model.bufferViews[image.bufferView];
            auto offsetBytes = view.byteOffset;
            const void* src = &model.buffers[view.buffer].data[offsetBytes];
            UINT64 size = view.byteLength;
            
            auto t = util::LoadTextureFromMemory(src, size, device);
            t.resource->SetName(fileName.c_str());
            m_textures.emplace_back(t);
        }

        m_whiteTex = util::LoadTextureFromFile(L"white.png", device);

        return true;
    }

    
    
    std::shared_ptr<DxrModelActor> DxrModel::Create(std::unique_ptr<dx12::GraphicsDevice>& device)
    {
        std::shared_ptr<DxrModelActor> actor(new DxrModelActor(device, this));
        std::vector<std::shared_ptr<DxrModelActor::Node>> nodes;
        nodes.resize(m_nodes.size());

        for (UINT i = 0; i < UINT(m_nodes.size()); ++i) {
            nodes[i] = std::make_shared<DxrModelActor::Node>();
            const auto src = m_nodes[i];
            nodes[i]->translation = src->translation;
            nodes[i]->rotation = src->rotation;
            nodes[i]->scale = src->scale;
            nodes[i]->name = src->name;
        }

        // 親子解決.
        for (UINT i = 0; i < UINT(m_nodes.size()); ++i) {
            const auto src = m_nodes[i];
            auto node = nodes[i];
            for (auto idx : src->children) {
                auto child = nodes[idx];
                node->children.push_back(child);
                child->parent = node;
            }
        }
        for (auto rootNodeIndex : m_rootNodes) {
            auto rootNode = nodes[rootNodeIndex];
            actor->m_nodes.push_back(rootNode);
        }

        // マテリアルの生成.
        for (auto inMaterial : m_materials) {
            actor->m_materials.emplace_back(new DxrModelActor::Material(device, inMaterial));
            auto material = actor->m_materials.back();

            if (inMaterial.m_textureIndex < 0) {
                // ダミーテクスチャを使用する.
                material->SetTexture(m_whiteTex);
            } else {
                material->SetTexture(m_textures[inMaterial.m_textureIndex]);
            }
        }

        actor->SetWorldMatrix(XMMatrixIdentity());
        actor->UpdateMatrices();

        actor->m_hasSkin = m_hasSkin;
        if (m_hasSkin) {
            // スキニングモデル用のリソースを準備する.
            auto& dstSkinInfo = actor->m_skinInfo;
            const auto& srcSkinInfo = m_skinInfo;

            for (auto jointIndex : srcSkinInfo.joints) {
                dstSkinInfo.jointList.push_back(nodes[jointIndex]);
            }
            dstSkinInfo.invBindMatrices = srcSkinInfo.invBindMatrices;
            dstSkinInfo.skinVertexCount = srcSkinInfo.skinVertexCount;

            // 今頂点位置も法線も同じ float3 のため共用する.
            auto vertexCount = srcSkinInfo.skinVertexCount;
            auto stride = UINT(sizeof(XMFLOAT3));
            auto bufferSize = vertexCount * stride;
            const auto initialState = D3D12_RESOURCE_STATE_COMMON;

            // 変換後のデータの格納先を確保する.
            dstSkinInfo.vbPositionTransformed = util::CreateBufferUAV(device, bufferSize, initialState, L"Transformed(Pos)");
            dstSkinInfo.vbNormalTransformed = util::CreateBufferUAV(device, bufferSize, initialState, L"Transformed(Nrm)");

            // UAV として使用するのでディスクリプタを準備する.
            dstSkinInfo.vbPositionDescriptor = util::CreateStructuredUAV(
                device, dstSkinInfo.vbPositionTransformed, vertexCount, 0, stride);
            dstSkinInfo.vbNormalDescriptor = util::CreateStructuredUAV(
                device, dstSkinInfo.vbPositionTransformed, vertexCount, 0, stride);

            // 更新されたスキニング行列のための定数バッファを確保.
            auto bufferCount = device->BackBufferCount;
            auto jointBufferSize = UINT(sizeof(XMMATRIX) * dstSkinInfo.jointList.size());
            auto jointBufferSizeDynamic = jointBufferSize * bufferCount;
            dstSkinInfo.bufJointMatrices = util::CreateBuffer(
                device, jointBufferSizeDynamic, nullptr, 
                D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, L"JointMatrices");
            for (UINT i = 0; i < bufferCount; ++i) {
                auto jointCount = UINT(dstSkinInfo.jointList.size());
                auto stride = UINT(sizeof(XMFLOAT4X4));
                auto offset = jointCount * i;
                auto srv = util::CreateStructuredSRV(device, dstSkinInfo.bufJointMatrices, jointCount, offset, stride);
                dstSkinInfo.bufJointMatricesDescriptors.push_back(srv);
            }

            //dstSkinInfo.jointMatricesCB.Initialize(device, jointCBSize, L"JointMatrices");
        }

        // BLAS 構築時に設定する行列バッファを確保.
        actor->CreateMatrixBufferBLAS(UINT(m_meshGroups.size()));

        // 頂点の属性データごとの SRV を生成.
        //  スキニング時には変換後のバッファに対して生成.
        for (UINT i = 0; i < UINT(m_meshGroups.size()); ++i) {
            actor->m_meshGroups.emplace_back(DxrModelActor::MeshGroup());
            auto& group = actor->m_meshGroups.back();
            group.m_node = nodes[m_meshGroups[i].m_nodeIndex];
            for (auto& inMesh : m_meshGroups[i].m_meshes) {
                group.m_meshes.emplace_back(DxrModelActor::Mesh());
                auto& mesh = group.m_meshes.back();

                auto vertexStart = inMesh.vertexStart;
                auto vertexCount = inMesh.vertexCount;

                mesh.indexCount = inMesh.indexCount;
                mesh.indexStart = inMesh.indexStart;
                mesh.vertexStart = vertexStart;
                mesh.vertexCount = vertexCount;

                D3D12Resource attrPosition;
                D3D12Resource attrNormal;
                if (actor->IsSkinned() == false) {
                    attrPosition = m_vertexAttrib.Position;
                    attrNormal = m_vertexAttrib.Normal;
                } else {
                    auto& skin = actor->m_skinInfo;
                    attrPosition = skin.vbPositionTransformed;
                    attrNormal = skin.vbNormalTransformed;
                }
                mesh.vbAttrPosision = util::CreateStructuredSRV(device, attrPosition, vertexCount, vertexStart, DXGI_FORMAT_R32G32B32_FLOAT);
                mesh.vbAttrNormal = util::CreateStructuredSRV(device, attrNormal, vertexCount, vertexStart, DXGI_FORMAT_R32G32B32_FLOAT);
                mesh.vbAttrTexcoord = util::CreateStructuredSRV(device, m_vertexAttrib.Texcoord, vertexCount, vertexStart, DXGI_FORMAT_R32G32_FLOAT);
                mesh.indexBuffer = util::CreateStructuredSRV(device, m_indexBuffer, mesh.indexCount, mesh.indexStart, DXGI_FORMAT_R32_UINT);
                mesh.material = actor->m_materials[inMesh.materialIndex];

                auto diffuse = m_materials[inMesh.materialIndex].GetDiffuseColor();
                DxrModelActor::Mesh::MeshParameters meshParams{};
                meshParams.diffuse = XMFLOAT4{ diffuse.x, diffuse.y, diffuse.z, 1 };
                meshParams.meshGroupIndex = i;
                meshParams.strideOfMatrixBuffer = UINT(m_meshGroups.size())*3; // float4換算でのサイズを入れる.
                mesh.meshParameters = util::CreateBuffer(device, sizeof(meshParams), &meshParams, D3D12_HEAP_TYPE_DEFAULT);
            }
        }

        if ( m_hasSkin ) {
            auto& skin = actor->m_skinInfo;
            auto command = device->CreateCommandList();
            // 初期値としてコピー.
            command->CopyResource(skin.vbPositionTransformed.Get(), m_vertexAttrib.Position.Get());
            command->CopyResource(skin.vbNormalTransformed.Get(), m_vertexAttrib.Normal.Get());
            command->Close();
            device->ExecuteCommandList(command);
            device->WaitForIdleGpu();
        }

        // 行列用のバッファを更新する.
        //  スキニングの定数バッファや BLAS 構築時に設定される行列バッファを対象.
        actor->ApplyTransform();

        // BLAS の生成.
        actor->CreateBLAS();
        return actor;
    }

    void DxrModel::LoadNode(const tinygltf::Model& inModel)
    {
        for (auto& inNode : inModel.nodes) {
            m_nodes.emplace_back(new Node());
            auto node = m_nodes.back();

            node->name = util::ConvertFromUTF8(inNode.name);
            if (!inNode.translation.empty()) {
                node->translation = makeFloat3(inNode.translation.data());
            }
            if (!inNode.scale.empty()) {
                node->scale = makeFloat3(inNode.scale.data());
            }
            if (!inNode.rotation.empty()) {
                node->rotation = makeQuat(inNode.rotation.data());
            }
            for (auto& c : inNode.children) {
                node->children.push_back(c);
            }
            node->meshIndex = inNode.mesh;
        }
    }

    void DxrModel::LoadMesh(const tinygltf::Model& inModel, VertexAttributeVisitor& visitor)
    {
        auto& indexBuffer = visitor.indexBuffer;
        auto& positionBuffer = visitor.positionBuffer;
        auto& normalBuffer = visitor.normalBuffer;
        auto& texcoordBuffer = visitor.texcoordBuffer;
        auto& jointBuffer = visitor.jointBuffer;
        auto& weightBuffer = visitor.weightBuffer;
        for (auto& inMesh : inModel.meshes) {
            m_meshGroups.emplace_back(MeshGroup());
            auto& meshgrp = m_meshGroups.back();
            
            for (auto& primitive : inMesh.primitives) {
                auto indexStart = static_cast<UINT>(indexBuffer.size());
                auto vertexStart = static_cast<UINT>(positionBuffer.size());
                UINT indexCount = 0, vertexCount = 0;
                bool hasSkin = false;

                const auto& notfound = primitive.attributes.end();
                if (auto attr = primitive.attributes.find("POSITION"); attr != notfound) {
                    auto& acc = inModel.accessors[attr->second];
                    auto& view = inModel.bufferViews[acc.bufferView];
                    auto offsetBytes = acc.byteOffset + view.byteOffset;
                    const auto* src = reinterpret_cast<const XMFLOAT3*>(&(inModel.buffers[view.buffer].data[offsetBytes]));

                    vertexCount = UINT(acc.count);
                    for (UINT i = 0; i < vertexCount; ++i) {
                        positionBuffer.push_back(src[i]);
                    }
                }
                if (auto attr = primitive.attributes.find("NORMAL"); attr != notfound) {
                    auto& acc = inModel.accessors[attr->second];
                    auto& view = inModel.bufferViews[acc.bufferView];
                    auto offsetBytes = acc.byteOffset + view.byteOffset;
                    const auto* src = reinterpret_cast<const XMFLOAT3*>(&(inModel.buffers[view.buffer].data[offsetBytes]));

                    vertexCount = UINT(acc.count);
                    for (UINT i = 0; i < vertexCount; ++i) {
                        normalBuffer.push_back(src[i]);
                    }
                }
                if (auto attr = primitive.attributes.find("TEXCOORD_0"); attr != notfound) {
                    auto& acc = inModel.accessors[attr->second];
                    auto& view = inModel.bufferViews[acc.bufferView];
                    auto offsetBytes = acc.byteOffset + view.byteOffset;
                    const auto* src = reinterpret_cast<const XMFLOAT2*>(&(inModel.buffers[view.buffer].data[offsetBytes]));

                    for (UINT i = 0; i < vertexCount; ++i) {
                        texcoordBuffer.push_back(src[i]);
                    }
                }
                else {
                    // UV データが無い場合には、他のものと合わせるべくゼロで埋めておく.
                    for (UINT i = 0; i < vertexCount; ++i) {
                        texcoordBuffer.push_back(XMFLOAT2(0.0f, 0.0f));
                    }
                }

                // スキニング用のジョイント(インデックス)番号とウェイト値を読み取る.
                if (auto attr = primitive.attributes.find("JOINTS_0"); attr != notfound) {
                    auto& acc = inModel.accessors[attr->second];
                    auto& view = inModel.bufferViews[acc.bufferView];
                    auto offsetBytes = acc.byteOffset + view.byteOffset;
                    const auto* src = reinterpret_cast<const uint16_t*>(&(inModel.buffers[view.buffer].data[offsetBytes]));

                    for (UINT i = 0; i < vertexCount; ++i) {
                        auto idx = i * 4;
                        auto v = XMUINT4(
                            src[idx + 0], src[idx + 1], src[idx + 2], src[idx + 3]);
                        jointBuffer.push_back(v);
                    }
                }
                if (auto attr = primitive.attributes.find("WEIGHTS_0"); attr != notfound) {
                    auto& acc = inModel.accessors[attr->second];
                    auto& view = inModel.bufferViews[acc.bufferView];
                    auto offsetBytes = acc.byteOffset + view.byteOffset;
                    const auto* src = reinterpret_cast<const XMFLOAT4*>(&(inModel.buffers[view.buffer].data[offsetBytes]));

                    for (UINT i = 0; i < vertexCount; ++i) {
                        weightBuffer.push_back(src[i]);
                    }
                }

                //　インデックスバッファ用.
                {
                    auto& acc = inModel.accessors[primitive.indices];
                    const auto& view = inModel.bufferViews[acc.bufferView];
                    const auto& buffer = inModel.buffers[view.buffer];
                    indexCount = UINT(acc.count);
                    auto offsetBytes = acc.byteOffset + view.byteOffset;
                    if (acc.componentType == TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT) {
                        auto src = reinterpret_cast<const uint32_t*>(&(buffer.data[offsetBytes]));

                        for (size_t index = 0; index < acc.count; index++)
                        {
                            indexBuffer.push_back(src[index]);
                        }
                    }
                    if (acc.componentType == TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT) {
                        auto src = reinterpret_cast<const uint16_t*>(&(buffer.data[offsetBytes]));
                        for (size_t index = 0; index < acc.count; index++)
                        {
                            indexBuffer.push_back(src[index]);
                        }
                    }
                }

                meshgrp.m_meshes.emplace_back(Mesh());
                auto& mesh = meshgrp.m_meshes.back();
                mesh.indexStart = indexStart;
                mesh.vertexStart = vertexStart;
                mesh.indexCount = indexCount;
                mesh.vertexCount = vertexCount;
                mesh.materialIndex = primitive.material;
            }
        }

        for (UINT nodeIndex = 0; nodeIndex < UINT(inModel.nodes.size()); ++nodeIndex) {
            auto meshIndex = inModel.nodes[nodeIndex].mesh;
            if (meshIndex < 0) {
                continue;
            }
            m_meshGroups[meshIndex].m_nodeIndex = nodeIndex;
        }

    }

    void DxrModel::LoadSkin(const tinygltf::Model& inModel)
    {
        if (inModel.skins.empty()) {
            m_hasSkin = false;
            return;
        }
        m_hasSkin = true;
        // 本処理ではスキンデータの最初の1つのみを取り扱う.
        const auto& inSkin = inModel.skins[0];
        
        m_skinInfo.name = ConvertFromUTF8(inSkin.name);
        m_skinInfo.joints.assign(inSkin.joints.begin(), inSkin.joints.end());

        if (inSkin.inverseBindMatrices > -1) {
            const auto& acc = inModel.accessors[inSkin.inverseBindMatrices];
            const auto& view = inModel.bufferViews[acc.bufferView];
            const auto& buffer = inModel.buffers[view.buffer];
            m_skinInfo.invBindMatrices.resize(acc.count);

            auto offsetBytes = acc.byteOffset + view.byteOffset;
            memcpy(
                m_skinInfo.invBindMatrices.data(),
                &buffer.data[offsetBytes],
                acc.count * sizeof(XMMATRIX));
        }
    }
    void DxrModel::LoadMaterial(const tinygltf::Model& inModel)
    {
        for (const auto& inMaterial : inModel.materials) {
            m_materials.emplace_back(Material());
            auto& material = m_materials.back();
            material.m_name = util::ConvertFromUTF8(inMaterial.name);

            for (auto& value : inMaterial.values) {
                auto valueName = value.first;
                if (valueName == "baseColorTexture") {
                    auto textureIndex = value.second.TextureIndex();
                    material.m_textureIndex = textureIndex;
                }
                if (valueName == "normalTexture") {
                    auto textureIndex = value.second.TextureIndex();
                }
                if (valueName == "baseColorFactor") {
                    auto color = value.second.ColorFactor();
                    material.m_diffuseColor = XMFLOAT3(
                        float(color[0]), float(color[1]), float(color[2])
                    );
                }
            }
        }
    }

    DxrModelActor::Node::Node() {
        translation = XMVectorZero();
        rotation = XMQuaternionIdentity();
        scale = XMVectorZero();
        mtxLocal = XMMatrixIdentity();
        mtxWorld = XMMatrixIdentity();
    }

    DxrModelActor::Node::~Node() {
        children.clear();
    }

    DxrModelActor::Material::Material(
        std::unique_ptr<dx12::GraphicsDevice>& device, const DxrModel::Material& src)
        : m_device(device)
    {
        m_name = src.GetName();
        auto diffuse = src.GetDiffuseColor();
        m_materialParams.diffuse.x = diffuse.x;
        m_materialParams.diffuse.y = diffuse.y;
        m_materialParams.diffuse.z = diffuse.z;
        m_materialParams.diffuse.w = 1.0f;

        // マテリアルのバッファは変更しないものとする.
        auto sizeCB = util::RoundUp(sizeof(MaterialParameters), 255);
        m_bufferCB = util::CreateConstantBuffer(device, sizeCB);

        void* p = nullptr;
        m_bufferCB->Map(0, nullptr, &p);
        if (p) {
            memcpy(p, &m_materialParams, sizeCB);
            m_bufferCB->Unmap(0, nullptr);
        }
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbDesc{};
        cbDesc.BufferLocation = m_bufferCB->GetGPUVirtualAddress();
        cbDesc.SizeInBytes = UINT(sizeCB);
        m_cbv = device->AllocateDescriptor();
        device->GetDevice()->CreateConstantBufferView(&cbDesc, m_cbv.hCpu);
    }
    
    DxrModelActor::Material::~Material() {
        if (m_device) {
            m_device->DeallocateDescriptor(m_cbv);
            // TestureResource は DxrModel 側が所有権を持つためここで解放しない.
        }
    }

    void DxrModelActor::Material::SetTexture(util::TextureResource& resTex) {
        m_texture = resTex;
    }

    void DxrModelActor::ApplyTransform()
    {
        auto frameIndex = m_device->GetCurrentFrameIndex();
        if (IsSkinned()) {
            const auto& skin = m_skinInfo;
            const auto jointCount = skin.jointList.size();
            auto meshAttached = m_meshGroups[0].GetNode();
            auto meshInvMatrix = XMMatrixInverse(nullptr, meshAttached->GetWorldMatrix());

            std::vector<XMMATRIX> matrices(jointCount);
            for (UINT i = 0; i < UINT(skin.jointList.size()); ++i) {
                auto node = skin.jointList[i];
                auto mtx = skin.invBindMatrices[i] * node->GetWorldMatrix() * meshInvMatrix;
                matrices[i] = XMMatrixTranspose(mtx);
            }

            auto jointCB = GetJointMatrixBuffer();
            void* p = nullptr;
            UINT bufferRegion = UINT(sizeof(XMFLOAT4X4) * jointCount);
            D3D12_RANGE range{ 0, bufferRegion };
            range.Begin = frameIndex * bufferRegion;
            range.End += range.Begin;
            jointCB->Map(0, &range, &p);
            if (p) {
                auto dst = static_cast<uint8_t*>(p) + range.Begin;
                memcpy(dst, matrices.data(), bufferRegion);
                jointCB->Unmap(0, &range);
            }
        }

        // BLAS の作成・更新時で使うマトリックスのバッファを更新する.
        std::vector<XMFLOAT3X4> blasMatrices;
        auto groupCount = UINT(m_meshGroups.size());
        blasMatrices.resize(groupCount);
        for (UINT i = 0; i < groupCount; ++i) {
            auto node = m_meshGroups[i].m_node;
            if (IsSkinned()) {
                XMStoreFloat3x4(&blasMatrices[i], node->GetWorldMatrix());
            } else {
                // TLASで設定した行列成分を打ち消すために必要.
                auto invRoot = XMMatrixInverse(nullptr, this->m_mtxWorld);
                XMStoreFloat3x4(&blasMatrices[i], node->GetWorldMatrix() * invRoot);
            }
        }

        auto bufferBytes = UINT(sizeof(XMFLOAT3X4) * groupCount);
        void* mapped = nullptr;
        D3D12_RANGE range;
        range.Begin = bufferBytes * frameIndex;
        range.End = range.Begin + bufferBytes;
        m_blasMatrices->Map(0, &range, &mapped);
        if (mapped) {
            auto p = static_cast<uint8_t*>(mapped) + range.Begin;
            memcpy(p, blasMatrices.data(), bufferBytes);
            m_blasMatrices->Unmap(0, &range);
        }
    }

    std::shared_ptr<DxrModelActor::Node> DxrModelActor::SearchNode(const std::wstring& name)
    {
        std::shared_ptr<Node> result = nullptr;
        for (auto node : m_nodes) {
            result = SearchNode(node, name);
            if (result) {
                break;
            }
        }
        return result;
    }
    std::shared_ptr<DxrModelActor::Node> DxrModelActor::SearchNode(
        std::shared_ptr<Node> node, const std::wstring& name)
    {
        if (node->GetName() == name) {
            return node;
        }
        std::shared_ptr<Node> result = nullptr;
        for (auto child : node->children) {
            result = SearchNode(child, name);
            if (result) {
                break;
            }
        }
        return result;
    }


    DxrModelActor::DxrModelActor(
        std::unique_ptr<dx12::GraphicsDevice>& device, 
        const DxrModel* model) : m_device(device)
    {
        m_mtxWorld = XMMatrixIdentity();
        m_modelReference = model;
    }
    DxrModelActor::~DxrModelActor() {
        if (IsSkinned()) {
            m_device->DeallocateDescriptor(m_skinInfo.jointMatricesDescriptor);
            m_device->DeallocateDescriptor(m_skinInfo.vbPositionDescriptor);
            m_device->DeallocateDescriptor(m_skinInfo.vbNormalDescriptor);
        }
        m_nodes.clear();
        m_skinInfo.jointList.clear();
    }

    void DxrModelActor::CreateBLAS()
    {
        std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> rtGeomDesc;
        CreateRtGeometryDesc(rtGeomDesc);

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildASDesc{};
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs = buildASDesc.Inputs;
        inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        inputs.NumDescs = UINT(rtGeomDesc.size());
        inputs.pGeometryDescs = rtGeomDesc.data();
        // 動的更新を考慮するため許可フラグをつける.
        inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;

        // AS 用のバッファを作成.
        auto command = m_device->CreateCommandList();

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
        m_device->GetDevice()->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
        auto asb = util::CreateAccelerationStructure(
            m_device, buildASDesc
        );
        m_blas = asb.asbuffer;
        m_blasUpdateBuffer = asb.update;

        buildASDesc.DestAccelerationStructureData = asb.asbuffer->GetGPUVirtualAddress();
        buildASDesc.ScratchAccelerationStructureData = asb.scratch->GetGPUVirtualAddress();

        command->BuildRaytracingAccelerationStructure(&buildASDesc, 0, nullptr);
        auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(m_blas.Get());
        command->ResourceBarrier(1, &barrier);

        command->Close();

        // BLAS の構築完了まで待つ.
        m_device->ExecuteCommandList(command);
        m_device->WaitForIdleGpu();
    }
    void DxrModelActor::UpdateBLAS(ComPtr<ID3D12GraphicsCommandList4> commandList)
    {
        std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> rtGeomDesc;
        CreateRtGeometryDesc(rtGeomDesc);

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildASDesc{};
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs = buildASDesc.Inputs;
        inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        inputs.NumDescs = UINT(rtGeomDesc.size());
        inputs.pGeometryDescs = rtGeomDesc.data();
        // 更新を実施するためフラグを設定する.
        inputs.Flags =
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE |
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
        
        auto frameIndex = m_device->GetCurrentFrameIndex();
        // インプレース更新を行う.
        auto update = m_blasUpdateBuffer;
        buildASDesc.DestAccelerationStructureData = m_blas->GetGPUVirtualAddress();
        buildASDesc.SourceAccelerationStructureData = m_blas->GetGPUVirtualAddress();
        buildASDesc.ScratchAccelerationStructureData = update->GetGPUVirtualAddress();

        // BLAS の再構築およびバリアの設定.
        commandList->BuildRaytracingAccelerationStructure(&buildASDesc, 0, nullptr);
        auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(m_blas.Get());
        commandList->ResourceBarrier(1, &barrier);
    }

    void DxrModelActor::CreateRtGeometryDesc(std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>& rtGeomDesc) {
        // 安全な書き込み先(開始位置)を求める.
        auto frameIndex = m_device->GetCurrentFrameIndex();
        const auto matrixSize = sizeof(XMFLOAT3X4);
        const auto matrixBufferSize = m_meshGroups.size() * matrixSize;
        auto addressBase = m_blasMatrices->GetGPUVirtualAddress();
        addressBase += matrixBufferSize * frameIndex;

        ComPtr<ID3D12Resource> positionBuffer;
        if (IsSkinned() == false) {
            positionBuffer = m_modelReference->GetPositionBuffer();
        } else {
            positionBuffer = m_skinInfo.vbPositionTransformed;
        }
        UINT matrixIndex = 0;
        for (const auto& meshGroup : m_meshGroups) {
            auto indexBuffer = m_modelReference->GetIndexBuffer();
            for (const auto& mesh : meshGroup.m_meshes) {
                rtGeomDesc.emplace_back(D3D12_RAYTRACING_GEOMETRY_DESC{});
                auto& desc = rtGeomDesc.back();
                auto& triangles = desc.Triangles;
                //BLAS構築時にマトリックスを適用する.
                triangles.Transform3x4 = addressBase + matrixIndex * matrixSize;
                desc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
                triangles.VertexBuffer.StrideInBytes = sizeof(XMFLOAT3);
                triangles.VertexBuffer.StartAddress = positionBuffer->GetGPUVirtualAddress();
                triangles.VertexBuffer.StartAddress += mesh.GetVertexStart() * sizeof(XMFLOAT3);
                triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
                triangles.VertexCount = mesh.GetVertexCount();

                triangles.IndexBuffer = indexBuffer->GetGPUVirtualAddress();
                triangles.IndexBuffer += mesh.GetIndexStart() * sizeof(UINT);
                triangles.IndexCount = mesh.GetIndexCount();
                triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
                desc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
            }
            matrixIndex++;
        }
    }


    void DxrModelActor::CreateMatrixBufferBLAS(UINT matrixCount) {
        // 行列データは各フレームで更新されるため、アクセスする範囲を切り替えて使う.
        const auto matrixStride = UINT(sizeof(XMFLOAT3X4));
        const auto matrixCountAll = matrixCount * m_device->BackBufferCount;
        auto bufferSize = matrixStride * matrixCountAll;

        m_blasMatrices = util::CreateBuffer(m_device, bufferSize, nullptr, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, L"MatrixBuffer(BLAS)");

        // DXR のシェーダー内からのアクセスで使うためにSRVを生成.
        auto countAsFloat4 = matrixCountAll * 3;
        m_blasMatrixDescriptor = util::CreateStructuredSRV(m_device, m_blasMatrices.Get(), countAsFloat4, 0, sizeof(XMFLOAT4));
    }

    void DxrModelActor::UpdateMatrices() {
        for (auto& node : m_nodes) {
            node->UpdateMatrixHierarchy(m_mtxWorld);
        }
    }

    void DxrModelActor::UpdateShapes()
    {
        // 子供ノードのWorld行列をBLASの行列として使いたいため、いったん単位行列で更新.
        auto mtxI = XMMatrixIdentity();
        for (auto& node : m_nodes) {
            node->UpdateMatrixHierarchy(mtxI);
        }
    }

    void DxrModelActor::Node::UpdateLocalMatrix()
    {
        auto mtxT = XMMatrixTranslationFromVector(translation);
        auto mtxR = XMMatrixRotationQuaternion(rotation);
        auto mtxS = XMMatrixScalingFromVector(scale);
        mtxLocal = mtxS * mtxR * mtxT;
    }

    void DxrModelActor::Node::UpdateWorldMatrix(XMMATRIX mtxParent)
    {
        mtxWorld = mtxLocal * mtxParent;
    }

    void DxrModelActor::Node::UpdateMatrixHierarchy(XMMATRIX mtxParent)
    {
        UpdateLocalMatrix();
        UpdateWorldMatrix(mtxParent);
        for (auto& child : children) {
            child->UpdateMatrixHierarchy(this->mtxWorld);
        }
    }


    UINT DxrModelActor::GetSkinVertexCount() const
    {
        if (IsSkinned()) {
            return m_skinInfo.skinVertexCount;
        }
        return 0;
    }

    DxrModelActor::BufferResource DxrModelActor::GetDestPositionBuffer() const
    {
        if (IsSkinned()) {
            return m_skinInfo.vbPositionTransformed;
        }
        return nullptr;
    }

    DxrModelActor::BufferResource DxrModelActor::GetDestNormalBuffer() const
    {
        if (IsSkinned()) {
            return m_skinInfo.vbNormalTransformed;
        }
        return nullptr;
    }
    DxrModelActor::BufferResource DxrModelActor::GetJointMatrixBuffer() const {
        if (IsSkinned()) {
            return m_skinInfo.bufJointMatrices;
        }
        return nullptr;
    }

    dx12::Descriptor DxrModelActor::GetJointMatrixDescriptor() const
    {
        if (IsSkinned()) {
            auto writeIndex = GetWriteIndex();
            return m_skinInfo.bufJointMatricesDescriptors[writeIndex];
        }
        return dx12::Descriptor();
    }



}
