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

private:
    struct MaterialDrawBatch
    {
        UINT StartIndexLocation = 0;
        UINT IndexCount = 0;
        std::shared_ptr<Texture> DiffuseTexture;
        std::shared_ptr<Texture> NormalTexture;
        std::shared_ptr<Texture> DisplacementTexture;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> SrvHeap;
        DirectX::XMFLOAT4 DiffuseFactor = { 1.0f, 1.0f, 1.0f, 1.0f };
        float Metallic = 0.0f;
        float Roughness = 0.55f;
    };

    bool LoadModel(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);
    bool BuildBatchHeap(ID3D12Device* device, MaterialDrawBatch& batch);
    std::shared_ptr<Texture> LoadDisplacement(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const std::string& albedoPath);

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
};
