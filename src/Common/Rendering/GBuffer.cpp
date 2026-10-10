#include "GBuffer.h"

void GBuffer::ReleaseSizeDependentResources()
{
    mAlbedo.Reset();
    mNormal.Reset();
    mDepthStencil.Reset();
    mRtvHeap.Reset();
    mDsvHeap.Reset();
    mSrvHeap.Reset();
    mWidth = 0;
    mHeight = 0;
    mColorTargetsShaderRead = false;
}

bool GBuffer::Resize(ID3D12Device* device, UINT width, UINT height)
{
    if (width == 0 || height == 0)
        return false;

    ReleaseSizeDependentResources();

    mWidth = width;
    mHeight = height;

    mRtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    mDsvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    mCbvSrvUavDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);

    D3D12_RESOURCE_DESC colorDesc = {};
    colorDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    colorDesc.Width = width;
    colorDesc.Height = height;
    colorDesc.DepthOrArraySize = 1;
    colorDesc.MipLevels = 1;
    colorDesc.SampleDesc.Count = 1;
    colorDesc.SampleDesc.Quality = 0;
    colorDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    colorDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE albedoClear = {};
    albedoClear.Format = AlbedoFormat;
    albedoClear.Color[0] = 0.0f;
    albedoClear.Color[1] = 0.0f;
    albedoClear.Color[2] = 0.0f;
    albedoClear.Color[3] = 0.0f;

    colorDesc.Format = AlbedoFormat;
    ThrowIfFailed(device->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &colorDesc,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        &albedoClear,
        IID_PPV_ARGS(&mAlbedo)));

    colorDesc.Format = NormalFormat;
    D3D12_CLEAR_VALUE normalClear = {};
    normalClear.Format = NormalFormat;
    normalClear.Color[0] = 0.5f;
    normalClear.Color[1] = 0.5f;
    normalClear.Color[2] = 1.0f;
    normalClear.Color[3] = 0.0f;
    ThrowIfFailed(device->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &colorDesc,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        &normalClear,
        IID_PPV_ARGS(&mNormal)));

    D3D12_RESOURCE_DESC depthDesc = {};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.DepthOrArraySize = 1;
    depthDesc.MipLevels = 1;
    depthDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.SampleDesc.Quality = 0;
    depthDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE depthClear = {};
    depthClear.Format = DXGI_FORMAT_D32_FLOAT;
    depthClear.DepthStencil.Depth = 1.0f;
    depthClear.DepthStencil.Stencil = 0;

    ThrowIfFailed(device->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &depthDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &depthClear,
        IID_PPV_ARGS(&mDepthStencil)));

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = ColorTargetCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&mRtvHeap)));

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&mDsvHeap)));

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = ColorTargetCount + 1;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvHeap)));

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvH(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
    mAlbedoRtv = rtvH;
    device->CreateRenderTargetView(mAlbedo.Get(), nullptr, mAlbedoRtv);
    rtvH.Offset(1, mRtvDescriptorSize);
    mNormalRtv = rtvH;
    device->CreateRenderTargetView(mNormal.Get(), nullptr, mNormalRtv);

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.Texture2D.MipSlice = 0;
    mDepthDsv = mDsvHeap->GetCPUDescriptorHandleForHeapStart();
    device->CreateDepthStencilView(mDepthStencil.Get(), &dsvDesc, mDepthDsv);

    mSrvCpuStart = mSrvHeap->GetCPUDescriptorHandleForHeapStart();
    mSrvGpuStart = mSrvHeap->GetGPUDescriptorHandleForHeapStart();
    CreateSrvs(device);

    return true;
}

void GBuffer::CreateSrvs(ID3D12Device* device)
{
    CD3DX12_CPU_DESCRIPTOR_HANDLE h(mSrvCpuStart);

    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Format = AlbedoFormat;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;
    srv.Texture2D.MostDetailedMip = 0;
    srv.Texture2D.ResourceMinLODClamp = 0.0f;
    device->CreateShaderResourceView(mAlbedo.Get(), &srv, h);
    h.Offset(1, mCbvSrvUavDescriptorSize);

    srv.Format = NormalFormat;
    device->CreateShaderResourceView(mNormal.Get(), &srv, h);
    h.Offset(1, mCbvSrvUavDescriptorSize);

    srv.Format = DepthSrvFormat;
    device->CreateShaderResourceView(mDepthStencil.Get(), &srv, h);
}

void GBuffer::TransitionToGeometryWrite(ID3D12GraphicsCommandList* cmdList)
{
    if (!mColorTargetsShaderRead)
        return;

    CD3DX12_RESOURCE_BARRIER barriers[3];
    barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(mAlbedo.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(mNormal.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    barriers[2] = CD3DX12_RESOURCE_BARRIER::Transition(mDepthStencil.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    cmdList->ResourceBarrier(3, barriers);
    mColorTargetsShaderRead = false;
}

void GBuffer::TransitionToShaderRead(ID3D12GraphicsCommandList* cmdList)
{
    CD3DX12_RESOURCE_BARRIER barriers[3];
    barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(mAlbedo.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(mNormal.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    barriers[2] = CD3DX12_RESOURCE_BARRIER::Transition(mDepthStencil.Get(),
        D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(3, barriers);
    mColorTargetsShaderRead = true;
}

void GBuffer::Clear(ID3D12GraphicsCommandList* cmdList, const float albedoClear[4])
{
    cmdList->ClearRenderTargetView(mAlbedoRtv, albedoClear, 0, nullptr);
    float nClear[4] = { 0.5f, 0.5f, 1.0f, 1.0f };
    cmdList->ClearRenderTargetView(mNormalRtv, nClear, 0, nullptr);
    cmdList->ClearDepthStencilView(mDepthDsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
}

void GBuffer::BeginGeometryPass(ID3D12GraphicsCommandList* cmdList, const float albedoClear[4])
{
    TransitionToGeometryWrite(cmdList);
    Clear(cmdList, albedoClear);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[ColorTargetCount] =
    {
        mAlbedoRtv,
        mNormalRtv
    };
    cmdList->OMSetRenderTargets(ColorTargetCount, rtvs, FALSE, &mDepthDsv);
}

void GBuffer::EndGeometryPass(ID3D12GraphicsCommandList* cmdList)
{
    TransitionToShaderRead(cmdList);
}
