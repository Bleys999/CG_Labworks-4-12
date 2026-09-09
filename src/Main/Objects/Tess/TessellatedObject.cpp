#include "TessellatedObject.h"
#include "../../../Common/Assets/GeometryGenerator.h"
#include "../../../Common/Core/Config.h"
#include <cstring>
#include <vector>
#include <cmath>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

std::shared_ptr<MeshGeometry> TessellatedObject::sSharedGeometry;
std::shared_ptr<Texture> TessellatedObject::sAlbedo;
std::shared_ptr<Texture> TessellatedObject::sNormal;
std::shared_ptr<Texture> TessellatedObject::sDisplacement;
ComPtr<ID3D12DescriptorHeap> TessellatedObject::sSrvHeap;

namespace
{
    std::string ModelsRoot()
    {
        char exePath[MAX_PATH];
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        char* lastSlash = strrchr(exePath, '\\');
        if (lastSlash)
            *lastSlash = 0;
        return std::string(exePath) + "\\models";
    }

    void MakeBrickFallback(
        std::vector<uint8_t>& albedo,
        std::vector<uint8_t>& normal,
        std::vector<uint8_t>& height,
        UINT dim)
    {
        albedo.resize(dim * dim * 4);
        normal.resize(dim * dim * 4);
        height.resize(dim * dim * 4);

        const float brickW = 32.0f;
        const float brickH = 16.0f;
        const float mortar = 3.0f;

        for (UINT y = 0; y < dim; ++y)
        {
            for (UINT x = 0; x < dim; ++x)
            {
                float row = floorf((float)y / brickH);
                float ox = (fmodf(row, 2.0f) > 0.5f) ? brickW * 0.5f : 0.0f;
                float lx = fmodf((float)x + ox, brickW);
                float ly = fmodf((float)y, brickH);
                bool mortarPx = lx < mortar || ly < mortar;
                float h = mortarPx ? 0.12f : 0.85f;

                UINT i = (y * dim + x) * 4;
                if (mortarPx)
                {
                    albedo[i + 0] = 70;
                    albedo[i + 1] = 68;
                    albedo[i + 2] = 64;
                }
                else
                {
                    albedo[i + 0] = 168;
                    albedo[i + 1] = 92;
                    albedo[i + 2] = 62;
                }
                albedo[i + 3] = 255;

                BYTE hv = (BYTE)(h * 255.0f);
                height[i + 0] = hv;
                height[i + 1] = hv;
                height[i + 2] = hv;
                height[i + 3] = 255;
            }
        }

        auto HeightAt = [&](int x, int y) -> float
        {
            if (x < 0) x = 0;
            if (y < 0) y = 0;
            if (x >= (int)dim) x = (int)dim - 1;
            if (y >= (int)dim) y = (int)dim - 1;
            return height[(y * dim + x) * 4] / 255.0f;
        };

        for (UINT y = 0; y < dim; ++y)
        {
            for (UINT x = 0; x < dim; ++x)
            {
                float dx = HeightAt((int)x - 1, (int)y) - HeightAt((int)x + 1, (int)y);
                float dy = HeightAt((int)x, (int)y - 1) - HeightAt((int)x, (int)y + 1);
                XMVECTOR n = XMVector3Normalize(XMVectorSet(dx * 4.0f, dy * 4.0f, 1.0f, 0.0f));
                XMFLOAT3 nf;
                XMStoreFloat3(&nf, n);
                UINT i = (y * dim + x) * 4;
                normal[i + 0] = (BYTE)((nf.x * 0.5f + 0.5f) * 255.0f);
                normal[i + 1] = (BYTE)((nf.y * 0.5f + 0.5f) * 255.0f);
                normal[i + 2] = (BYTE)((nf.z * 0.5f + 0.5f) * 255.0f);
                normal[i + 3] = 255;
            }
        }
    }
}

TessellatedObject::TessellatedObject() : GameObject("Tess")
{
    SetCullable(false);
    BoundingBox local;
    local.Center = XMFLOAT3(0.0f, 0.0f, 0.0f);
    local.Extents = XMFLOAT3(6.0f, 1.0f, 6.0f);
    SetLocalBounds(local);
}

