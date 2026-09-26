#pragma once

#include "../../../Common/D3D12/d3dUtil.h"
#include "../../../Common/Core/GameTimer.h"
#include "../../../Common/Scene/GameObject.h"
#include "../../../Common/Assets/TextureManager.h"
#include "../../../Common/Assets/Texture.h"
#include <memory>
#include <array>

class BoxObject : public GameObject
{
public:
    BoxObject();
    explicit BoxObject(const std::string& name);
    virtual ~BoxObject() = default;

    virtual bool Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList) override;
    virtual bool Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, TextureManager* textureManager) override;
    virtual void Update(const GameTimer& gt) override;
    virtual void Draw(ID3D12GraphicsCommandList* cmdList) override;
    virtual void DrawShadow(ID3D12GraphicsCommandList* cmdList) override;
    virtual void UpdateConstantBuffer(DirectX::FXMMATRIX view, DirectX::FXMMATRIX proj) override;

    void SetDiffuseFactor(const DirectX::XMFLOAT4& color) { mDiffuseFactor = color; }
    void SetMetallic(float metallic) { mMetallic = metallic; }
    void SetRoughness(float roughness) { mRoughness = roughness; }

private:
    bool CreateSharedGeometry(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);

private:
    std::shared_ptr<MeshGeometry> mGeometry;
    std::unique_ptr<UploadBuffer<ObjectConstants>> mObjectCB;
    std::shared_ptr<Texture> mTexture;
    DirectX::XMFLOAT4 mDiffuseFactor = { 1.0f, 1.0f, 1.0f, 1.0f };
    float mMetallic = 0.0f;
    float mRoughness = 0.55f;

    static std::shared_ptr<MeshGeometry> sSharedGeometry;
    static const std::array<Vertex, 8> sVertices;
    static const std::array<std::uint16_t, 36> sIndices;
};
