#pragma once

#include "../../../Common/D3D12/d3dUtil.h"
#include "../../../Common/Core/GameTimer.h"
#include "../../../Common/Scene/GameObject.h"
#include "../../../Common/Assets/TextureManager.h"
#include "../../../Common/Assets/Texture.h"
#include <memory>
#include <vector>

class ChandelierObject : public GameObject
{
public:
    ChandelierObject();
    explicit ChandelierObject(const std::string& name);
    virtual ~ChandelierObject() = default;

    void SetSwing(float amplitude, float frequency, float phase);
    bool InitializeMesh(
        ID3D12Device* device,
        ID3D12GraphicsCommandList* cmdList,
        TextureManager* textureManager,
        const std::vector<ObjVertex>& vertices,
        const std::vector<uint16_t>& indices,
        const DirectX::XMFLOAT3& lightLocal,
        const DirectX::BoundingBox& localBounds);

    DirectX::XMFLOAT3 GetLightPosition() const;
    DirectX::XMFLOAT3 GetLightDirection() const;

    virtual bool Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList) override;
    virtual bool Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, TextureManager* textureManager) override;
    virtual void Update(const GameTimer& gt) override;
    virtual void Draw(ID3D12GraphicsCommandList* cmdList) override;
    virtual void DrawShadow(ID3D12GraphicsCommandList* cmdList) override;
    virtual void UpdateConstantBuffer(DirectX::FXMMATRIX view, DirectX::FXMMATRIX proj) override;

private:
    std::shared_ptr<MeshGeometry> mGeometry;
    std::unique_ptr<UploadBuffer<ObjectConstants>> mObjectCB;
    std::shared_ptr<Texture> mTexture;
    DirectX::XMFLOAT3 mLightLocal = { 0.0f, -110.0f, 0.0f };
    float mAmplitude = 0.44f;
    float mFrequency = 0.85f;
    float mPhase = 0.0f;
};
