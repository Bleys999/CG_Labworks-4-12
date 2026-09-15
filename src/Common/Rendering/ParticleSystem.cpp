#include "ParticleSystem.h"
#include "GBuffer.h"
#include "../Core/Config.h"
#include <cstring>

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

    ComPtr<ID3DBlob> CompileShaderFile(const wchar_t* name, const char* entry, const char* target)
    {
        std::wstring a = std::wstring(L"..\\Shaders\\") + name;
        std::wstring b = std::wstring(L"Shaders\\") + name;
        ComPtr<ID3DBlob> blob;
        ComPtr<ID3DBlob> errors;
        HRESULT hr = D3DCompileFromFile(a.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
            entry, target, CompileFlags(), 0, &blob, &errors);
        if (FAILED(hr))
        {
            if (errors)
                OutputDebugStringA((char*)errors->GetBufferPointer());
            errors.Reset();
            hr = D3DCompileFromFile(b.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                entry, target, CompileFlags(), 0, &blob, &errors);
            if (FAILED(hr) && errors)
                OutputDebugStringA((char*)errors->GetBufferPointer());
            ThrowIfFailed(hr);
        }
        return blob;
    }

    template<typename T>
    void WriteUpload(ID3D12Resource* resource, const T& data)
    {
        void* mapped = nullptr;
        ThrowIfFailed(resource->Map(0, nullptr, &mapped));
        memcpy(mapped, &data, sizeof(T));
        resource->Unmap(0, nullptr);
    }
}

bool ParticleSystem::Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList)
{
    mDevice = device;
    mSrvUavDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    mSimCB = std::make_unique<UploadBuffer<ParticleSimConstants>>(device, 1, true);
    mDrawCB = std::make_unique<UploadBuffer<ParticleDrawConstants>>(device, 1, true);
    CreateBuffers(device, cmdList);
    CreateViews(device);
    BuildCompute(device);
    BuildSort(device);
    BuildDraw(device);
    return true;
}

void ParticleSystem::Transition(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    if (before == after)
        return;
    cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(resource, before, after));
}

