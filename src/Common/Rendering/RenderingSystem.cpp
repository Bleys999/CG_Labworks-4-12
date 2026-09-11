#include "RenderingSystem.h"
#include "../Core/Config.h"
#include <cmath>
#include <algorithm>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace
{
    UINT CompileFlags()
    {
        UINT f = 0;
#if defined(DEBUG) || defined(_DEBUG)
        f |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
        return f;
    }
}

bool RenderingSystem::Initialize(ID3D12Device* device)
{
    mDevice = device;
    mLightingCB = std::make_unique<UploadBuffer<DeferredLightingConstants>>(device, 1, true);
    mShadowPassCB = std::make_unique<UploadBuffer<ShadowPassConstants>>(device, Config::ShadowCascadeCount, true);
    InitDefaultLights();
    CreateShadowMaps(device);
    BuildGeometryPass(device);
    BuildTessellationPass(device);
    BuildShadowPass(device);
    BuildLightingPass(device);
    UpdateLightingSrvs(device);
    return true;
}

void RenderingSystem::Resize(ID3D12Device* device, UINT width, UINT height)
{
    mGBuffer.Resize(device, width, height);
    UpdateLightingSrvs(device);
}

void RenderingSystem::InitDefaultLights()
{
    XMVECTOR sun = XMVector3Normalize(XMVectorSet(0.42f, -0.86f, 0.22f, 0.0f));
    XMStoreFloat4(&mLightingCpu.DirectionalDir, sun);
    mLightingCpu.DirectionalColor = { 1.0f, 0.96f, 0.88f, 2.35f };
    mLightingCpu.AmbientColor = { 0.13f, 0.13f, 0.145f, 1.0f };

    mLightingCpu.PointCount = 0;
    mLightingCpu.SpotCount = 0;
}

void RenderingSystem::UpdateLightingConstants(const Camera& camera)
{
    XMVECTOR eye = camera.GetPosition();
    XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(&mLightingCpu.CameraWorld), eye);
    XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(&mLightingCpu.CameraLook), camera.GetLook());
    mLightingCpu.CameraWorld.w = camera.GetAspect();
    mLightingCpu.CameraLook.w = tanf(0.5f * camera.GetFovY());
    UpdateShadowCascades(camera);

    float nBias = 0.8f;
    if (mHasShadowCasterBounds)
    {
        const XMFLOAT3 e = mShadowCasterBounds.Extents;
        const float r = (std::max)(e.x, (std::max)(e.y, e.z));
        nBias = (std::max)(0.8f, r * 0.0008f);
    }
    mLightingCpu.ShadowParams = XMFLOAT4(1.0f / (float)Config::ShadowMapSize, 0.0015f, (float)Config::ShadowCascadeCount, nBias);
    mLightingCB->CopyData(0, mLightingCpu);
}

void RenderingSystem::SetShadowDebugMode(UINT mode)
{
    mLightingCpu.DebugMode = mode;
}

void RenderingSystem::SetShadowCasterBounds(const BoundingBox& bounds)
{
    mShadowCasterBounds = bounds;
    mHasShadowCasterBounds = true;
}

void RenderingSystem::ApplyGeometryPass(ID3D12GraphicsCommandList* cmdList) const
{
    cmdList->SetGraphicsRootSignature(mGeometryRootSignature.Get());
    cmdList->SetPipelineState(mGeometryPSO.Get());
}

void RenderingSystem::ApplyTessellationPass(ID3D12GraphicsCommandList* cmdList) const
{
    cmdList->SetGraphicsRootSignature(mTessellationRootSignature.Get());
    cmdList->SetPipelineState(mTessellationPSO.Get());
}

void RenderingSystem::ApplyShadowPass(ID3D12GraphicsCommandList* cmdList) const
{
    cmdList->SetGraphicsRootSignature(mShadowRootSignature.Get());
    cmdList->SetPipelineState(mShadowPSO.Get());
}

void RenderingSystem::BeginShadowPass(ID3D12GraphicsCommandList* cmdList)
{
    if (mShadowShaderRead)
    {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            mShadowMaps.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_DEPTH_WRITE);
        cmdList->ResourceBarrier(1, &barrier);
        mShadowShaderRead = false;
    }
}

