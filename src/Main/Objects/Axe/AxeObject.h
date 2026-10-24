#pragma once

#include "../../../Common/D3D12/d3dUtil.h"
#include "../../../Common/Core/GameTimer.h"
#include "../../../Common/Scene/GameObject.h"
#include "../../../Common/Assets/TextureManager.h"
#include "../../../Common/Assets/Texture.h"
#include <memory>

class RenderingSystem;

class AxeObject : public GameObject
{
public:
    AxeObject();
    explicit AxeObject(const std::string& name);
    virtual ~AxeObject() = default;

    void SetRenderingSystem(RenderingSystem* rendering) { mRendering = rendering; }

    virtual bool Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, TextureManager* textureManager) override;
    virtual void Update(const GameTimer& gt) override;
    virtual void Draw(ID3D12GraphicsCommandList* cmdList) override;
    virtual void DrawShadow(ID3D12GraphicsCommandList* cmdList) override;
    virtual void UpdateConstantBuffer(DirectX::FXMMATRIX view, DirectX::FXMMATRIX proj) override;

private:
    bool LoadMesh(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);
    bool LoadMaps(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, TextureManager* textureManager);
    bool BuildPipeline(ID3D12Device* device);

private:
    struct AxeVertex
    {
        DirectX::XMFLOAT3 Pos;
        DirectX::XMFLOAT3 Normal;
        DirectX::XMFLOAT2 TexC;
        DirectX::XMFLOAT4 Tangent;
    };

    RenderingSystem* mRendering = nullptr;
    std::shared_ptr<MeshGeometry> mGeometry;
    std::unique_ptr<UploadBuffer<ObjectConstants>> mObjectCB;
    std::shared_ptr<Texture> mBase;
    std::shared_ptr<Texture> mNormal;
    std::shared_ptr<Texture> mMetallic;
    std::shared_ptr<Texture> mRoughness;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mHeap;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> mRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mPSO;
    UINT mIndexCount = 0;
};