void ParticleSystem::ResetCounter(ID3D12GraphicsCommandList* cmdList, UINT bufferIndex)
{
    Transition(cmdList, mCounters[bufferIndex].Get(), mCounterState[bufferIndex], D3D12_RESOURCE_STATE_COPY_DEST);
    mCounterState[bufferIndex] = D3D12_RESOURCE_STATE_COPY_DEST;
    cmdList->CopyBufferRegion(mCounters[bufferIndex].Get(), 0, mZeroUpload.Get(), 0, sizeof(UINT));
    Transition(cmdList, mCounters[bufferIndex].Get(), mCounterState[bufferIndex], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    mCounterState[bufferIndex] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
}

void ParticleSystem::CopyCounter(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* dest, UINT destOffset, UINT srcBufferIndex)
{
    Transition(cmdList, mCounters[srcBufferIndex].Get(), mCounterState[srcBufferIndex], D3D12_RESOURCE_STATE_COPY_SOURCE);
    mCounterState[srcBufferIndex] = D3D12_RESOURCE_STATE_COPY_SOURCE;
    cmdList->CopyBufferRegion(dest, destOffset, mCounters[srcBufferIndex].Get(), 0, sizeof(UINT));
    Transition(cmdList, mCounters[srcBufferIndex].Get(), mCounterState[srcBufferIndex], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    mCounterState[srcBufferIndex] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
}

void ParticleSystem::CreateBuffers(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList)
{
    const UINT64 particleBytes = (UINT64)sizeof(ParticleGpu) * Config::ParticleMaxCount;
    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);

    CD3DX12_RESOURCE_DESC particleDesc = CD3DX12_RESOURCE_DESC::Buffer(
        particleBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    for (int i = 0; i < 2; ++i)
    {
        ThrowIfFailed(device->CreateCommittedResource(
            &defaultHeap, D3D12_HEAP_FLAG_NONE, &particleDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&mBuffers[i])));
        mBufferState[i] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        CD3DX12_RESOURCE_DESC counterDesc = CD3DX12_RESOURCE_DESC::Buffer(
            kCounterSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        ThrowIfFailed(device->CreateCommittedResource(
            &defaultHeap, D3D12_HEAP_FLAG_NONE, &counterDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&mCounters[i])));
        mCounterState[i] = D3D12_RESOURCE_STATE_COPY_DEST;
    }

    CD3DX12_RESOURCE_DESC dispatchDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(D3D12_DISPATCH_ARGUMENTS));
    ThrowIfFailed(device->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE, &dispatchDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&mDispatchArgs)));
    mDispatchArgsState = D3D12_RESOURCE_STATE_COPY_DEST;

    CD3DX12_RESOURCE_DESC drawDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(D3D12_DRAW_ARGUMENTS));
    ThrowIfFailed(device->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE, &drawDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&mDrawArgs)));
    mDrawArgsState = D3D12_RESOURCE_STATE_COPY_DEST;

    CD3DX12_RESOURCE_DESC countDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(UINT));
    ThrowIfFailed(device->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE, &countDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&mCountBuffer)));
    mCountBufferState = D3D12_RESOURCE_STATE_COPY_DEST;

    CD3DX12_RESOURCE_DESC uploadSmall = CD3DX12_RESOURCE_DESC::Buffer(16);
    ThrowIfFailed(device->CreateCommittedResource(
        &uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadSmall,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mZeroUpload)));
    ThrowIfFailed(device->CreateCommittedResource(
        &uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadSmall,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mDispatchInitUpload)));
    ThrowIfFailed(device->CreateCommittedResource(
        &uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadSmall,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mDrawInitUpload)));

    UINT zero = 0;
    WriteUpload(mZeroUpload.Get(), zero);
    D3D12_DISPATCH_ARGUMENTS dispInit = { 0, 1, 1 };
    WriteUpload(mDispatchInitUpload.Get(), dispInit);
    D3D12_DRAW_ARGUMENTS drawInit = { 0, 1, 0, 0 };
    WriteUpload(mDrawInitUpload.Get(), drawInit);

    cmdList->CopyBufferRegion(mDispatchArgs.Get(), 0, mDispatchInitUpload.Get(), 0, sizeof(D3D12_DISPATCH_ARGUMENTS));
    cmdList->CopyBufferRegion(mDrawArgs.Get(), 0, mDrawInitUpload.Get(), 0, sizeof(D3D12_DRAW_ARGUMENTS));
    cmdList->CopyBufferRegion(mCounters[0].Get(), 0, mZeroUpload.Get(), 0, sizeof(UINT));
    cmdList->CopyBufferRegion(mCounters[1].Get(), 0, mZeroUpload.Get(), 0, sizeof(UINT));

    Transition(cmdList, mDispatchArgs.Get(), mDispatchArgsState, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    mDispatchArgsState = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    Transition(cmdList, mDrawArgs.Get(), mDrawArgsState, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    mDrawArgsState = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    Transition(cmdList, mCounters[0].Get(), mCounterState[0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    mCounterState[0] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    Transition(cmdList, mCounters[1].Get(), mCounterState[1], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    mCounterState[1] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
}

void ParticleSystem::CreateViews(ID3D12Device* device)
{
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = 9;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mHeap)));

    CD3DX12_CPU_DESCRIPTOR_HANDLE cpu(mHeap->GetCPUDescriptorHandleForHeapStart());

    D3D12_UNORDERED_ACCESS_VIEW_DESC appendUav = {};
    appendUav.Format = DXGI_FORMAT_UNKNOWN;
    appendUav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    appendUav.Buffer.FirstElement = 0;
    appendUav.Buffer.NumElements = Config::ParticleMaxCount;
    appendUav.Buffer.StructureByteStride = sizeof(ParticleGpu);
    appendUav.Buffer.CounterOffsetInBytes = 0;
    appendUav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

    device->CreateUnorderedAccessView(mBuffers[0].Get(), mCounters[0].Get(), &appendUav, cpu);
    cpu.Offset(1, mSrvUavDescriptorSize);
    device->CreateUnorderedAccessView(mBuffers[1].Get(), mCounters[1].Get(), &appendUav, cpu);
    cpu.Offset(1, mSrvUavDescriptorSize);
    device->CreateUnorderedAccessView(mBuffers[1].Get(), mCounters[1].Get(), &appendUav, cpu);
    cpu.Offset(1, mSrvUavDescriptorSize);
    device->CreateUnorderedAccessView(mBuffers[0].Get(), mCounters[0].Get(), &appendUav, cpu);
    cpu.Offset(1, mSrvUavDescriptorSize);

    D3D12_UNORDERED_ACCESS_VIEW_DESC rwUav = appendUav;
    rwUav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    device->CreateUnorderedAccessView(mBuffers[0].Get(), nullptr, &rwUav, cpu);
    cpu.Offset(1, mSrvUavDescriptorSize);
    device->CreateUnorderedAccessView(mBuffers[1].Get(), nullptr, &rwUav, cpu);
    cpu.Offset(1, mSrvUavDescriptorSize);

    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Format = DXGI_FORMAT_UNKNOWN;
    srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv.Buffer.FirstElement = 0;
    srv.Buffer.NumElements = Config::ParticleMaxCount;
    srv.Buffer.StructureByteStride = sizeof(ParticleGpu);
    srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    device->CreateShaderResourceView(mBuffers[0].Get(), &srv, cpu);
    cpu.Offset(1, mSrvUavDescriptorSize);
    device->CreateShaderResourceView(mBuffers[1].Get(), &srv, cpu);
    cpu.Offset(1, mSrvUavDescriptorSize);

    D3D12_SHADER_RESOURCE_VIEW_DESC countSrv = {};
    countSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    countSrv.Format = DXGI_FORMAT_R32_UINT;
    countSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    countSrv.Buffer.FirstElement = 0;
    countSrv.Buffer.NumElements = 1;
    countSrv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    device->CreateShaderResourceView(mCountBuffer.Get(), &countSrv, cpu);
}

void ParticleSystem::BuildCompute(ID3D12Device* device)
{
    mUpdateCs = CompileShaderFile(L"particles_cs.hlsl", "UpdateCS", "cs_5_0");
    mEmitCs = CompileShaderFile(L"particles_cs.hlsl", "EmitCS", "cs_5_0");

    CD3DX12_DESCRIPTOR_RANGE uavRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0);
    CD3DX12_ROOT_PARAMETER slot[2];
    slot[0].InitAsConstantBufferView(0);
    slot[1].InitAsDescriptorTable(1, &uavRange);

    CD3DX12_ROOT_SIGNATURE_DESC rootDesc(2, slot, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);
    ComPtr<ID3DBlob> serialized;
    ComPtr<ID3DBlob> error;
    ThrowIfFailed(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serialized.GetAddressOf(), error.GetAddressOf()));
    ThrowIfFailed(device->CreateRootSignature(0, serialized->GetBufferPointer(),
        serialized->GetBufferSize(), IID_PPV_ARGS(&mComputeRootSignature)));

    D3D12_COMPUTE_PIPELINE_STATE_DESC pso = {};
    pso.pRootSignature = mComputeRootSignature.Get();
    pso.CS = { reinterpret_cast<BYTE*>(mUpdateCs->GetBufferPointer()), mUpdateCs->GetBufferSize() };
    ThrowIfFailed(device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&mUpdatePSO)));

    pso.CS = { reinterpret_cast<BYTE*>(mEmitCs->GetBufferPointer()), mEmitCs->GetBufferSize() };
    ThrowIfFailed(device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&mEmitPSO)));

    D3D12_INDIRECT_ARGUMENT_DESC arg = {};
    arg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
    D3D12_COMMAND_SIGNATURE_DESC sigDesc = {};
    sigDesc.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS);
    sigDesc.NumArgumentDescs = 1;
    sigDesc.pArgumentDescs = &arg;
    ThrowIfFailed(device->CreateCommandSignature(&sigDesc, nullptr, IID_PPV_ARGS(&mDispatchSignature)));
}

