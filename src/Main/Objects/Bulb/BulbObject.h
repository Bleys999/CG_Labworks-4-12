#pragma once

#include "../../../Common/D3D12/d3dUtil.h"
#include "../../../Common/Core/GameTimer.h"
#include "../../../Common/Scene/GameObject.h"
#include "../../../Common/Assets/TextureManager.h"

class BulbObject : public GameObject
{
public:
    BulbObject();
    explicit BulbObject(const std::string& name);
    virtual ~BulbObject() = default;

    virtual bool Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, TextureManager* textureManager) override;
    virtual void Update(const GameTimer& gt) override;
    virtual void Draw(ID3D12GraphicsCommandList* cmdList) override;
    virtual void UpdateConstantBuffer(DirectX::FXMMATRIX view, DirectX::FXMMATRIX proj) override;

    void SetColor(float r, float g, float b);
    void SetIntensity(float intensity);
    void SetRange(float range);

    DirectX::XMFLOAT4 GetLightPosRange() const;
    DirectX::XMFLOAT4 GetLightColorIntensity() const;

private:
    DirectX::XMFLOAT3 mColor = { 1.0f, 0.98f, 0.92f };
    float mIntensity = 12.0f;
    float mRange = 14.0f;
};
