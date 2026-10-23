#include "BulbObject.h"

using namespace DirectX;

BulbObject::BulbObject() : GameObject("Bulb")
{
    SetCullable(false);
}

BulbObject::BulbObject(const std::string& name) : GameObject(name)
{
    SetCullable(false);
}

void BulbObject::SetColor(float r, float g, float b)
{
    mColor = XMFLOAT3(r, g, b);
}

void BulbObject::SetIntensity(float intensity)
{
    mIntensity = intensity;
}

void BulbObject::SetRange(float range)
{
    mRange = range;
}

XMFLOAT4 BulbObject::GetLightPosRange() const
{
    return XMFLOAT4(mPosition.x, mPosition.y, mPosition.z, mRange);
}

XMFLOAT4 BulbObject::GetLightColorIntensity() const
{
    return XMFLOAT4(mColor.x, mColor.y, mColor.z, mIntensity);
}

bool BulbObject::Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, TextureManager* textureManager)
{
    return true;
}

void BulbObject::Update(const GameTimer& gt)
{
}

void BulbObject::UpdateConstantBuffer(FXMMATRIX view, FXMMATRIX proj)
{
}

void BulbObject::Draw(ID3D12GraphicsCommandList* cmdList)
{
}