void RenderingSystem::EndShadowPass(ID3D12GraphicsCommandList* cmdList)
{
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        mShadowMaps.Get(),
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &barrier);
    mShadowShaderRead = true;
}

void RenderingSystem::SetShadowCascadeTarget(ID3D12GraphicsCommandList* cmdList, UINT cascadeIndex) const
{
    cmdList->RSSetViewports(1, &mShadowViewport);
    cmdList->RSSetScissorRects(1, &mShadowScissor);
    cmdList->OMSetRenderTargets(0, nullptr, FALSE, &mShadowDsv[cascadeIndex]);
    cmdList->ClearDepthStencilView(mShadowDsv[cascadeIndex], D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    const UINT cbSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ShadowPassConstants));
    cmdList->SetGraphicsRootConstantBufferView(
        1, mShadowPassCB->Resource()->GetGPUVirtualAddress() + cascadeIndex * cbSize);
}

XMMATRIX RenderingSystem::GetCascadeView(UINT cascadeIndex) const
{
    return XMLoadFloat4x4(&mCascadeView[cascadeIndex]);
}

XMMATRIX RenderingSystem::GetCascadeProj(UINT cascadeIndex) const
{
    return XMLoadFloat4x4(&mCascadeProj[cascadeIndex]);
}

void RenderingSystem::DrawDeferredLighting(
    ID3D12GraphicsCommandList* cmdList,
    D3D12_CPU_DESCRIPTOR_HANDLE backBufferRtv,
    const D3D12_VIEWPORT& viewport,
    const D3D12_RECT& scissor) const
{
    cmdList->SetGraphicsRootSignature(mLightingRootSignature.Get());
    cmdList->SetPipelineState(mLightingPSO.Get());

    ID3D12DescriptorHeap* heaps[] = { mLightingSrvHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);

    cmdList->SetGraphicsRootConstantBufferView(0, mLightingCB->Resource()->GetGPUVirtualAddress());
    cmdList->SetGraphicsRootDescriptorTable(1, mLightingSrvHeap->GetGPUDescriptorHandleForHeapStart());

    cmdList->RSSetViewports(1, &viewport);
    cmdList->RSSetScissorRects(1, &scissor);

    float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    cmdList->OMSetRenderTargets(1, &backBufferRtv, false, nullptr);
    cmdList->ClearRenderTargetView(backBufferRtv, black, 0, nullptr);

    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->DrawInstanced(3, 1, 0, 0);
}

void RenderingSystem::BuildGeometryPass(ID3D12Device* device)
{
    ComPtr<ID3DBlob> errors;

    HRESULT hr = D3DCompileFromFile(L"Shaders\\gbuffer.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "VS", "vs_5_0", CompileFlags(), 0, &mGeometryVs, &errors);
    if (FAILED(hr))
    {
        if (errors)
            OutputDebugStringA((char*)errors->GetBufferPointer());
        ThrowIfFailed(hr);
    }

    hr = D3DCompileFromFile(L"Shaders\\gbuffer.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "PS", "ps_5_0", CompileFlags(), 0, &mGeometryPs, &errors);
    if (FAILED(hr))
    {
        if (errors)
            OutputDebugStringA((char*)errors->GetBufferPointer());
        ThrowIfFailed(hr);
    }

    CD3DX12_ROOT_PARAMETER slotRootParameter[2];
    slotRootParameter[0].InitAsConstantBufferView(0);
    slotRootParameter[1].InitAsDescriptorTable(1, &CD3DX12_DESCRIPTOR_RANGE(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0));

    CD3DX12_STATIC_SAMPLER_DESC samplerDesc(
        0,
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        0.0f,
        1,
        D3D12_COMPARISON_FUNC_ALWAYS,
        D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE,
        0.0f,
        D3D12_FLOAT32_MAX,
        D3D12_SHADER_VISIBILITY_PIXEL,
        0);

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, slotRootParameter, 1, &samplerDesc,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serializedRootSig;
    ComPtr<ID3DBlob> errorBlob;
    ThrowIfFailed(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf()));
    ThrowIfFailed(device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&mGeometryRootSignature)));

    mGeometryInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { mGeometryInputLayout.data(), (UINT)mGeometryInputLayout.size() };
    psoDesc.pRootSignature = mGeometryRootSignature.Get();
    psoDesc.VS = { reinterpret_cast<BYTE*>(mGeometryVs->GetBufferPointer()), mGeometryVs->GetBufferSize() };
    psoDesc.PS = { reinterpret_cast<BYTE*>(mGeometryPs->GetBufferPointer()), mGeometryPs->GetBufferSize() };
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = GBuffer::ColorTargetCount;
    psoDesc.RTVFormats[0] = GBuffer::AlbedoFormat;
    psoDesc.RTVFormats[1] = GBuffer::NormalFormat;
    psoDesc.RTVFormats[2] = GBuffer::WorldPosFormat;
    psoDesc.DSVFormat = GBuffer::DepthFormat;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;

    ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mGeometryPSO)));
}

