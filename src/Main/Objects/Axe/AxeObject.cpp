#include "AxeObject.h"
#include "../../../Common/Assets/tiny_obj_loader.h"
#include "../../../Common/Rendering/RenderingSystem.h"
#include "../../../Common/Rendering/GBuffer.h"
#include <vector>
#include <string>
#include <algorithm>
#include <cfloat>

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

    Microsoft::WRL::ComPtr<ID3DBlob> CompileShaderFile(const wchar_t* name, const char* entry, const char* target)
    {
        std::wstring a = std::wstring(L"..\\Shaders\\") + name;
        std::wstring b = std::wstring(L"Shaders\\") + name;
        Microsoft::WRL::ComPtr<ID3DBlob> blob;
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
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

    std::string ExeDirectory()
    {
        char exePath[MAX_PATH];
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        char* slash = strrchr(exePath, '\\');
        if (slash)
            *slash = 0;
        return std::string(exePath);
    }
}

AxeObject::AxeObject() : GameObject("Axe")
{
    SetCullable(true);
}

AxeObject::AxeObject(const std::string& name) : GameObject(name)
{
    SetCullable(true);
}

bool AxeObject::Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, TextureManager* textureManager)
{
    if (!LoadMesh(device, cmdList))
        return false;
    if (!LoadMaps(device, cmdList, textureManager))
        return false;
    if (!BuildPipeline(device))
        return false;
    mObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(device, 1, true);
    return true;
}

void AxeObject::Update(const GameTimer& gt)
{
}

void AxeObject::UpdateConstantBuffer(FXMMATRIX view, FXMMATRIX proj)
{
    if (!mVisible || !mObjectCB)
        return;

    const XMMATRIX world = GetWorldMatrix();
    ObjectConstants obj{};
    XMStoreFloat4x4(&obj.WorldViewProj, XMMatrixTranspose(world * view * proj));
    XMStoreFloat4x4(&obj.World, XMMatrixTranspose(world));
    obj.DiffuseFactor = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    obj.UVScale = XMFLOAT2(1.0f, 1.0f);
    obj.ReceiveShadow = 1.0f;
    obj.Metallic = 0.0f;
    obj.Roughness = 0.5f;
    mObjectCB->CopyData(0, obj);
}

void AxeObject::Draw(ID3D12GraphicsCommandList* cmdList)
{
    if (!mGeometry || !mVisible || !mObjectCB || !mPSO)
        return;

    cmdList->SetGraphicsRootSignature(mRootSignature.Get());
    cmdList->SetPipelineState(mPSO.Get());
    cmdList->SetGraphicsRootConstantBufferView(0, mObjectCB->Resource()->GetGPUVirtualAddress());

    ID3D12DescriptorHeap* heap = mHeap.Get();
    cmdList->SetDescriptorHeaps(1, &heap);
    cmdList->SetGraphicsRootDescriptorTable(1, mHeap->GetGPUDescriptorHandleForHeapStart());

    cmdList->IASetVertexBuffers(0, 1, &mGeometry->VertexBufferView());
    cmdList->IASetIndexBuffer(&mGeometry->IndexBufferView());
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->DrawIndexedInstanced(mIndexCount, 1, 0, 0, 0);

    if (mRendering)
        mRendering->ApplyGeometryPass(cmdList);
}

void AxeObject::DrawShadow(ID3D12GraphicsCommandList* cmdList)
{
    if (!mGeometry || !mVisible || !mObjectCB)
        return;

    cmdList->SetGraphicsRootConstantBufferView(0, mObjectCB->Resource()->GetGPUVirtualAddress());
    cmdList->IASetVertexBuffers(0, 1, &mGeometry->VertexBufferView());
    cmdList->IASetIndexBuffer(&mGeometry->IndexBufferView());
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->DrawIndexedInstanced(mIndexCount, 1, 0, 0, 0);
}

