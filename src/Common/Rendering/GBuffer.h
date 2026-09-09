#pragma once

#include "../D3D12/d3dUtil.h"

class GBuffer
{
public:
    static constexpr UINT ColorTargetCount = 3;
    static constexpr DXGI_FORMAT AlbedoFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    static constexpr DXGI_FORMAT NormalFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    static constexpr DXGI_FORMAT WorldPosFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
    static constexpr DXGI_FORMAT DepthFormat = DXGI_FORMAT_D32_FLOAT;

    GBuffer() = default;
    ~GBuffer() = default;

    bool Resize(ID3D12Device* device, UINT width, UINT height);
    void ReleaseSizeDependentResources();

    void TransitionToGeometryWrite(ID3D12GraphicsCommandList* cmdList);
    void TransitionToShaderRead(ID3D12GraphicsCommandList* cmdList);

    void Clear(ID3D12GraphicsCommandList* cmdList, const float albedoClear[4]);
    void BeginGeometryPass(ID3D12GraphicsCommandList* cmdList, const float albedoClear[4]);
    void EndGeometryPass(ID3D12GraphicsCommandList* cmdList);

    D3D12_CPU_DESCRIPTOR_HANDLE AlbedoRTV() const { return mAlbedoRtv; }
    D3D12_CPU_DESCRIPTOR_HANDLE NormalRTV() const { return mNormalRtv; }
    D3D12_CPU_DESCRIPTOR_HANDLE WorldPosRTV() const { return mWorldPosRtv; }
    D3D12_CPU_DESCRIPTOR_HANDLE DepthDSV() const { return mDepthDsv; }

    ID3D12DescriptorHeap* SrvHeap() const { return mSrvHeap.Get(); }
    D3D12_GPU_DESCRIPTOR_HANDLE SrvGpuStart() const { return mSrvGpuStart; }

    ID3D12Resource* AlbedoResource() const { return mAlbedo.Get(); }
    ID3D12Resource* NormalResource() const { return mNormal.Get(); }
    ID3D12Resource* WorldPosResource() const { return mWorldPos.Get(); }

    UINT Width() const { return mWidth; }
    UINT Height() const { return mHeight; }

private:
    void CreateSrvs(ID3D12Device* device);

private:
    UINT mWidth = 0;
    UINT mHeight = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> mAlbedo;
    Microsoft::WRL::ComPtr<ID3D12Resource> mNormal;
    Microsoft::WRL::ComPtr<ID3D12Resource> mWorldPos;
    Microsoft::WRL::ComPtr<ID3D12Resource> mDepthStencil;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mRtvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mDsvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mSrvHeap;

    D3D12_CPU_DESCRIPTOR_HANDLE mAlbedoRtv{};
    D3D12_CPU_DESCRIPTOR_HANDLE mNormalRtv{};
    D3D12_CPU_DESCRIPTOR_HANDLE mWorldPosRtv{};
    D3D12_CPU_DESCRIPTOR_HANDLE mDepthDsv{};

    D3D12_CPU_DESCRIPTOR_HANDLE mSrvCpuStart{};
    D3D12_GPU_DESCRIPTOR_HANDLE mSrvGpuStart{};

    UINT mRtvDescriptorSize = 0;
    UINT mDsvDescriptorSize = 0;
    UINT mCbvSrvUavDescriptorSize = 0;

    bool mColorTargetsShaderRead = false;
};