void RenderingSystem::BuildTessellationPass(ID3D12Device* device)
{
    ComPtr<ID3DBlob> errors;

    auto Compile = [&](const char* entry, const char* target, ComPtr<ID3DBlob>& out)
    {
        errors.Reset();
        HRESULT hr = D3DCompileFromFile(L"Shaders\\tessellation.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
            entry, target, CompileFlags(), 0, &out, &errors);
        if (FAILED(hr))
        {
            if (errors)
                OutputDebugStringA((char*)errors->GetBufferPointer());
            ThrowIfFailed(hr);
        }
    };

    Compile("VS", "vs_5_0", mTessVs);
    Compile("HS", "hs_5_0", mTessHs);
    Compile("DS", "ds_5_0", mTessDs);
    Compile("PS", "ps_5_0", mTessPs);

    CD3DX12_DESCRIPTOR_RANGE srvRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0);

    CD3DX12_ROOT_PARAMETER slotRootParameter[2];
    slotRootParameter[0].InitAsConstantBufferView(0);
    slotRootParameter[1].InitAsDescriptorTable(1, &srvRange);

    CD3DX12_STATIC_SAMPLER_DESC samplerDesc(
        0,
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        0.0f,
        1,
        D3D12_COMPARISON_FUNC_ALWAYS,
        D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE,
        0.0f,
        D3D12_FLOAT32_MAX,
        D3D12_SHADER_VISIBILITY_ALL,
        0);

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, slotRootParameter, 1, &samplerDesc,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serializedRootSig;
    ComPtr<ID3DBlob> errorBlob;
    ThrowIfFailed(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf()));
    ThrowIfFailed(device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&mTessellationRootSignature)));

    mTessellationInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { mTessellationInputLayout.data(), (UINT)mTessellationInputLayout.size() };
    psoDesc.pRootSignature = mTessellationRootSignature.Get();
    psoDesc.VS = { reinterpret_cast<BYTE*>(mTessVs->GetBufferPointer()), mTessVs->GetBufferSize() };
    psoDesc.HS = { reinterpret_cast<BYTE*>(mTessHs->GetBufferPointer()), mTessHs->GetBufferSize() };
    psoDesc.DS = { reinterpret_cast<BYTE*>(mTessDs->GetBufferPointer()), mTessDs->GetBufferSize() };
    psoDesc.PS = { reinterpret_cast<BYTE*>(mTessPs->GetBufferPointer()), mTessPs->GetBufferSize() };
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    psoDesc.NumRenderTargets = GBuffer::ColorTargetCount;
    psoDesc.RTVFormats[0] = GBuffer::AlbedoFormat;
    psoDesc.RTVFormats[1] = GBuffer::NormalFormat;
    psoDesc.RTVFormats[2] = GBuffer::WorldPosFormat;
    psoDesc.DSVFormat = GBuffer::DepthFormat;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;

    ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mTessellationPSO)));
}

void RenderingSystem::CreateShadowMaps(ID3D12Device* device)
{
    const UINT size = Config::ShadowMapSize;
    const UINT cascades = (UINT)Config::ShadowCascadeCount;

    mShadowViewport = { 0.0f, 0.0f, (float)size, (float)size, 0.0f, 1.0f };
    mShadowScissor = { 0, 0, (LONG)size, (LONG)size };

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = size;
    desc.Height = size;
    desc.DepthOrArraySize = (UINT16)cascades;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R32_TYPELESS;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clear = {};
    clear.Format = DXGI_FORMAT_D32_FLOAT;
    clear.DepthStencil.Depth = 1.0f;
    clear.DepthStencil.Stencil = 0;

    ThrowIfFailed(device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clear,
        IID_PPV_ARGS(&mShadowMaps)));

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = cascades;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&mShadowDsvHeap)));

    UINT dsvInc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    CD3DX12_CPU_DESCRIPTOR_HANDLE dsv(mShadowDsvHeap->GetCPUDescriptorHandleForHeapStart());

    for (UINT i = 0; i < cascades; ++i)
    {
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        dsvDesc.Texture2DArray.MipSlice = 0;
        dsvDesc.Texture2DArray.FirstArraySlice = i;
        dsvDesc.Texture2DArray.ArraySize = 1;
        mShadowDsv[i] = dsv;
        device->CreateDepthStencilView(mShadowMaps.Get(), &dsvDesc, dsv);
        dsv.Offset(1, dsvInc);
    }

    mShadowShaderRead = false;
}