bool AxeObject::LoadMesh(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList)
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    const std::string root = ExeDirectory() + "\\models";
    const std::string objPath = root + "\\axe.obj";
    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, objPath.c_str(), root.c_str(), true))
        return false;
    if (attrib.vertices.empty() || attrib.texcoords.empty() || shapes.empty())
        return false;

    std::vector<AxeVertex> vertices;
    std::vector<std::uint16_t> indices;
    XMFLOAT3 bmin(FLT_MAX, FLT_MAX, FLT_MAX);
    XMFLOAT3 bmax(-FLT_MAX, -FLT_MAX, -FLT_MAX);

    for (const auto& shape : shapes)
    {
        size_t indexOffset = 0;
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); ++f)
        {
            const int fv = shape.mesh.num_face_vertices[f];
            if (fv != 3)
            {
                indexOffset += (size_t)fv;
                continue;
            }
            for (int v = 0; v < 3; ++v)
            {
                const tinyobj::index_t idx = shape.mesh.indices[indexOffset + (size_t)v];
                AxeVertex vert{};
                vert.Pos = XMFLOAT3(
                    attrib.vertices[3 * (size_t)idx.vertex_index + 0],
                    attrib.vertices[3 * (size_t)idx.vertex_index + 1],
                    attrib.vertices[3 * (size_t)idx.vertex_index + 2]);
                if (idx.normal_index >= 0)
                {
                    vert.Normal = XMFLOAT3(
                        attrib.normals[3 * (size_t)idx.normal_index + 0],
                        attrib.normals[3 * (size_t)idx.normal_index + 1],
                        attrib.normals[3 * (size_t)idx.normal_index + 2]);
                }
                else
                {
                    vert.Normal = XMFLOAT3(0.0f, 1.0f, 0.0f);
                }
                vert.TexC = XMFLOAT2(
                    attrib.texcoords[2 * (size_t)idx.texcoord_index + 0],
                    1.0f - attrib.texcoords[2 * (size_t)idx.texcoord_index + 1]);
                vert.Tangent = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);

                bmin.x = (std::min)(bmin.x, vert.Pos.x);
                bmin.y = (std::min)(bmin.y, vert.Pos.y);
                bmin.z = (std::min)(bmin.z, vert.Pos.z);
                bmax.x = (std::max)(bmax.x, vert.Pos.x);
                bmax.y = (std::max)(bmax.y, vert.Pos.y);
                bmax.z = (std::max)(bmax.z, vert.Pos.z);

                if (vertices.size() >= 65535)
                    return false;
                indices.push_back((std::uint16_t)vertices.size());
                vertices.push_back(vert);
            }
            indexOffset += 3;
        }
    }
    if (indices.size() < 3)
        return false;

    std::vector<XMFLOAT3> bitangents(vertices.size(), XMFLOAT3(0.0f, 0.0f, 0.0f));
    for (size_t i = 0; i + 2 < indices.size(); i += 3)
    {
        AxeVertex& v0 = vertices[indices[i]];
        AxeVertex& v1 = vertices[indices[i + 1]];
        AxeVertex& v2 = vertices[indices[i + 2]];
        const XMVECTOR p0 = XMLoadFloat3(&v0.Pos);
        const XMVECTOR p1 = XMLoadFloat3(&v1.Pos);
        const XMVECTOR p2 = XMLoadFloat3(&v2.Pos);
        const XMVECTOR e1 = XMVectorSubtract(p1, p0);
        const XMVECTOR e2 = XMVectorSubtract(p2, p0);
        const float du1 = v1.TexC.x - v0.TexC.x;
        const float dv1 = v1.TexC.y - v0.TexC.y;
        const float du2 = v2.TexC.x - v0.TexC.x;
        const float dv2 = v2.TexC.y - v0.TexC.y;
        const float det = du1 * dv2 - du2 * dv1;
        if (fabsf(det) < 1e-8f)
            continue;
        const float inv = 1.0f / det;
        XMVECTOR t = XMVectorScale(XMVectorSubtract(XMVectorScale(e1, dv2), XMVectorScale(e2, dv1)), inv);
        XMVECTOR b = XMVectorScale(XMVectorSubtract(XMVectorScale(e2, du1), XMVectorScale(e1, du2)), inv);
        XMFLOAT3 tf, bf;
        XMStoreFloat3(&tf, t);
        XMStoreFloat3(&bf, b);
        for (int k = 0; k < 3; ++k)
        {
            AxeVertex& vert = vertices[indices[i + (size_t)k]];
            vert.Tangent.x += tf.x;
            vert.Tangent.y += tf.y;
            vert.Tangent.z += tf.z;
            XMFLOAT3& bit = bitangents[indices[i + (size_t)k]];
            bit.x += bf.x;
            bit.y += bf.y;
            bit.z += bf.z;
        }
    }

    for (size_t i = 0; i < vertices.size(); ++i)
    {
        XMVECTOR n = XMVector3Normalize(XMLoadFloat3(&vertices[i].Normal));
        XMVECTOR t = XMLoadFloat3(reinterpret_cast<XMFLOAT3*>(&vertices[i].Tangent));
        t = XMVectorSubtract(t, XMVectorScale(n, XMVectorGetX(XMVector3Dot(n, t))));
        if (XMVectorGetX(XMVector3LengthSq(t)) < 1e-8f)
            t = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
        t = XMVector3Normalize(t);
        XMVECTOR b = XMLoadFloat3(&bitangents[i]);
        const float sign = XMVectorGetX(XMVector3Dot(XMVector3Cross(n, t), b)) < 0.0f ? -1.0f : 1.0f;
        XMFLOAT3 tn;
        XMStoreFloat3(&tn, t);
        XMStoreFloat3(&vertices[i].Normal, n);
        vertices[i].Tangent = XMFLOAT4(tn.x, tn.y, tn.z, sign);
    }

    BoundingBox local;
    local.Center = XMFLOAT3(
        0.5f * (bmin.x + bmax.x),
        0.5f * (bmin.y + bmax.y),
        0.5f * (bmin.z + bmax.z));
    local.Extents = XMFLOAT3(
        0.5f * (bmax.x - bmin.x),
        0.5f * (bmax.y - bmin.y),
        0.5f * (bmax.z - bmin.z));
    SetLocalBounds(local);

    mIndexCount = (UINT)indices.size();
    mGeometry = std::make_shared<MeshGeometry>();
    mGeometry->Name = "axe";

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(AxeVertex);
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);
    mGeometry->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(device, cmdList, vertices.data(), vbByteSize, mGeometry->VertexBufferUploader);
    mGeometry->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(device, cmdList, indices.data(), ibByteSize, mGeometry->IndexBufferUploader);
    mGeometry->VertexByteStride = sizeof(AxeVertex);
    mGeometry->VertexBufferByteSize = vbByteSize;
    mGeometry->IndexFormat = DXGI_FORMAT_R16_UINT;
    mGeometry->IndexBufferByteSize = ibByteSize;

    SubmeshGeometry submesh;
    submesh.IndexCount = mIndexCount;
    submesh.StartIndexLocation = 0;
    submesh.BaseVertexLocation = 0;
    mGeometry->DrawArgs["axe"] = submesh;
    return true;
}

