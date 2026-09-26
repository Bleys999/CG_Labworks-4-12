#include "BoxObject.h"
#include "../../../Common/Assets/GeometryGenerator.h"
#include <cmath>

using namespace DirectX;

std::shared_ptr<MeshGeometry> BoxObject::sSharedGeometry;

const std::array<Vertex, 8> BoxObject::sVertices = { {
    { XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT4(0.7f, 0.7f, 0.7f, 1.0f) },
    { XMFLOAT3(-1.0f, +1.0f, -1.0f), XMFLOAT4(0.7f, 0.7f, 0.7f, 1.0f) },
    { XMFLOAT3(+1.0f, +1.0f, -1.0f), XMFLOAT4(0.7f, 0.7f, 0.7f, 1.0f) },
    { XMFLOAT3(+1.0f, -1.0f, -1.0f), XMFLOAT4(0.7f, 0.7f, 0.7f, 1.0f) },
    { XMFLOAT3(-1.0f, -1.0f, +1.0f), XMFLOAT4(0.7f, 0.7f, 0.7f, 1.0f) },
    { XMFLOAT3(-1.0f, +1.0f, +1.0f), XMFLOAT4(0.7f, 0.7f, 0.7f, 1.0f) },
    { XMFLOAT3(+1.0f, +1.0f, +1.0f), XMFLOAT4(0.7f, 0.7f, 0.7f, 1.0f) },
    { XMFLOAT3(+1.0f, -1.0f, +1.0f), XMFLOAT4(0.7f, 0.7f, 0.7f, 1.0f) }
} };

const std::array<std::uint16_t, 36> BoxObject::sIndices = { {
    0, 1, 2, 0, 2, 3,
    4, 6, 5, 4, 7, 6,
    4, 5, 1, 4, 1, 0,
    3, 2, 6, 3, 6, 7,
    1, 5, 6, 1, 6, 2,
    4, 0, 3, 4, 3, 7
} };

BoxObject::BoxObject() : GameObject("Box")
{
    SetCullable(true);
    BoundingBox local;
    local.Center = XMFLOAT3(0.0f, 0.0f, 0.0f);
    const float e = fabsf(sVertices[0].Pos.x);
    local.Extents = XMFLOAT3(e, e, e);
    SetLocalBounds(local);
}

BoxObject::BoxObject(const std::string& name) : GameObject(name)
{
    SetCullable(true);
    BoundingBox local;
    local.Center = XMFLOAT3(0.0f, 0.0f, 0.0f);
    const float e = fabsf(sVertices[0].Pos.x);
    local.Extents = XMFLOAT3(e, e, e);
    SetLocalBounds(local);
}

bool BoxObject::Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList)
{
    if (!CreateSharedGeometry(device, cmdList))
        return false;

    mGeometry = sSharedGeometry;
    mObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(device, 1, true);
    return true;
}

bool BoxObject::Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, TextureManager* textureManager)
{
    if (!Initialize(device, cmdList))
        return false;

    if (textureManager)
        mTexture = textureManager->GetWhiteTexture();
    return true;
}

void BoxObject::Update(const GameTimer& gt)
{
}

void BoxObject::UpdateConstantBuffer(DirectX::FXMMATRIX view, DirectX::FXMMATRIX proj)
{
    if (!mVisible || !mObjectCB) return;

    XMMATRIX world = GetWorldMatrix();
    XMMATRIX worldViewProj = world * view * proj;

    ObjectConstants objConstants{};
    XMStoreFloat4x4(&objConstants.WorldViewProj, XMMatrixTranspose(worldViewProj));
    XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
    objConstants.DiffuseFactor = mDiffuseFactor;
    objConstants.UVScale = XMFLOAT2(1.0f, 1.0f);
    objConstants.UVOffset = XMFLOAT2(0.0f, 0.0f);
    objConstants.ReceiveShadow = 1.0f;
    objConstants.Metallic = mMetallic;
    objConstants.Roughness = mRoughness;

    mObjectCB->CopyData(0, objConstants);
}

void BoxObject::Draw(ID3D12GraphicsCommandList* cmdList)
{
    if (!mGeometry || !mVisible || !mObjectCB) return;

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

    cmdList->DrawIndexedInstanced(mGeometry->DrawArgs["box"].IndexCount, 1, 0, 0, 0);
}

void BoxObject::DrawShadow(ID3D12GraphicsCommandList* cmdList)
{
    if (!mGeometry || !mVisible || !mObjectCB) return;

    cmdList->SetGraphicsRootConstantBufferView(0, mObjectCB->Resource()->GetGPUVirtualAddress());
    cmdList->IASetVertexBuffers(0, 1, &mGeometry->VertexBufferView());
    cmdList->IASetIndexBuffer(&mGeometry->IndexBufferView());
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->DrawIndexedInstanced(mGeometry->DrawArgs["box"].IndexCount, 1, 0, 0, 0);
}

bool BoxObject::CreateSharedGeometry(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList)
{
    if (sSharedGeometry)
        return true;

    GeometryGenerator geoGen;
    GeometryGenerator::MeshData box = geoGen.CreateBox(2.0f, 2.0f, 2.0f, 0);

    std::vector<ObjVertex> vertices(box.Vertices.size());
    for (size_t i = 0; i < box.Vertices.size(); ++i)
    {
        vertices[i].Pos = box.Vertices[i].Position;
        vertices[i].Normal = box.Vertices[i].Normal;
        vertices[i].TexC = box.Vertices[i].TexC;
    }

    const std::vector<std::uint16_t>& indices = box.GetIndices16();

    sSharedGeometry = std::make_shared<MeshGeometry>();
    sSharedGeometry->Name = "boxGeo";

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(ObjVertex);
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &sSharedGeometry->VertexBufferCPU));
    CopyMemory(sSharedGeometry->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &sSharedGeometry->IndexBufferCPU));
    CopyMemory(sSharedGeometry->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    sSharedGeometry->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(device,
        cmdList, vertices.data(), vbByteSize, sSharedGeometry->VertexBufferUploader);

    sSharedGeometry->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(device,
        cmdList, indices.data(), ibByteSize, sSharedGeometry->IndexBufferUploader);

    sSharedGeometry->VertexByteStride = sizeof(ObjVertex);
    sSharedGeometry->VertexBufferByteSize = vbByteSize;
    sSharedGeometry->IndexFormat = DXGI_FORMAT_R16_UINT;
    sSharedGeometry->IndexBufferByteSize = ibByteSize;

    SubmeshGeometry submesh;
    submesh.IndexCount = (UINT)indices.size();
    submesh.StartIndexLocation = 0;
    submesh.BaseVertexLocation = 0;
    submesh.Bounds.Center = XMFLOAT3(0.0f, 0.0f, 0.0f);
    submesh.Bounds.Extents = XMFLOAT3(1.0f, 1.0f, 1.0f);

    sSharedGeometry->DrawArgs["box"] = submesh;

    return true;
}
