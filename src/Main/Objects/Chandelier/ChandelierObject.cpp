#include "ChandelierObject.h"

using namespace DirectX;

ChandelierObject::ChandelierObject() : GameObject("Chandelier")
{
    SetCullable(false);
}

ChandelierObject::ChandelierObject(const std::string& name) : GameObject(name)
{
    SetCullable(false);
}

void ChandelierObject::SetSwing(float amplitude, float frequency, float phase)
{
    mAmplitude = amplitude;
    mFrequency = frequency;
    mPhase = phase;
}

XMFLOAT3 ChandelierObject::GetLightPosition() const
{
    XMVECTOR p = XMVector3TransformCoord(XMLoadFloat3(&mLightLocal), GetWorldMatrix());
    XMFLOAT3 out;
    XMStoreFloat3(&out, p);
    return out;
}

XMFLOAT3 ChandelierObject::GetLightDirection() const
{
    XMVECTOR d = XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f), GetWorldMatrix()));
    XMFLOAT3 out;
    XMStoreFloat3(&out, d);
    return out;
}

bool ChandelierObject::Initialize(ID3D12Device*, ID3D12GraphicsCommandList*)
{
    return mGeometry != nullptr;
}

bool ChandelierObject::Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, TextureManager* textureManager)
{
    if (!Initialize(device, cmdList))
        return false;
    if (textureManager)
        mTexture = textureManager->GetWhiteTexture();
    return true;
}

bool ChandelierObject::InitializeMesh(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList,
    TextureManager* textureManager,
    const std::vector<ObjVertex>& vertices,
    const std::vector<uint16_t>& indices,
    const XMFLOAT3& lightLocal,
    const BoundingBox& localBounds)
{
    if (vertices.empty() || indices.empty())
        return false;

    mGeometry = std::make_shared<MeshGeometry>();
    mGeometry->Name = GetName();

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(ObjVertex);
    const UINT ibByteSize = (UINT)indices.size() * sizeof(uint16_t);

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &mGeometry->VertexBufferCPU));
    CopyMemory(mGeometry->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &mGeometry->IndexBufferCPU));
    CopyMemory(mGeometry->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    mGeometry->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(device,
        cmdList, vertices.data(), vbByteSize, mGeometry->VertexBufferUploader);

    mGeometry->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(device,
        cmdList, indices.data(), ibByteSize, mGeometry->IndexBufferUploader);

    mGeometry->VertexByteStride = sizeof(ObjVertex);
    mGeometry->VertexBufferByteSize = vbByteSize;
    mGeometry->IndexFormat = DXGI_FORMAT_R16_UINT;
    mGeometry->IndexBufferByteSize = ibByteSize;

    SubmeshGeometry submesh;
    submesh.IndexCount = (UINT)indices.size();
    submesh.StartIndexLocation = 0;
    submesh.BaseVertexLocation = 0;
    mGeometry->DrawArgs["chandelier"] = submesh;

    mLightLocal = lightLocal;
    SetLocalBounds(localBounds);
    mObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(device, 1, true);

    if (textureManager)
        mTexture = textureManager->GetWhiteTexture();
    return true;
}

void ChandelierObject::Update(const GameTimer& gt)
{
    const float t = gt.TotalTime();
    const float swingZ = mAmplitude * sinf(t * mFrequency + mPhase);
    const float swingX = mAmplitude * 0.55f * sinf(t * mFrequency * 0.73f + mPhase + 0.9f);
    SetRotation(swingX, 0.0f, swingZ);
}

void ChandelierObject::UpdateConstantBuffer(FXMMATRIX view, FXMMATRIX proj)
{
    if (!mVisible || !mObjectCB)
        return;

    XMMATRIX world = GetWorldMatrix();
    XMMATRIX worldViewProj = world * view * proj;

    ObjectConstants oc{};
    XMStoreFloat4x4(&oc.WorldViewProj, XMMatrixTranspose(worldViewProj));
    XMStoreFloat4x4(&oc.World, XMMatrixTranspose(world));
    oc.DiffuseFactor = XMFLOAT4(3.4f, 2.15f, 0.72f, 1.0f);
    oc.UVScale = XMFLOAT2(1.0f, 1.0f);
    oc.UVOffset = XMFLOAT2(0.0f, 0.0f);
    oc.ReceiveShadow = 1.0f;
    oc.Metallic = 0.55f;
    oc.Roughness = 0.22f;
    oc.PadMaterial = 2.2f;
    mObjectCB->CopyData(0, oc);
}

void ChandelierObject::Draw(ID3D12GraphicsCommandList* cmdList)
{
    if (!mGeometry || !mVisible || !mObjectCB)
        return;

    cmdList->SetGraphicsRootConstantBufferView(0, mObjectCB->Resource()->GetGPUVirtualAddress());

    if (mTexture)
    {
        ID3D12DescriptorHeap* heap = mTexture->GetDescriptorHeap();
        cmdList->SetDescriptorHeaps(1, &heap);
        cmdList->SetGraphicsRootDescriptorTable(1, mTexture->GetSRV());
    }

    cmdList->IASetVertexBuffers(0, 1, &mGeometry->VertexBufferView());
    cmdList->IASetIndexBuffer(&mGeometry->IndexBufferView());
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->DrawIndexedInstanced(mGeometry->DrawArgs["chandelier"].IndexCount, 1, 0, 0, 0);
}

void ChandelierObject::DrawShadow(ID3D12GraphicsCommandList* cmdList)
{
    if (!mGeometry || !mVisible || !mObjectCB)
        return;

    cmdList->SetGraphicsRootConstantBufferView(0, mObjectCB->Resource()->GetGPUVirtualAddress());
    cmdList->IASetVertexBuffers(0, 1, &mGeometry->VertexBufferView());
    cmdList->IASetIndexBuffer(&mGeometry->IndexBufferView());
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->DrawIndexedInstanced(mGeometry->DrawArgs["chandelier"].IndexCount, 1, 0, 0, 0);
}