bool AxeObject::LoadMaps(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, TextureManager* textureManager)
{
    if (!textureManager)
        return false;

    const std::string dir = ExeDirectory() + "\\models\\textures\\";
    mBase = textureManager->LoadTexture(dir + "axe_Axe_BaseColor.png");
    mNormal = textureManager->LoadTexture(dir + "axe_Axe_Normal.png");
    mMetallic = textureManager->LoadTexture(dir + "axe_Axe_Metallic.png");
    mRoughness = textureManager->LoadTexture(dir + "axe_Axe_Roughness.png");
    if (!mBase || !mNormal || !mMetallic || !mRoughness)
        return false;

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = 4;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mHeap)));

    const UINT inc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    CD3DX12_CPU_DESCRIPTOR_HANDLE cpu(mHeap->GetCPUDescriptorHandleForHeapStart());
    Texture* maps[4] = { mBase.get(), mNormal.get(), mMetallic.get(), mRoughness.get() };
    for (Texture* map : maps)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = map->GetResource()->GetDesc().Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(map->GetResource(), &srvDesc, cpu);
        cpu.Offset(1, inc);
    }
    return true;
}

bool AxeObject::BuildPipeline(ID3D12Device* device)
{
    auto vs = CompileShaderFile(L"axe.hlsl", "VS", "vs_5_0");
    auto ps = CompileShaderFile(L"axe.hlsl", "PS", "ps_5_0");

    CD3DX12_DESCRIPTOR_RANGE range(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0);
    CD3DX12_ROOT_PARAMETER slot[2];
    slot[0].InitAsConstantBufferView(0);
    slot[1].InitAsDescriptorTable(1, &range);

    CD3DX12_STATIC_SAMPLER_DESC sampler(
        0,
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP);

    CD3DX12_ROOT_SIGNATURE_DESC rootDesc(2, slot, 1, &sampler,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    Microsoft::WRL::ComPtr<ID3DBlob> serialized;
    Microsoft::WRL::ComPtr<ID3DBlob> error;
    ThrowIfFailed(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serialized.GetAddressOf(), error.GetAddressOf()));
    ThrowIfFailed(device->CreateRootSignature(0, serialized->GetBufferPointer(),
        serialized->GetBufferSize(), IID_PPV_ARGS(&mRootSignature)));

    D3D12_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
    pso.InputLayout = { layout, 4 };
    pso.pRootSignature = mRootSignature.Get();
    pso.VS = { reinterpret_cast<BYTE*>(vs->GetBufferPointer()), vs->GetBufferSize() };
    pso.PS = { reinterpret_cast<BYTE*>(ps->GetBufferPointer()), ps->GetBufferSize() };
    pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pso.RasterizerState.FrontCounterClockwise = TRUE;
    pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    pso.SampleMask = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = GBuffer::ColorTargetCount;
    pso.RTVFormats[0] = GBuffer::AlbedoFormat;
    pso.RTVFormats[1] = GBuffer::NormalFormat;
    pso.DSVFormat = GBuffer::DepthFormat;
    pso.SampleDesc.Count = 1;
    ThrowIfFailed(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&mPSO)));
    return true;
}
