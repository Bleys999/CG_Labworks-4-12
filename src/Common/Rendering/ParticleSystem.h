#pragma once

#include "../D3D12/d3dUtil.h"
#include "../Scene/Camera.h"
#include "../Core/GameTimer.h"

struct ParticleGpu
{
    DirectX::XMFLOAT3 Position;
    float Age = 0.0f;
    DirectX::XMFLOAT3 Velocity;
    float Lifetime = 1.0f;
    DirectX::XMFLOAT4 Color = { 1.0f, 1.0f, 1.0f, 1.0f };
    float Size = 0.2f;
    float Pad[3] = {};
};
static_assert(sizeof(ParticleGpu) == 64, "ParticleGpu must be 64 bytes");

struct ParticleSimConstants
{
    float DeltaTime = 0.0f;
    float TotalTime = 0.0f;
    UINT EmitCount = 0;
    UINT MaxParticles = 0;
    DirectX::XMFLOAT3 EmitterPos = { 0.0f, 1.2f, 0.0f };
    float EmitLifetime = 3.2f;
    DirectX::XMFLOAT3 Gravity = { 0.0f, -12.0f, 0.0f };
    float SizeMin = 0.12f;
    DirectX::XMFLOAT4 Color0 = { 1.0f, 0.42f, 0.08f, 1.0f };
    DirectX::XMFLOAT4 Color1 = { 1.0f, 0.85f, 0.35f, 1.0f };
    DirectX::XMFLOAT3 EmitVelocity = { 0.0f, 20.0f, 0.0f };
    float EmitSpread = 10.0f;
    DirectX::XMFLOAT3 CameraPos = { 0.0f, 0.0f, 0.0f };
    float SizeMax = 0.26f;
    float Pad[36] = {};
};
static_assert(sizeof(ParticleSimConstants) == 256, "ParticleSimConstants must be 256 bytes");

struct ParticleDrawConstants
{
    DirectX::XMFLOAT4X4 ViewProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT3 CamRight = { 1.0f, 0.0f, 0.0f };
    float Pad0 = 0.0f;
    DirectX::XMFLOAT3 CamUp = { 0.0f, 1.0f, 0.0f };
    float Pad1 = 0.0f;
    DirectX::XMFLOAT3 CamForward = { 0.0f, 0.0f, 1.0f };
    float ReceiveShadow = 1.0f;
    float Roughness = 0.45f;
    float Metallic = 0.0f;
    UINT TriCount = 0;
    float Pad3 = 0.0f;
    float Pad[32] = {};
};
static_assert(sizeof(ParticleDrawConstants) == 256, "ParticleDrawConstants must be 256 bytes");

class ParticleSystem
{
public:
    bool Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);
    void Update(ID3D12GraphicsCommandList* cmdList, const Camera& camera, const GameTimer& gt);
    void Draw(ID3D12GraphicsCommandList* cmdList, const Camera& camera);

private:
    void BuildCompute(ID3D12Device* device);
    void BuildSort(ID3D12Device* device);
    void BuildDraw(ID3D12Device* device);
    bool LoadMonkeyMesh(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);
    void CreateBuffers(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);
    void CreateViews(ID3D12Device* device);
    void ResetCounter(ID3D12GraphicsCommandList* cmdList, UINT bufferIndex);
    void CopyCounter(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* dest, UINT destOffset, UINT srcBufferIndex);
    void Transition(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* resource,
        D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);

private:
    static constexpr UINT kCounterSize = D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT;
    static constexpr UINT kSortGroupSize = 256;

    ID3D12Device* mDevice = nullptr;
    UINT mSrvUavDescriptorSize = 0;
    UINT mPing = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> mBuffers[2];
    Microsoft::WRL::ComPtr<ID3D12Resource> mCounters[2];
    Microsoft::WRL::ComPtr<ID3D12Resource> mDispatchArgs;
    Microsoft::WRL::ComPtr<ID3D12Resource> mDrawArgs;
    Microsoft::WRL::ComPtr<ID3D12Resource> mCountBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> mMonkeyBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> mMonkeyUpload;
    UINT mMonkeyTriCount = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> mZeroUpload;
    Microsoft::WRL::ComPtr<ID3D12Resource> mDispatchInitUpload;
    Microsoft::WRL::ComPtr<ID3D12Resource> mDrawInitUpload;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mHeap;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> mComputeRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mUpdatePSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mEmitPSO;
    Microsoft::WRL::ComPtr<ID3DBlob> mUpdateCs;
    Microsoft::WRL::ComPtr<ID3DBlob> mEmitCs;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> mSortRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mSortPSO;
    Microsoft::WRL::ComPtr<ID3DBlob> mSortCs;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> mDrawRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mDrawPSO;
    Microsoft::WRL::ComPtr<ID3DBlob> mDrawVs;
    Microsoft::WRL::ComPtr<ID3DBlob> mDrawGs;
    Microsoft::WRL::ComPtr<ID3DBlob> mDrawPs;

    Microsoft::WRL::ComPtr<ID3D12CommandSignature> mDispatchSignature;
    Microsoft::WRL::ComPtr<ID3D12CommandSignature> mDrawSignature;

    std::unique_ptr<UploadBuffer<ParticleSimConstants>> mSimCB;
    std::unique_ptr<UploadBuffer<ParticleDrawConstants>> mDrawCB;

    D3D12_RESOURCE_STATES mBufferState[2] = {
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS
    };
    D3D12_RESOURCE_STATES mCounterState[2] = {
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS
    };
    D3D12_RESOURCE_STATES mDispatchArgsState = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    D3D12_RESOURCE_STATES mDrawArgsState = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    D3D12_RESOURCE_STATES mCountBufferState = D3D12_RESOURCE_STATE_COPY_DEST;
};