void RenderingSystem::UpdateLightingSrvs(ID3D12Device* device)
{
    if (!mGBuffer.AlbedoResource() || !mShadowMaps)
        return;

    if (!mLightingSrvHeap)
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.NumDescriptors = 4;
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mLightingSrvHeap)));
    }

    UINT inc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    CD3DX12_CPU_DESCRIPTOR_HANDLE cpu(mLightingSrvHeap->GetCPUDescriptorHandleForHeapStart());

    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;

    srv.Format = GBuffer::AlbedoFormat;
    device->CreateShaderResourceView(mGBuffer.AlbedoResource(), &srv, cpu);
    cpu.Offset(1, inc);

    srv.Format = GBuffer::NormalFormat;
    device->CreateShaderResourceView(mGBuffer.NormalResource(), &srv, cpu);
    cpu.Offset(1, inc);

    srv.Format = GBuffer::WorldPosFormat;
    device->CreateShaderResourceView(mGBuffer.WorldPosResource(), &srv, cpu);
    cpu.Offset(1, inc);

    D3D12_SHADER_RESOURCE_VIEW_DESC shadowSrv = {};
    shadowSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    shadowSrv.Format = DXGI_FORMAT_R32_FLOAT;
    shadowSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    shadowSrv.Texture2DArray.MostDetailedMip = 0;
    shadowSrv.Texture2DArray.MipLevels = 1;
    shadowSrv.Texture2DArray.FirstArraySlice = 0;
    shadowSrv.Texture2DArray.ArraySize = (UINT)Config::ShadowCascadeCount;
    shadowSrv.Texture2DArray.ResourceMinLODClamp = 0.0f;
    device->CreateShaderResourceView(mShadowMaps.Get(), &shadowSrv, cpu);
}