void ParticleSystem::BuildSort(ID3D12Device* device)
{
    mSortCs = CompileShaderFile(L"particles_sort.hlsl", "BitonicCS", "cs_5_0");

    CD3DX12_DESCRIPTOR_RANGE uavRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
    CD3DX12_DESCRIPTOR_RANGE srvRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    CD3DX12_ROOT_PARAMETER slot[4];
    slot[0].InitAsConstantBufferView(0);
    slot[1].InitAsDescriptorTable(1, &uavRange);
    slot[2].InitAsDescriptorTable(1, &srvRange);
    slot[3].InitAsConstants(2, 1);

    CD3DX12_ROOT_SIGNATURE_DESC rootDesc(4, slot, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);
    ComPtr<ID3DBlob> serialized;
    ComPtr<ID3DBlob> error;
    ThrowIfFailed(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serialized.GetAddressOf(), error.GetAddressOf()));
    ThrowIfFailed(device->CreateRootSignature(0, serialized->GetBufferPointer(),
        serialized->GetBufferSize(), IID_PPV_ARGS(&mSortRootSignature)));

    D3D12_COMPUTE_PIPELINE_STATE_DESC pso = {};
    pso.pRootSignature = mSortRootSignature.Get();
    pso.CS = { reinterpret_cast<BYTE*>(mSortCs->GetBufferPointer()), mSortCs->GetBufferSize() };
    ThrowIfFailed(device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&mSortPSO)));
}

