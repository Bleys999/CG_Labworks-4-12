#include "Texture.h"
#include <vector>
#include <wincodec.h>

namespace
{
    bool CopyWicPixels(IWICBitmapSource* source, UINT stride, UINT size, BYTE* dst)
    {
        if (!source || !dst || size == 0)
            return false;
        __try
        {
            return SUCCEEDED(source->CopyPixels(nullptr, stride, size, dst));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    HRESULT CreateWicBitmap(IWICImagingFactory* factory, IWICBitmapSource* source, IWICBitmap** out)
    {
        if (!factory || !source || !out)
            return E_INVALIDARG;
        *out = nullptr;
        __try
        {
            return factory->CreateBitmapFromSource(source, WICBitmapCacheOnLoad, out);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return E_FAIL;
        }
    }
}

#pragma comment(lib, "windowscodecs.lib")

using namespace DirectX;

Texture::Texture()
    : mWidth(0)
    , mHeight(0)
{
}

Texture::Texture(const std::string& filename)
    : mFilename(filename)
    , mWidth(0)
    , mHeight(0)
{
}

Texture::~Texture()
{
    Shutdown();
}

bool Texture::Load(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const std::string& filename)
{
    mFilename = filename;
    return LoadFromFile(device, cmdList, filename);
}

bool Texture::Create1x1RGBA8(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    const BYTE pix[4] = { r, g, b, a };
    mFilename.clear();
    return UploadTexture2D(device, cmdList, 1, 1, pix);
}

void Texture::Shutdown()
{
    mTexture.Reset();
    mUploadBuffer.Reset();
    mDescriptorHeap.Reset();
}

bool Texture::UploadTexture2D(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, UINT width, UINT height, const void* rgbaPixels)
{
    mWidth = width;
    mHeight = height;

    UINT bufferSize = width * height * 4;

    D3D12_RESOURCE_DESC textureDesc = {};
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Width = width;
    textureDesc.Height = height;
    textureDesc.DepthOrArraySize = 1;
    textureDesc.MipLevels = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.SampleDesc.Quality = 0;
    textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ThrowIfFailed(device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &textureDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&mTexture)));

    UINT64 uploadBufferSize = GetRequiredIntermediateSize(mTexture.Get(), 0, 1);

    ThrowIfFailed(device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&mUploadBuffer)));

    D3D12_SUBRESOURCE_DATA textureData = {};
    textureData.pData = rgbaPixels;
    textureData.RowPitch = width * 4;
    textureData.SlicePitch = bufferSize;

    UpdateSubresources(cmdList, mTexture.Get(), mUploadBuffer.Get(), 0, 0, 1, &textureData);

    cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mTexture.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));

    CreateSRV(device);

    return true;
}

bool Texture::CreateFromRGBA8(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, UINT width, UINT height, const void* rgbaPixels)
{
    mFilename.clear();
    return UploadTexture2D(device, cmdList, width, height, rgbaPixels);
}

bool Texture::DecodeImageRGBA(const std::string& filename, std::vector<uint8_t>& outRgba, UINT& width, UINT& height)
{
    std::wstring wfilename(filename.begin(), filename.end());

    Microsoft::WRL::ComPtr<IWICImagingFactory> wicFactory;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&wicFactory));

    if (FAILED(hr))
        return false;

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    hr = wicFactory->CreateDecoderFromFilename(
        wfilename.c_str(),
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnLoad,
        &decoder);

    if (FAILED(hr))
        return false;

    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr))
        return false;

    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    hr = wicFactory->CreateFormatConverter(&converter);
    if (FAILED(hr))
        return false;

    hr = converter->Initialize(
        frame.Get(),
        GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0f,
        WICBitmapPaletteTypeCustom);

    if (FAILED(hr))
        return false;

    UINT w = 0;
    UINT h = 0;
    hr = converter->GetSize(&w, &h);
    if (FAILED(hr) || w == 0 || h == 0 || w > 16384 || h > 16384)
        return false;

    const UINT64 byteCount64 = (UINT64)w * h * 4;
    if (byteCount64 > 0xffffffffu)
        return false;
    const UINT byteCount = (UINT)byteCount64;

    Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
    hr = CreateWicBitmap(wicFactory.Get(), converter.Get(), &bitmap);
    if (FAILED(hr))
        return false;

    outRgba.resize(byteCount);
    if (!CopyWicPixels(bitmap.Get(), w * 4, byteCount, outRgba.data()))
        return false;

    for (UINT i = 0; i + 3 < byteCount; i += 4)
    {
        const uint8_t b = outRgba[i];
        outRgba[i] = outRgba[i + 2];
        outRgba[i + 2] = b;
    }

    width = w;
    height = h;
    return true;
}

bool Texture::LoadFromFile(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const std::string& filename)
{
    std::vector<BYTE> pixels;
    UINT width = 0;
    UINT height = 0;
    if (!DecodeImageRGBA(filename, pixels, width, height))
        return false;
    return UploadTexture2D(device, cmdList, width, height, pixels.data());
}

void Texture::CreateSRV(ID3D12Device* device)
{
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = 1;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    ThrowIfFailed(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mDescriptorHeap)));

    mCpuHandle = mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    mGpuHandle = mDescriptorHeap->GetGPUDescriptorHandleForHeapStart();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = mTexture->GetDesc().Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    device->CreateShaderResourceView(mTexture.Get(), &srvDesc, mCpuHandle);
}