void RenderingSystem::UpdateShadowCascades(const Camera& camera)
{
    const int nCascades = Config::ShadowCascadeCount;
    const float nearZ = camera.GetNearZ();

    float casterRadius = Config::ShadowCasterDist;
    if (mHasShadowCasterBounds)
    {
        const XMFLOAT3 e = mShadowCasterBounds.Extents;
        casterRadius = (std::max)(e.x, (std::max)(e.y, e.z));
    }

    const float farZ = (std::max)(Config::ShadowDistance, casterRadius * 2.2f);

    float splits[5];
    splits[0] = nearZ;
    for (int i = 0; i < nCascades; ++i)
        splits[i + 1] = (std::max)(nearZ + 0.01f, farZ * Config::ShadowCascadeEnds[i]);
    mLightingCpu.CascadeSplits = XMFLOAT4(splits[1], splits[2], splits[3], splits[4]);

    XMVECTOR lightDir = XMVector3Normalize(XMLoadFloat3(reinterpret_cast<XMFLOAT3*>(&mLightingCpu.DirectionalDir)));
    XMVECTOR worldUp = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    if (fabsf(XMVectorGetZ(XMVector3Dot(lightDir, worldUp))) > 0.9f)
        worldUp = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);

    XMVECTOR camPos = camera.GetPosition();
    XMVECTOR camRight = camera.GetRight();
    XMVECTOR camUp = camera.GetUp();
    XMVECTOR camLook = camera.GetLook();
    float fovY = camera.GetFovY();
    float aspect = camera.GetAspect();
    float tanHalf = tanf(0.5f * fovY);

    XMVECTOR focus = camPos + camLook * (farZ * 0.3f);
    float lightPull = casterRadius * 2.5f + 200.0f;
    if (mHasShadowCasterBounds)
        focus = XMLoadFloat3(&mShadowCasterBounds.Center);
    XMMATRIX lightView = XMMatrixLookAtLH(focus - lightDir * lightPull, focus, worldUp);

    const float mapSize = (float)Config::ShadowMapSize;
    const float casterDist = casterRadius * 2.0f + 100.0f;

    for (int i = 0; i < nCascades; ++i)
    {
        float zn = splits[i];
        float zf = splits[i + 1];
        float ny = tanHalf * zn;
        float nx = ny * aspect;
        float fy = tanHalf * zf;
        float fx = fy * aspect;

        XMVECTOR corners[8];
        corners[0] = camPos + camLook * zn + camUp * ny - camRight * nx;
        corners[1] = camPos + camLook * zn + camUp * ny + camRight * nx;
        corners[2] = camPos + camLook * zn - camUp * ny - camRight * nx;
        corners[3] = camPos + camLook * zn - camUp * ny + camRight * nx;
        corners[4] = camPos + camLook * zf + camUp * fy - camRight * fx;
        corners[5] = camPos + camLook * zf + camUp * fy + camRight * fx;
        corners[6] = camPos + camLook * zf - camUp * fy - camRight * fx;
        corners[7] = camPos + camLook * zf - camUp * fy + camRight * fx;

        float minX = 1.0e8f, maxX = -1.0e8f;
        float minY = 1.0e8f, maxY = -1.0e8f;
        float minZ = 1.0e8f, maxZ = -1.0e8f;

        for (int c = 0; c < 8; ++c)
        {
            XMVECTOR pts[2] = { corners[c], corners[c] - lightDir * casterDist };
            for (int p = 0; p < 2; ++p)
            {
                XMFLOAT3 ls;
                XMStoreFloat3(&ls, XMVector3TransformCoord(pts[p], lightView));
                minX = (std::min)(minX, ls.x);
                maxX = (std::max)(maxX, ls.x);
                minY = (std::min)(minY, ls.y);
                maxY = (std::max)(maxY, ls.y);
                minZ = (std::min)(minZ, ls.z);
                maxZ = (std::max)(maxZ, ls.z);
            }
        }

        float pad = 8.0f + 8.0f * (maxX - minX) / mapSize;
        minX -= pad;
        maxX += pad;
        minY -= pad;
        maxY += pad;

        if (mHasShadowCasterBounds)
        {
            const XMFLOAT3 c = mShadowCasterBounds.Center;
            const XMFLOAT3 e = mShadowCasterBounds.Extents;
            for (int ix = 0; ix < 2; ++ix)
            {
                for (int iy = 0; iy < 2; ++iy)
                {
                    for (int iz = 0; iz < 2; ++iz)
                    {
                        XMVECTOR p = XMVectorSet(
                            c.x + (ix ? e.x : -e.x),
                            c.y + (iy ? e.y : -e.y),
                            c.z + (iz ? e.z : -e.z),
                            1.0f);
                        XMFLOAT3 ls;
                        XMStoreFloat3(&ls, XMVector3TransformCoord(p, lightView));
                        minZ = (std::min)(minZ, ls.z);
                        maxZ = (std::max)(maxZ, ls.z);
                    }
                }
            }
        }

        minZ = (std::max)(minZ - casterRadius * 0.02f, 0.05f);
        maxZ += casterRadius * 0.05f + 16.0f;

        float texelX = (maxX - minX) / mapSize;
        float texelY = (maxY - minY) / mapSize;
        minX = floorf(minX / texelX) * texelX;
        maxX = ceilf(maxX / texelX) * texelX;
        minY = floorf(minY / texelY) * texelY;
        maxY = ceilf(maxY / texelY) * texelY;

        XMMATRIX proj = XMMatrixOrthographicOffCenterLH(minX, maxX, minY, maxY, minZ, maxZ);
        XMMATRIX vp = lightView * proj;

        XMStoreFloat4x4(&mCascadeView[i], lightView);
        XMStoreFloat4x4(&mCascadeProj[i], proj);
        XMStoreFloat4x4(&mLightingCpu.CascadeViewProj[i], XMMatrixTranspose(vp));

        ShadowPassConstants pass{};
        XMStoreFloat4x4(&pass.LightViewProj, XMMatrixTranspose(vp));
        mShadowPassCB->CopyData(i, pass);
    }
}

