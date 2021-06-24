#pragma once

#include <DirectXMath.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <wrl.h>
#include <stdexcept>

#include "GraphicsDevice.h"
#include "util/TextureResource.h"
#include "util/DxrBookUtility.h"

namespace tinygltf {
    class Node;
    class Model;
    struct Mesh;
}
namespace util {

    class DxrModelActor;


    class DxrModel {
    public:
        using XMFLOAT2 = DirectX::XMFLOAT2;
        using XMFLOAT3 = DirectX::XMFLOAT3;
        using XMFLOAT4 = DirectX::XMFLOAT4;
        using XMMATRIX = DirectX::XMMATRIX;
        using XMVECTOR = DirectX::XMVECTOR;
        using XMUINT4 = DirectX::XMUINT4;

        template<class T>
        using ComPtr = Microsoft::WRL::ComPtr<T>;
        using D3D12Resource = ComPtr<ID3D12Resource>;

        DxrModel();
        ~DxrModel();

        void Destroy(std::unique_ptr<dx12::GraphicsDevice>& device);

        bool LoadFromGltf(
            const std::wstring& fileName,
            std::unique_ptr<dx12::GraphicsDevice>& device);


        std::shared_ptr<DxrModelActor> Create(std::unique_ptr<dx12::GraphicsDevice>& device);

        class Node {
        public:
            Node();
            ~Node();

        private:
            std::wstring name;
            XMVECTOR translation;
            XMVECTOR rotation;
            XMVECTOR scale;
            XMMATRIX mtxLocal;
            XMMATRIX mtxWorld;
            std::vector<int> children;
            Node* parent = nullptr;
            int meshIndex = -1;
            friend class DxrModel;
        };
        class Mesh {
        private:
            UINT indexStart;
            UINT vertexStart;
            UINT indexCount;
            UINT vertexCount;
            UINT materialIndex;

            friend class DxrModel;
        };
        class MeshGroup {
        private:
            std::vector<Mesh> m_meshes;
            int m_nodeIndex;
            friend class DxrModel;
        };

        class Skin {
        private:
            std::wstring name;
            std::vector<int> m_joints;
            std::vector<XMMATRIX> invBindMatices;
            friend class DxrModel;
        };

        class Material {
        public:
            Material() : m_name(), m_textureIndex(-1), m_diffuseColor(1, 1, 1) { }
            std::wstring GetName() const { return m_name; }
            int GetTextureIndex() const { return m_textureIndex; }
            XMFLOAT3 GetDiffuseColor() const { return m_diffuseColor; }
        private:
            std::wstring m_name;
            int m_textureIndex;
            XMFLOAT3 m_diffuseColor;

            friend class DxrModel;
        };

        D3D12Resource GetPositionBuffer() const { return m_vertexAttrib.Position; }
        D3D12Resource GetNormalBuffer() const { return m_vertexAttrib.Normal; }
        D3D12Resource GetIndexBuffer() const { return m_indexBuffer; }

        D3D12Resource GetJointIndicesBuffer() const { return m_vertexAttrib.JointIndices; }
        D3D12Resource GetJointWeightsBuffer() const { return m_vertexAttrib.JointWeights; }

    private:
        struct VertexAttributeVisitor {
            std::vector<UINT> indexBuffer;
            std::vector<XMFLOAT3> positionBuffer;
            std::vector<XMFLOAT3> normalBuffer;
            std::vector<XMFLOAT2> texcoordBuffer;

            std::vector<XMUINT4> jointBuffer;
            std::vector<XMFLOAT4> weightBuffer;
        };

        void LoadNode(const tinygltf::Model& inModel);
        void LoadMesh(const tinygltf::Model& inModel, VertexAttributeVisitor& visitor);

        void LoadSkin(const tinygltf::Model& inModel);
        void LoadMaterial(const tinygltf::Model& inModel);
        
        // 各頂点属性ごとのバッファ(ストリーム)
        struct VertexAttribute
        {
            D3D12Resource Position;
            D3D12Resource Normal;
            D3D12Resource Texcoord;
            D3D12Resource JointIndices;
            D3D12Resource JointWeights;
        } m_vertexAttrib;
        D3D12Resource m_indexBuffer;

        std::vector<util::TextureResource> m_textures;

        std::vector<MeshGroup> m_meshGroups;
        std::vector<Material> m_materials;
        std::vector<std::shared_ptr<Node>> m_nodes;
        std::vector<int> m_rootNodes; // シーンルートに存在するノードのインデックス値.

