#pragma once

#include "../D3D12/d3dUtil.h"

class IblMaps
{
public:
    bool Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);

    ID3D12Resource* Irradiance() const { return mIrradiance.Get(); }
    ID3D12Resource* Prefilter() const { return mPrefilter.Get(); }
    ID3D12Resource* BrdfLut() const { return mBrdfLut.Get(); }
    UINT PrefilterMipCount() const { return mPrefilterMips; }

private:
    bool CreateCube(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList,
        UINT size, UINT mips, const float* rgba,
        Microsoft::WRL::ComPtr<ID3D12Resource>& texture,
        Microsoft::WRL::ComPtr<ID3D12Resource>& upload);
    bool CreateLut(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList,
        UINT size, const float* rgba,
        Microsoft::WRL::ComPtr<ID3D12Resource>& texture,
        Microsoft::WRL::ComPtr<ID3D12Resource>& upload);

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> mIrradiance;
    Microsoft::WRL::ComPtr<ID3D12Resource> mPrefilter;
    Microsoft::WRL::ComPtr<ID3D12Resource> mBrdfLut;
    Microsoft::WRL::ComPtr<ID3D12Resource> mIrradianceUpload;
    Microsoft::WRL::ComPtr<ID3D12Resource> mPrefilterUpload;
    Microsoft::WRL::ComPtr<ID3D12Resource> mBrdfUpload;
    UINT mPrefilterMips = 1;
};