void ParticleSystem::BuildDraw(ID3D12Device* device)
{
    mDrawVs = CompileShaderFile(L"particles.hlsl", "VS", "vs_5_0");
    mDrawGs = CompileShaderFile(L"particles.hlsl", "GS", "gs_5_0");
    mDrawPs = CompileShaderFile(L"particles.hlsl", "PS", "ps_5_0");

    CD3DX12_DESCRIPTOR_RANGE srvRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    CD3DX12_ROOT_PARAMETER slot[2];
    slot[0].InitAsConstantBufferView(0);
    slot[1].InitAsDescriptorTable(1, &srvRange);

    CD3DX12_ROOT_SIGNATURE_DESC rootDesc(2, slot, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);
    ComPtr<ID3DBlob> serialized;
    ComPtr<ID3DBlob> error;
    ThrowIfFailed(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serialized.GetAddressOf(), error.GetAddressOf()));
    ThrowIfFailed(device->CreateRootSignature(0, serialized->GetBufferPointer(),
        serialized->GetBufferSize(), IID_PPV_ARGS(&mDrawRootSignature)));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
    pso.InputLayout = { nullptr, 0 };
    pso.pRootSignature = mDrawRootSignature.Get();
    pso.VS = { reinterpret_cast<BYTE*>(mDrawVs->GetBufferPointer()), mDrawVs->GetBufferSize() };
    pso.GS = { reinterpret_cast<BYTE*>(mDrawGs->GetBufferPointer()), mDrawGs->GetBufferSize() };
    pso.PS = { reinterpret_cast<BYTE*>(mDrawPs->GetBufferPointer()), mDrawPs->GetBufferSize() };
    pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    pso.SampleMask = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    pso.NumRenderTargets = GBuffer::ColorTargetCount;
    pso.RTVFormats[0] = GBuffer::AlbedoFormat;
    pso.RTVFormats[1] = GBuffer::NormalFormat;
    pso.RTVFormats[2] = GBuffer::WorldPosFormat;
    pso.DSVFormat = GBuffer::DepthFormat;
    pso.SampleDesc.Count = 1;
    pso.SampleDesc.Quality = 0;
    ThrowIfFailed(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&mDrawPSO)));

    D3D12_INDIRECT_ARGUMENT_DESC arg = {};
    arg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
    D3D12_COMMAND_SIGNATURE_DESC sigDesc = {};
    sigDesc.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS);
    sigDesc.NumArgumentDescs = 1;
    sigDesc.pArgumentDescs = &arg;
    ThrowIfFailed(device->CreateCommandSignature(&sigDesc, nullptr, IID_PPV_ARGS(&mDrawSignature)));
}

