#pragma once

#include "../../../Common/D3D12/d3dUtil.h"
#include "../../../Common/Core/GameTimer.h"
#include "../../../Common/Assets/tiny_obj_loader.h"
#include "../../../Common/Scene/GameObject.h"
#include "../../../Common/Assets/TextureManager.h"
#include "../../../Common/Assets/Texture.h"
#include "../../../Common/Assets/MathHelper.h"
#include "../../../Common/Core/Config.h"
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>

class SponzaObject : public GameObject
{
public:
    SponzaObject();
    explicit SponzaObject(const std::string& name);
    virtual ~SponzaObject() = default;

    virtual bool Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, TextureManager* textureManager);
    virtual void Update(const GameTimer& gt) override;
    virtual void Draw(ID3D12GraphicsCommandList* cmdList) override;
    virtual void DrawShadow(ID3D12GraphicsCommandList* cmdList) override;
    virtual void UpdateConstantBuffer(DirectX::FXMMATRIX view, DirectX::FXMMATRIX proj) override;
    virtual bool UsesTessellation() const override { return true; }
    virtual void SetFrameCamera(const DirectX::XMFLOAT3& pos) override;
    UINT GetBakedTriangleCount() const { return mBakedTriangleCount; }

    struct ExtractedChandelier
    {
        std::vector<ObjVertex> Vertices;
        std::vector<uint16_t> Indices;
        DirectX::XMFLOAT3 HangPivot = {};
        DirectX::XMFLOAT3 LightLocal = {};
        DirectX::BoundingBox LocalBounds = {};
    };

    const std::vector<ExtractedChandelier>& GetExtractedChandeliers() const { return mExtractedChandeliers; }

private:
    struct HeightMapCpu
    {
        UINT Width = 0;
        UINT Height = 0;
        std::vector<uint8_t> Lum;
        float Sample(float u, float v) const;
    };

    struct MaterialDrawBatch
    {
        UINT StartIndexLocation = 0;
        UINT IndexCount = 0;
        UINT BakedStartIndexLocation = 0;
        UINT BakedIndexCount = 0;
        std::shared_ptr<Texture> DiffuseTexture;
        std::shared_ptr<Texture> NormalTexture;
        std::shared_ptr<Texture> DisplacementTexture;
        std::shared_ptr<HeightMapCpu> HeightCpu;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> SrvHeap;
        DirectX::XMFLOAT4 DiffuseFactor = { 1.0f, 1.0f, 1.0f, 1.0f };
        float Metallic = 0.0f;
        float Roughness = 0.55f;
    };

    bool LoadModel(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);
    bool BuildBatchHeap(ID3D12Device* device, MaterialDrawBatch& batch);
    std::shared_ptr<Texture> LoadDisplacement(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const std::string& albedoPath);
    std::shared_ptr<HeightMapCpu> HeightCpuFor(const std::string& albedoPath) const;
    bool BakeTessellation(
        ID3D12Device* device,
        ID3D12GraphicsCommandList* cmdList,
        const std::vector<TessVertex>& srcVertices,
        const std::vector<uint32_t>& srcIndices,
        UINT factor);

private:
    std::unique_ptr<MeshGeometry> mGeometry = nullptr;
    std::unique_ptr<UploadBuffer<ObjectConstants>> mObjectCB = nullptr;
    TextureManager* mTextureManager = nullptr;
    std::vector<MaterialDrawBatch> mMaterialBatches;
    DirectX::XMFLOAT4X4 mWorldViewProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 mWorld = MathHelper::Identity4x4();
    DirectX::XMFLOAT2 mUVScale = { 1.0f, 1.0f };
    DirectX::XMFLOAT2 mUVOffset = { 0.0f, 0.0f };
    DirectX::XMFLOAT3 mCameraWorld = { 0.0f, 0.0f, 0.0f };
    std::shared_ptr<Texture> mFlatNormal;
    std::shared_ptr<Texture> mFlatHeight;
    std::unordered_map<std::string, std::shared_ptr<Texture>> mDisplacementCache;
    std::unordered_map<std::string, std::shared_ptr<HeightMapCpu>> mHeightCpuCache;
    std::shared_ptr<HeightMapCpu> mFlatHeightCpu;
    std::vector<ExtractedChandelier> mExtractedChandeliers;
    std::unique_ptr<MeshGeometry> mBakedGeometry = nullptr;
    bool mBaked = false;
    UINT mBakedTriangleCount = 0;
};