TessellatedObject::TessellatedObject(const std::string& name) : GameObject(name)
{
    SetCullable(false);
    BoundingBox local;
    local.Center = XMFLOAT3(0.0f, 0.0f, 0.0f);
    local.Extents = XMFLOAT3(6.0f, 1.0f, 6.0f);
    SetLocalBounds(local);
}

bool TessellatedObject::Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, TextureManager* textureManager)
{
    if (!CreateSharedResources(device, cmdList, textureManager))
        return false;
    if (!CreateMaterialHeap(device))
        return false;

    mGeometry = sSharedGeometry;
    mObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(device, 1, true);
    return true;
}

void TessellatedObject::SetFrameCamera(const XMFLOAT3& pos)
{
    mCameraWorld = pos;
}

void TessellatedObject::Update(const GameTimer& gt)
{
}

void TessellatedObject::UpdateConstantBuffer(FXMMATRIX view, FXMMATRIX proj)
{
    if (!mVisible || !mObjectCB)
        return;

    XMMATRIX world = GetWorldMatrix();
    XMMATRIX worldViewProj = world * view * proj;

    ObjectConstants oc{};
    XMStoreFloat4x4(&oc.WorldViewProj, XMMatrixTranspose(worldViewProj));
    XMStoreFloat4x4(&oc.World, XMMatrixTranspose(world));
    oc.DiffuseFactor = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    oc.UVScale = XMFLOAT2(6.0f, 6.0f);
    oc.UVOffset = XMFLOAT2(0.0f, 0.0f);
    oc.CameraWorld = mCameraWorld;
    oc.DisplacementScale = Config::DisplacementScale;
    oc.TessMin = Config::TessMin;
    oc.TessMax = Config::TessMax;
    oc.TessNear = Config::TessNear;
    oc.TessFar = Config::TessFar;
    oc.ReceiveShadow = 1.0f;
    mObjectCB->CopyData(0, oc);
}

void TessellatedObject::Draw(ID3D12GraphicsCommandList* cmdList)
{
    if (!mGeometry || !mVisible || !mObjectCB || !sSrvHeap)
        return;

    cmdList->SetGraphicsRootConstantBufferView(0, mObjectCB->Resource()->GetGPUVirtualAddress());

    ID3D12DescriptorHeap* heaps[] = { sSrvHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);
    cmdList->SetGraphicsRootDescriptorTable(1, sSrvHeap->GetGPUDescriptorHandleForHeapStart());

    cmdList->IASetVertexBuffers(0, 1, &mGeometry->VertexBufferView());
    cmdList->IASetIndexBuffer(&mGeometry->IndexBufferView());
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);

    cmdList->DrawIndexedInstanced(mGeometry->DrawArgs["tess"].IndexCount, 1, 0, 0, 0);
}

