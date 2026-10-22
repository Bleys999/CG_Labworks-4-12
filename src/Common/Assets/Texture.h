#pragma once

#include "../D3D12/d3dUtil.h"
#include <cstdint>
#include <string>
#include <memory>
#include <vector>

class Texture
{
public:
    Texture();
    explicit Texture(const std::string& filename);
    ~Texture();

    bool Load(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const std::string& filename);
    bool Create1x1RGBA8(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    bool CreateFromRGBA8(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, UINT width, UINT height, const void* rgbaPixels);
    static bool DecodeImageRGBA(const std::string& filename, std::vector<uint8_t>& outRgba, UINT& width, UINT& height);
    void Shutdown();
    void ReleaseUpload() { mUploadBuffer.Reset(); }

    ID3D12Resource* GetResource() const { return mTexture.Get(); }
    ID3D12DescriptorHeap* GetDescriptorHeap() const { return mDescriptorHeap.Get(); }
    D3D12_GPU_DESCRIPTOR_HANDLE GetSRV() const { return mGpuHandle; }
    const std::string& GetFilename() const { return mFilename; }
    UINT GetWidth() const { return mWidth; }
    UINT GetHeight() const { return mHeight; }

private:
    bool UploadTexture2D(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, UINT width, UINT height, const void* rgbaPixels);
    bool LoadFromFile(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const std::string& filename);
    void CreateSRV(ID3D12Device* device);

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> mTexture;
    Microsoft::WRL::ComPtr<ID3D12Resource> mUploadBuffer;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mDescriptorHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE mCpuHandle;
    D3D12_GPU_DESCRIPTOR_HANDLE mGpuHandle;
    std::string mFilename;
    UINT mWidth;
    UINT mHeight;
};