        struct SkinInfo {
            std::wstring name;
            std::vector<int> joints;
            std::vector<XMMATRIX> invBindMatrices;
            UINT skinVertexCount;
        } m_skinInfo;
        bool m_hasSkin = false;

        util::TextureResource m_whiteTex;

        friend class DxrModelActor;
    };

    class DxrModelActor {
    public:
        using XMFLOAT2 = DirectX::XMFLOAT2;
        using XMFLOAT3 = DirectX::XMFLOAT3;
        using XMFLOAT4 = DirectX::XMFLOAT4;
        using XMMATRIX = DirectX::XMMATRIX;
        using XMVECTOR = DirectX::XMVECTOR;
        using XMUINT4 = DirectX::XMUINT4;

        template<class T>
        using ComPtr = Microsoft::WRL::ComPtr<T>;
        using BLASResource = ComPtr<ID3D12Resource>;
        using BufferResource = ComPtr<ID3D12Resource>;
        using D3D12Resource = ComPtr<ID3D12Resource>;

        ~DxrModelActor();
        class Node;
        class Material;
        using SpNode = std::shared_ptr<Node>;
        using SpMaterial = std::shared_ptr<Material>;

        class Node {
        public:
            Node();
            ~Node();
            const std::wstring GetName() const { return name; }

            void UpdateLocalMatrix();
            void UpdateWorldMatrix(XMMATRIX mtxParent);

            void UpdateMatrixHierarchy(XMMATRIX mtxParent);

            XMMATRIX GetWorldMatrix() { return mtxWorld; }
            XMMATRIX GetLocalMatrix() { return mtxLocal; }
            auto GetParent() const { return parent; }

            void SetTranslation(XMVECTOR t) { translation = t; }
            void SetRotation(XMVECTOR r) { rotation = r; }
            void SetScale(XMVECTOR s) { scale = s; }
        private:
            XMVECTOR translation;
            XMVECTOR rotation;
            XMVECTOR scale;
            XMMATRIX mtxLocal;
            XMMATRIX mtxWorld;
            std::wstring name;
            std::weak_ptr<Node> parent;
            std::vector<SpNode> children;
            std::vector<UINT> m_meshes;
            int m_meshGroupIndex = -1;

            friend class DxrModel;
            friend class DxrModelActor;
        };

        class Material {
        public:
            Material(std::unique_ptr<dx12::GraphicsDevice>& device, const DxrModel::Material& src);
            ~Material();

            void SetHitgroup(const std::wstring& hitgroup) { m_hitgroup = hitgroup; }
            std::wstring GetHitgroup() const { return m_hitgroup; }

            D3D12Resource GetBuffer() const { return m_bufferCB; }
            dx12::Descriptor GetTextureDescriptor() const { return m_texture.srv; }
            dx12::Descriptor GetMaterialDescriptor() const { return m_cbv; }

            void SetTexture(util::TextureResource& resTex);
        private:
            std::wstring m_name;
            struct MaterialParameters {
                XMFLOAT4 diffuse;
            } m_materialParams;
            std::wstring m_hitgroup;
            D3D12Resource m_bufferCB;
            dx12::Descriptor m_cbv;
            util::TextureResource m_texture;

            std::unique_ptr<dx12::GraphicsDevice>& m_device;
            friend class DxrModel;
        };

        class Mesh {
        public:
            UINT GetIndexStart() const { return indexStart; }
            UINT GetIndexCount() const { return indexCount; }
            UINT GetVertexStart() const { return vertexStart; }
            UINT GetVertexCount() const { return vertexCount; }

            dx12::Descriptor GetPosition() const { return vbAttrPosision; }
            dx12::Descriptor GetNormal() const   { return vbAttrNormal; }
            dx12::Descriptor GetTexcoord() const { return vbAttrTexcoord; }
            dx12::Descriptor GetIndexBuffer() const { return indexBuffer; }

            SpMaterial GetMaterial() const { return material; }
            ComPtr<ID3D12Resource> GetMeshParametersCB() const { return meshParameters; }
        private:
            UINT indexStart;
            UINT indexCount;
            UINT vertexStart;
            UINT vertexCount;

            dx12::Descriptor vbAttrPosision;
            dx12::Descriptor vbAttrNormal;
            dx12::Descriptor vbAttrTexcoord;
            dx12::Descriptor indexBuffer;
            SpMaterial material;