void ParticleSystem::Update(ID3D12GraphicsCommandList* cmdList, const Camera& camera, const GameTimer& gt)
{
    float dt = gt.DeltaTime();
    if (dt > 1.0f / 15.0f)
        dt = 1.0f / 15.0f;

    ParticleSimConstants sim{};
    sim.DeltaTime = dt;
    sim.TotalTime = gt.TotalTime();
    sim.EmitCount = Config::ParticleEmitPerFrame;
    sim.MaxParticles = Config::ParticleMaxCount;
    sim.EmitLifetime = Config::ParticleLifetime;
    sim.SizeMin = Config::ParticleSizeMin;
    sim.SizeMax = Config::ParticleSizeMax;
    sim.EmitVelocity = XMFLOAT3(0.0f, Config::ParticleEmitSpeed, 0.0f);
    sim.EmitSpread = Config::ParticleEmitSpread;
    sim.Gravity = XMFLOAT3(0.0f, Config::ParticleGravity, 0.0f);
    sim.CameraPos = camera.GetPosition3f();
    mSimCB->CopyData(0, sim);

    const UINT consume = mPing;
    const UINT append = 1 - mPing;

    ResetCounter(cmdList, append);

    Transition(cmdList, mDispatchArgs.Get(), mDispatchArgsState, D3D12_RESOURCE_STATE_COPY_DEST);
    mDispatchArgsState = D3D12_RESOURCE_STATE_COPY_DEST;
    CopyCounter(cmdList, mDispatchArgs.Get(), 0, consume);
    Transition(cmdList, mDispatchArgs.Get(), mDispatchArgsState, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    mDispatchArgsState = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;

    ID3D12DescriptorHeap* heaps[] = { mHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);
    cmdList->SetComputeRootSignature(mComputeRootSignature.Get());
    cmdList->SetComputeRootConstantBufferView(0, mSimCB->Resource()->GetGPUVirtualAddress());

    CD3DX12_GPU_DESCRIPTOR_HANDLE computeTable(
        mHeap->GetGPUDescriptorHandleForHeapStart(),
        consume == 0 ? 0 : 2,
        mSrvUavDescriptorSize);
    cmdList->SetComputeRootDescriptorTable(1, computeTable);

    cmdList->SetPipelineState(mUpdatePSO.Get());
    cmdList->ExecuteIndirect(mDispatchSignature.Get(), 1, mDispatchArgs.Get(), 0, nullptr, 0);

    D3D12_RESOURCE_BARRIER uavBarriers[4];
    uavBarriers[0] = CD3DX12_RESOURCE_BARRIER::UAV(mBuffers[0].Get());
    uavBarriers[1] = CD3DX12_RESOURCE_BARRIER::UAV(mBuffers[1].Get());
    uavBarriers[2] = CD3DX12_RESOURCE_BARRIER::UAV(mCounters[0].Get());
    uavBarriers[3] = CD3DX12_RESOURCE_BARRIER::UAV(mCounters[1].Get());
    cmdList->ResourceBarrier(4, uavBarriers);

    cmdList->SetPipelineState(mEmitPSO.Get());
    const UINT emitGroups = (Config::ParticleEmitPerFrame + 63u) / 64u;
    cmdList->Dispatch(emitGroups, 1, 1);
    cmdList->ResourceBarrier(4, uavBarriers);

    if (Config::ParticleGpuSort)
    {
        Transition(cmdList, mCountBuffer.Get(), mCountBufferState, D3D12_RESOURCE_STATE_COPY_DEST);
        mCountBufferState = D3D12_RESOURCE_STATE_COPY_DEST;
        CopyCounter(cmdList, mCountBuffer.Get(), 0, append);
        Transition(cmdList, mCountBuffer.Get(), mCountBufferState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        mCountBufferState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

        cmdList->SetComputeRootSignature(mSortRootSignature.Get());
        cmdList->SetComputeRootConstantBufferView(0, mSimCB->Resource()->GetGPUVirtualAddress());
        CD3DX12_GPU_DESCRIPTOR_HANDLE rwTable(
            mHeap->GetGPUDescriptorHandleForHeapStart(),
            4 + append,
            mSrvUavDescriptorSize);
        cmdList->SetComputeRootDescriptorTable(1, rwTable);
        CD3DX12_GPU_DESCRIPTOR_HANDLE countTable(
            mHeap->GetGPUDescriptorHandleForHeapStart(),
            8,
            mSrvUavDescriptorSize);
        cmdList->SetComputeRootDescriptorTable(2, countTable);
        cmdList->SetPipelineState(mSortPSO.Get());

        const UINT groups = Config::ParticleMaxCount / kSortGroupSize;
        for (UINT k = 2; k <= Config::ParticleMaxCount; k <<= 1)
        {
            for (UINT j = k >> 1; j > 0; j >>= 1)
            {
                UINT constants[2] = { j, k };
                cmdList->SetComputeRoot32BitConstants(3, 2, constants, 0);
                cmdList->Dispatch(groups, 1, 1);
                cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(mBuffers[append].Get()));
            }
        }

        Transition(cmdList, mCountBuffer.Get(), mCountBufferState, D3D12_RESOURCE_STATE_COPY_DEST);
        mCountBufferState = D3D12_RESOURCE_STATE_COPY_DEST;
    }

    Transition(cmdList, mDrawArgs.Get(), mDrawArgsState, D3D12_RESOURCE_STATE_COPY_DEST);
    mDrawArgsState = D3D12_RESOURCE_STATE_COPY_DEST;
    CopyCounter(cmdList, mDrawArgs.Get(), 0, append);
    Transition(cmdList, mDrawArgs.Get(), mDrawArgsState, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    mDrawArgsState = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
}

void ParticleSystem::Draw(ID3D12GraphicsCommandList* cmdList, const Camera& camera)
{
    const UINT append = 1 - mPing;

    ParticleDrawConstants dc{};
    XMMATRIX viewProj = camera.GetView() * camera.GetProj();
    XMStoreFloat4x4(&dc.ViewProj, XMMatrixTranspose(viewProj));
    dc.CamRight = camera.GetRight3f();
    dc.CamUp = camera.GetUp3f();
    dc.CamForward = camera.GetLook3f();
    dc.ReceiveShadow = 1.0f;
    mDrawCB->CopyData(0, dc);

    Transition(cmdList, mBuffers[append].Get(), mBufferState[append],
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    mBufferState[append] = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    ID3D12DescriptorHeap* heaps[] = { mHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);
    cmdList->SetGraphicsRootSignature(mDrawRootSignature.Get());
    cmdList->SetPipelineState(mDrawPSO.Get());
    cmdList->SetGraphicsRootConstantBufferView(0, mDrawCB->Resource()->GetGPUVirtualAddress());
    CD3DX12_GPU_DESCRIPTOR_HANDLE srvTable(
        mHeap->GetGPUDescriptorHandleForHeapStart(),
        6 + append,
        mSrvUavDescriptorSize);
    cmdList->SetGraphicsRootDescriptorTable(1, srvTable);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
    cmdList->ExecuteIndirect(mDrawSignature.Get(), 1, mDrawArgs.Get(), 0, nullptr, 0);

    Transition(cmdList, mBuffers[append].Get(), mBufferState[append], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    mBufferState[append] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    mPing = append;
}
