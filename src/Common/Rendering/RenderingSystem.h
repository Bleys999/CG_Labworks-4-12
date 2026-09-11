#pragma once

#include "GBuffer.h"
#include "../D3D12/d3dUtil.h"
#include "../Scene/Camera.h"
#include <memory>
#include <vector>

static constexpr UINT kMaxPointLights = 8;
static constexpr UINT kMaxSpotLights = 4;

struct DeferredLightingConstants
{
    DirectX::XMFLOAT4 DirectionalDir = { 0.35f, -0.85f, 0.35f, 0.0f };
    DirectX::XMFLOAT4 DirectionalColor = { 1.0f, 0.95f, 0.85f, 0.55f };
    DirectX::XMFLOAT4 AmbientColor = { 0.06f, 0.06f, 0.07f, 1.0f };
    DirectX::XMFLOAT4 CameraWorld = { 0.0f, 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT4 CameraLook = { 0.0f, 0.0f, 1.0f, 0.0f };
    UINT PointCount = 0;
    UINT SpotCount = 0;
    UINT DebugMode = 0;
    UINT Pad = 0;
    DirectX::XMFLOAT4 PointPosRange[kMaxPointLights] = {};
    DirectX::XMFLOAT4 PointColorIntensity[kMaxPointLights] = {};
    DirectX::XMFLOAT4 SpotPosRange[kMaxSpotLights] = {};
    DirectX::XMFLOAT4 SpotDirCosine[kMaxSpotLights] = {};
    DirectX::XMFLOAT4 SpotColorIntensity[kMaxSpotLights] = {};
    DirectX::XMFLOAT4X4 CascadeViewProj[4] = {};
    DirectX::XMFLOAT4 CascadeSplits = {};
    DirectX::XMFLOAT4 ShadowParams = {};
};

struct ShadowPassConstants
{
    DirectX::XMFLOAT4X4 LightViewProj = MathHelper::Identity4x4();
};

class RenderingSystem
{
public:
    bool Initialize(ID3D12Device* device);
    void Resize(ID3D12Device* device, UINT width, UINT height);

    GBuffer& GetGBuffer() { return mGBuffer; }

    void ApplyGeometryPass(ID3D12GraphicsCommandList* cmdList) const;
    ID3D12PipelineState* GetGeometryPSO() const { return mGeometryPSO.Get(); }

    void ApplyTessellationPass(ID3D12GraphicsCommandList* cmdList) const;
    ID3D12PipelineState* GetTessellationPSO() const { return mTessellationPSO.Get(); }

    void ApplyShadowPass(ID3D12GraphicsCommandList* cmdList) const;
    void SetShadowCascadeTarget(ID3D12GraphicsCommandList* cmdList, UINT cascadeIndex) const;
    void BeginShadowPass(ID3D12GraphicsCommandList* cmdList);
    void EndShadowPass(ID3D12GraphicsCommandList* cmdList);
    DirectX::XMMATRIX GetCascadeView(UINT cascadeIndex) const;
    DirectX::XMMATRIX GetCascadeProj(UINT cascadeIndex) const;

    void SetShadowDebugMode(UINT mode);
    UINT GetShadowDebugMode() const { return mLightingCpu.DebugMode; }
    void SetShadowCasterBounds(const DirectX::BoundingBox& bounds);

    void UpdateLightingConstants(const Camera& camera);
    void DrawDeferredLighting(
        ID3D12GraphicsCommandList* cmdList,
        D3D12_CPU_DESCRIPTOR_HANDLE backBufferRtv,
        const D3D12_VIEWPORT& viewport,
        const D3D12_RECT& scissor) const;

private:
    void BuildGeometryPass(ID3D12Device* device);
    void BuildTessellationPass(ID3D12Device* device);
    void BuildShadowPass(ID3D12Device* device);
    void BuildLightingPass(ID3D12Device* device);
    void InitDefaultLights();
    void CreateShadowMaps(ID3D12Device* device);
    void UpdateLightingSrvs(ID3D12Device* device);
    void UpdateShadowCascades(const Camera& camera);

private:
    GBuffer mGBuffer;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> mGeometryRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mGeometryPSO;
    Microsoft::WRL::ComPtr<ID3DBlob> mGeometryVs;
    Microsoft::WRL::ComPtr<ID3DBlob> mGeometryPs;
    std::vector<D3D12_INPUT_ELEMENT_DESC> mGeometryInputLayout;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> mTessellationRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mTessellationPSO;
    Microsoft::WRL::ComPtr<ID3DBlob> mTessVs;
    Microsoft::WRL::ComPtr<ID3DBlob> mTessHs;
    Microsoft::WRL::ComPtr<ID3DBlob> mTessDs;
    Microsoft::WRL::ComPtr<ID3DBlob> mTessPs;
    std::vector<D3D12_INPUT_ELEMENT_DESC> mTessellationInputLayout;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> mLightingRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mLightingPSO;
    Microsoft::WRL::ComPtr<ID3DBlob> mLightingVs;
    Microsoft::WRL::ComPtr<ID3DBlob> mLightingPs;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mLightingSrvHeap;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> mShadowRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mShadowPSO;
    Microsoft::WRL::ComPtr<ID3DBlob> mShadowVs;
    Microsoft::WRL::ComPtr<ID3D12Resource> mShadowMaps;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mShadowDsvHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE mShadowDsv[4] = {};
    D3D12_VIEWPORT mShadowViewport = {};
    D3D12_RECT mShadowScissor = {};
    DirectX::XMFLOAT4X4 mCascadeView[4] = {};
    DirectX::XMFLOAT4X4 mCascadeProj[4] = {};
    std::unique_ptr<UploadBuffer<ShadowPassConstants>> mShadowPassCB;
    bool mShadowShaderRead = false;
    ID3D12Device* mDevice = nullptr;

    std::unique_ptr<UploadBuffer<DeferredLightingConstants>> mLightingCB;

    DeferredLightingConstants mLightingCpu = {};
    DirectX::BoundingBox mShadowCasterBounds = {};
    bool mHasShadowCasterBounds = false;
};