            struct MeshParameters {
                XMFLOAT4 diffuse;
                UINT     strideOfMatrixBuffer;
                UINT     meshGroupIndex;
            };
            ComPtr<ID3D12Resource> meshParameters;

            friend class DxrModel;
        };
        class MeshGroup {
        public:
            const SpNode GetNode() const { return m_node; }

            UINT GetMeshCount() const { return UINT(m_meshes.size()); }
            const Mesh& GetMesh(int index) const { return m_meshes[index]; }
        private:
            SpNode m_node;
            std::vector<Mesh> m_meshes;
            friend class DxrModel;
            friend class DxrModelActor;
        };

        DxrModelActor() = delete;

        void SetWorldMatrix(XMMATRIX mtxWorld) { m_mtxWorld = mtxWorld; }
        XMMATRIX GetWorldMatrix() const { return m_mtxWorld; }
        const DxrModel* GetModel() const { return m_modelReference; }

        void UpdateMatrices();
        
        void UpdateShapes();

        BLASResource GetBLAS() const { return m_blas; }

        UINT GetMeshGroupCount() const { return UINT(m_meshGroups.size()); }
        UINT GetMeshCount(int groupIndex) const {
            return UINT(m_meshGroups[groupIndex].m_meshes.size());
        }
        const Mesh& GetMesh(int groupIndex, int meshIndex) const {
            return m_meshGroups[groupIndex].m_meshes[meshIndex];
        }

        UINT GetMeshCountAll() const {
            UINT meshCount = 0;
            for (auto& group : m_meshGroups) {
                meshCount += UINT(group.m_meshes.size());
            }
            return meshCount;
        }

        // スキンモデルであるか.
        bool IsSkinned() const {
            return m_hasSkin;
        }
        UINT GetSkinVertexCount() const;
       
        BufferResource GetDestPositionBuffer() const;
        BufferResource GetDestNormalBuffer() const;
        BufferResource GetJointMatrixBuffer() const;
        dx12::Descriptor GetJointMatrixDescriptor() const;

        // BLAS を更新する.
        void UpdateBLAS(ComPtr<ID3D12GraphicsCommandList4> commandList);

        // 各ワールド行列を GPU のバッファに書き込む.
        void ApplyTransform();

        // 指定ノードの検索.
        std::shared_ptr<Node> SearchNode(const std::wstring& name);


        UINT GetMaterialCount() const { return UINT(m_materials.size()); }
        std::shared_ptr<Material> GetMaterial(UINT idx) const { return m_materials[idx]; }

        dx12::Descriptor GetBLASMatrixDescriptor() {
            return m_blasMatrixDescriptor;
        }
        BufferResource GetBlasMatrices() const {
            return m_blasMatrices;
        }
    private:
        DxrModelActor(std::unique_ptr<dx12::GraphicsDevice>& device, const DxrModel* model);

        void CreateMatrixBufferBLAS(UINT matrixCount);
        void CreateBLAS();
        void CreateRtGeometryDesc(std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>& rtGeomDesc);
        SpNode SearchNode(SpNode node, const std::wstring& name);
        UINT GetWriteIndex() const {
            return m_device->GetCurrentFrameIndex();
        }

        XMMATRIX m_mtxWorld;
        const DxrModel* m_modelReference;

        std::vector<SpNode> m_nodes;
        std::vector<SpMaterial> m_materials;
        std::vector<MeshGroup> m_meshGroups;

        BufferResource m_blas;
        BufferResource m_blasUpdateBuffer;

        ComPtr<ID3D12Resource> m_blasMatrices;
        dx12::Descriptor m_blasMatrixDescriptor;

        struct SkinInfo {
            std::vector<XMMATRIX> invBindMatrices;
            std::vector<SpNode> jointList;

            dx12::Descriptor jointMatricesDescriptor;
            dx12::Descriptor vbPositionDescriptor;
            dx12::Descriptor vbNormalDescriptor;
            std::vector<dx12::Descriptor> bufJointMatricesDescriptors;


            BufferResource   vbPositionTransformed;
            BufferResource   vbNormalTransformed;
            BufferResource   bufJointMatrices;
            UINT skinVertexCount;
        } m_skinInfo;
        bool m_hasSkin = false;
        std::unique_ptr<dx12::GraphicsDevice>& m_device;
        friend class DxrModel;
    };
}