void RenderingSystem::BuildShadowPass(ID3D12Device* device)
{
    ComPtr<ID3DBlob> errors;
    HRESULT hr = D3DCompileFromFile(L"Shaders\\shadow.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "VS", "vs_5_0", CompileFlags(), 0, &mShadowVs, &errors);
    if (FAILED(hr))
    {
        if (errors)
            OutputDebugStringA((char*)errors->GetBufferPointer());
        ThrowIfFailed(hr);
    }

    CD3DX12_ROOT_PARAMETER slotRootParameter[2];
    slotRootParameter[0].InitAsConstantBufferView(0);
    slotRootParameter[1].InitAsConstantBufferView(1);

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, slotRootParameter, 0, nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serializedRootSig;
    ComPtr<ID3DBlob> errorBlob;
    ThrowIfFailed(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf()));
    ThrowIfFailed(device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&mShadowRootSignature)));

    D3D12_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { layout, 1 };
    psoDesc.pRootSignature = mShadowRootSignature.Get();
    psoDesc.VS = { reinterpret_cast<BYTE*>(mShadowVs->GetBufferPointer()), mShadowVs->GetBufferSize() };
    psoDesc.PS = { nullptr, 0 };
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.DepthBias = 0;
    psoDesc.RasterizerState.SlopeScaledDepthBias = 1.6f;
    psoDesc.RasterizerState.DepthBiasClamp = 0.0f;
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 0;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;

    ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mShadowPSO)));
}

void RenderingSystem::BuildLightingPass(ID3D12Device* device)
{
    ComPtr<ID3DBlob> errors;

    HRESULT hr = D3DCompileFromFile(L"Shaders\\deferred_light.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "VS", "vs_5_0", CompileFlags(), 0, &mLightingVs, &errors);
    if (FAILED(hr))
    {
        if (errors)
            OutputDebugStringA((char*)errors->GetBufferPointer());
        ThrowIfFailed(hr);
    }

    hr = D3DCompileFromFile(L"Shaders\\deferred_light.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "PS", "ps_5_0", CompileFlags(), 0, &mLightingPs, &errors);
    if (FAILED(hr))
    {
        if (errors)
            OutputDebugStringA((char*)errors->GetBufferPointer());
        ThrowIfFailed(hr);
    }

    CD3DX12_DESCRIPTOR_RANGE srvRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0);

    CD3DX12_ROOT_PARAMETER slotRootParameter[2];
    slotRootParameter[0].InitAsConstantBufferView(0);
    slotRootParameter[1].InitAsDescriptorTable(1, &srvRange);

    CD3DX12_STATIC_SAMPLER_DESC shadowSampler(
        0,
        D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,
        0.0f,
        1,
        D3D12_COMPARISON_FUNC_LESS_EQUAL,
        D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE,
        0.0f,
        D3D12_FLOAT32_MAX,
        D3D12_SHADER_VISIBILITY_PIXEL,
        0);

    CD3DX12_STATIC_SAMPLER_DESC mapSampler(
        1,
        D3D12_FILTER_MIN_MAG_MIP_POINT,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        0.0f,
        1,
        D3D12_COMPARISON_FUNC_NEVER,
        D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE,
        0.0f,
        D3D12_FLOAT32_MAX,
        D3D12_SHADER_VISIBILITY_PIXEL,
        0);

    CD3DX12_STATIC_SAMPLER_DESC samplers[2] = { shadowSampler, mapSampler };

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, slotRootParameter, 2, samplers,
        D3D12_ROOT_SIGNATURE_FLAG_NONE);

    ComPtr<ID3DBlob> serializedRootSig;
    ComPtr<ID3DBlob> errorBlob;
    ThrowIfFailed(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf()));
    ThrowIfFailed(device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&mLightingRootSignature)));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { nullptr, 0 };
    psoDesc.pRootSignature = mLightingRootSignature.Get();
    psoDesc.VS = { reinterpret_cast<BYTE*>(mLightingVs->GetBufferPointer()), mLightingVs->GetBufferSize() };
    psoDesc.PS = { reinterpret_cast<BYTE*>(mLightingPs->GetBufferPointer()), mLightingPs->GetBufferSize() };
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthEnable = false;
    psoDesc.DepthStencilState.StencilEnable = false;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;

    ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mLightingPSO)));
}