bool TessellatedObject::CreateSharedResources(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, TextureManager* textureManager)
{
    if (sSharedGeometry && sAlbedo && sNormal && sDisplacement)
        return true;

    if (!sSharedGeometry)
    {
        GeometryGenerator geoGen;
        GeometryGenerator::MeshData grid = geoGen.CreateGrid(12.0f, 12.0f, 9, 9);

        std::vector<TessVertex> vertices(grid.Vertices.size());
        for (size_t i = 0; i < grid.Vertices.size(); ++i)
        {
            vertices[i].Pos = grid.Vertices[i].Position;
            vertices[i].Normal = grid.Vertices[i].Normal;
            vertices[i].Tangent = grid.Vertices[i].TangentU;
            vertices[i].TexC = grid.Vertices[i].TexC;
        }

        const std::vector<std::uint16_t>& indices = grid.GetIndices16();

        sSharedGeometry = std::make_shared<MeshGeometry>();
        sSharedGeometry->Name = "tessGrid";

        const UINT vbByteSize = (UINT)vertices.size() * sizeof(TessVertex);
        const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

        ThrowIfFailed(D3DCreateBlob(vbByteSize, &sSharedGeometry->VertexBufferCPU));
        CopyMemory(sSharedGeometry->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);
        ThrowIfFailed(D3DCreateBlob(ibByteSize, &sSharedGeometry->IndexBufferCPU));
        CopyMemory(sSharedGeometry->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

        sSharedGeometry->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(device,
            cmdList, vertices.data(), vbByteSize, sSharedGeometry->VertexBufferUploader);
        sSharedGeometry->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(device,
            cmdList, indices.data(), ibByteSize, sSharedGeometry->IndexBufferUploader);

        sSharedGeometry->VertexByteStride = sizeof(TessVertex);
        sSharedGeometry->VertexBufferByteSize = vbByteSize;
        sSharedGeometry->IndexFormat = DXGI_FORMAT_R16_UINT;
        sSharedGeometry->IndexBufferByteSize = ibByteSize;

        SubmeshGeometry submesh;
        submesh.IndexCount = (UINT)indices.size();
        submesh.StartIndexLocation = 0;
        submesh.BaseVertexLocation = 0;
        sSharedGeometry->DrawArgs["tess"] = submesh;
    }

    const std::string texDir = ModelsRoot() + "\\textures\\";
    const std::string albedoPath = texDir + "brickwall_01_BaseColor.png";
    const std::string normalPath = texDir + "brickwall_01_Normal.png";

    if (textureManager)
    {
        sAlbedo = textureManager->LoadTexture(albedoPath);
        sNormal = textureManager->LoadTexture(normalPath);
    }

    std::vector<uint8_t> albedoPx, heightPx;
    UINT aw = 0, ah = 0;
    if (Texture::DecodeImageRGBA(albedoPath, albedoPx, aw, ah) && aw > 0 && ah > 0)
    {
        heightPx.resize(albedoPx.size());
        for (size_t i = 0; i < albedoPx.size(); i += 4)
        {
            float lum = (albedoPx[i] * 0.299f + albedoPx[i + 1] * 0.587f + albedoPx[i + 2] * 0.114f) / 255.0f;
            BYTE h = (BYTE)(lum * 255.0f);
            heightPx[i] = h;
            heightPx[i + 1] = h;
            heightPx[i + 2] = h;
            heightPx[i + 3] = 255;
        }
        sDisplacement = std::make_shared<Texture>();
        if (!sDisplacement->CreateFromRGBA8(device, cmdList, aw, ah, heightPx.data()))
            sDisplacement.reset();
    }

    if (!sAlbedo || !sNormal || !sDisplacement)
    {
        std::vector<uint8_t> a, n, h;
        const UINT dim = 256;
        MakeBrickFallback(a, n, h, dim);

        if (!sAlbedo)
        {
            sAlbedo = std::make_shared<Texture>();
            if (!sAlbedo->CreateFromRGBA8(device, cmdList, dim, dim, a.data()))
                return false;
        }
        if (!sNormal)
        {
            sNormal = std::make_shared<Texture>();
            if (!sNormal->CreateFromRGBA8(device, cmdList, dim, dim, n.data()))
                return false;
        }
        if (!sDisplacement)
        {
            sDisplacement = std::make_shared<Texture>();
            if (!sDisplacement->CreateFromRGBA8(device, cmdList, dim, dim, h.data()))
                return false;
        }
    }

    return sAlbedo && sNormal && sDisplacement && sSharedGeometry;
}

bool TessellatedObject::CreateMaterialHeap(ID3D12Device* device)
{
    if (sSrvHeap)
        return true;
    if (!sAlbedo || !sNormal || !sDisplacement)
        return false;

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = 3;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&sSrvHeap)));

    UINT inc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    CD3DX12_CPU_DESCRIPTOR_HANDLE cpu(sSrvHeap->GetCPUDescriptorHandleForHeapStart());

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    ID3D12Resource* maps[3] = { sAlbedo->GetResource(), sNormal->GetResource(), sDisplacement->GetResource() };
    for (int i = 0; i < 3; ++i)
    {
        srvDesc.Format = maps[i]->GetDesc().Format;
        device->CreateShaderResourceView(maps[i], &srvDesc, cpu);
        cpu.Offset(1, inc);
    }

    return true;
}
