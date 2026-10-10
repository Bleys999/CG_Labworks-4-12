#include "SponzaObject.h"
#include <cstring>
#include <string>
#include <vector>
#include <cmath>
#include <cfloat>
#include <algorithm>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace
{
    std::string JoinUnderModels(const std::string& modelsRoot, const std::string& rel)
    {
        std::string p = rel;
        while (!p.empty() && (p.front() == ' ' || p.front() == '\t'))
            p.erase(p.begin());
        while (!p.empty() && (p.back() == ' ' || p.back() == '\t'))
            p.pop_back();
        if (p.empty())
            return {};
        for (char& c : p)
        {
            if (c == '/')
                c = '\\';
        }
        return modelsRoot + "\\" + p;
    }

    std::string ToNormalMapPath(const std::string& albedoPath)
    {
        std::string p = albedoPath;
        const char* from = "BaseColor";
        const char* to = "Normal";
        size_t at = p.find(from);
        if (at == std::string::npos)
            return {};
        p.replace(at, strlen(from), to);
        return p;
    }

    void ComputeTangents(std::vector<TessVertex>& vertices, const std::vector<uint32_t>& indices)
    {
        for (size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            TessVertex& v0 = vertices[indices[i + 0]];
            TessVertex& v1 = vertices[indices[i + 1]];
            TessVertex& v2 = vertices[indices[i + 2]];

            XMVECTOR p0 = XMLoadFloat3(&v0.Pos);
            XMVECTOR p1 = XMLoadFloat3(&v1.Pos);
            XMVECTOR p2 = XMLoadFloat3(&v2.Pos);
            XMVECTOR e1 = XMVectorSubtract(p1, p0);
            XMVECTOR e2 = XMVectorSubtract(p2, p0);

            float du1 = v1.TexC.x - v0.TexC.x;
            float dv1 = v1.TexC.y - v0.TexC.y;
            float du2 = v2.TexC.x - v0.TexC.x;
            float dv2 = v2.TexC.y - v0.TexC.y;
            float det = du1 * dv2 - du2 * dv1;
            if (fabsf(det) < 1e-8f)
                det = 1.0f;
            float inv = 1.0f / det;

            XMVECTOR t = XMVectorScale(
                XMVectorSubtract(XMVectorScale(e1, dv2), XMVectorScale(e2, dv1)),
                inv);
            t = XMVector3Normalize(t);

            XMFLOAT3 tf;
            XMStoreFloat3(&tf, t);
            v0.Tangent = tf;
            v1.Tangent = tf;
            v2.Tangent = tf;
        }
    }

    bool IsSponzaChandelierShape(const std::string& name)
    {
        return name.find("vase_hanging") != std::string::npos ||
            name.find("chain") != std::string::npos;
    }

    struct LampPart
    {
        std::vector<ObjVertex> Vertices;
        float SumX = 0.0f;
        float SumY = 0.0f;
        float SumZ = 0.0f;
    };
}

SponzaObject::SponzaObject() : GameObject("Sponza")
{
}

SponzaObject::SponzaObject(const std::string& name) : GameObject(name)
{
}

bool SponzaObject::Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, TextureManager* textureManager)
{
    mTextureManager = textureManager;

    mFlatNormal = std::make_shared<Texture>();
    if (!mFlatNormal->Create1x1RGBA8(device, cmdList, 128, 128, 255, 255))
        return false;
    mFlatHeight = std::make_shared<Texture>();
    if (!mFlatHeight->Create1x1RGBA8(device, cmdList, 128, 128, 128, 255))
        return false;

    if (!LoadModel(device, cmdList))
        return false;

    mObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(device, 1, true);
    return true;
}

void SponzaObject::SetFrameCamera(const XMFLOAT3& pos)
{
    mCameraWorld = pos;
}

void SponzaObject::Update(const GameTimer& gt)
{
}

void SponzaObject::UpdateConstantBuffer(FXMMATRIX view, FXMMATRIX proj)
{
    if (!mVisible) return;

    XMMATRIX world = GetWorldMatrix();
    XMMATRIX worldViewProj = world * view * proj;
    XMStoreFloat4x4(&mWorldViewProj, XMMatrixTranspose(worldViewProj));
    XMStoreFloat4x4(&mWorld, XMMatrixTranspose(world));
}

void SponzaObject::Draw(ID3D12GraphicsCommandList* cmdList)
{
    if (!mGeometry || !mVisible || !mObjectCB) return;

    cmdList->IASetVertexBuffers(0, 1, &mGeometry->VertexBufferView());
    cmdList->IASetIndexBuffer(&mGeometry->IndexBufferView());
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);

    for (const auto& batch : mMaterialBatches)
    {
        if (batch.IndexCount == 0 || !batch.SrvHeap)
            continue;

        ObjectConstants oc{};
        oc.WorldViewProj = mWorldViewProj;
        oc.World = mWorld;
        oc.DiffuseFactor = batch.DiffuseFactor;
        oc.UVScale = mUVScale;
        oc.UVOffset = mUVOffset;
        oc.CameraWorld = mCameraWorld;
        oc.DisplacementScale = Config::DisplacementScale;
        oc.TessMin = Config::TessMin;
        oc.TessMax = Config::TessMax;
        oc.TessNear = Config::TessNear;
        oc.TessFar = Config::TessFar;
        oc.ReceiveShadow = 1.0f;
        oc.Metallic = batch.Metallic;
        oc.Roughness = batch.Roughness;
        mObjectCB->CopyData(0, oc);

        cmdList->SetGraphicsRootConstantBufferView(0, mObjectCB->Resource()->GetGPUVirtualAddress());

        ID3D12DescriptorHeap* heap = batch.SrvHeap.Get();
        cmdList->SetDescriptorHeaps(1, &heap);
        cmdList->SetGraphicsRootDescriptorTable(1, batch.SrvHeap->GetGPUDescriptorHandleForHeapStart());

        cmdList->DrawIndexedInstanced(batch.IndexCount, 1, batch.StartIndexLocation, 0, 0);
    }
}

void SponzaObject::DrawShadow(ID3D12GraphicsCommandList* cmdList)
{
    if (!mGeometry || !mVisible || !mObjectCB) return;

    ObjectConstants oc{};
    oc.WorldViewProj = mWorldViewProj;
    oc.World = mWorld;
    mObjectCB->CopyData(0, oc);

    cmdList->SetGraphicsRootConstantBufferView(0, mObjectCB->Resource()->GetGPUVirtualAddress());
    cmdList->IASetVertexBuffers(0, 1, &mGeometry->VertexBufferView());
    cmdList->IASetIndexBuffer(&mGeometry->IndexBufferView());
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->DrawIndexedInstanced(mGeometry->DrawArgs["sponza"].IndexCount, 1, 0, 0, 0);
}

std::shared_ptr<Texture> SponzaObject::LoadDisplacement(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const std::string& albedoPath)
{
    if (albedoPath.empty())
        return mFlatHeight;

    auto found = mDisplacementCache.find(albedoPath);
    if (found != mDisplacementCache.end())
        return found->second;

    std::vector<uint8_t> pixels;
    UINT w = 0, h = 0;
    if (!Texture::DecodeImageRGBA(albedoPath, pixels, w, h) || w == 0 || h == 0)
    {
        mDisplacementCache[albedoPath] = mFlatHeight;
        return mFlatHeight;
    }

    for (size_t i = 0; i < pixels.size(); i += 4)
    {
        float lum = (pixels[i] * 0.299f + pixels[i + 1] * 0.587f + pixels[i + 2] * 0.114f) / 255.0f;
        BYTE v = (BYTE)(lum * 255.0f);
        pixels[i] = v;
        pixels[i + 1] = v;
        pixels[i + 2] = v;
        pixels[i + 3] = 255;
    }

    auto tex = std::make_shared<Texture>();
    if (!tex->CreateFromRGBA8(device, cmdList, w, h, pixels.data()))
    {
        mDisplacementCache[albedoPath] = mFlatHeight;
        return mFlatHeight;
    }

    mDisplacementCache[albedoPath] = tex;
    return tex;
}

bool SponzaObject::BuildBatchHeap(ID3D12Device* device, MaterialDrawBatch& batch)
{
    if (!batch.DiffuseTexture || !batch.NormalTexture || !batch.DisplacementTexture)
        return false;

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = 3;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&batch.SrvHeap)));

    UINT inc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    CD3DX12_CPU_DESCRIPTOR_HANDLE cpu(batch.SrvHeap->GetCPUDescriptorHandleForHeapStart());

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    ID3D12Resource* maps[3] = {
        batch.DiffuseTexture->GetResource(),
        batch.NormalTexture->GetResource(),
        batch.DisplacementTexture->GetResource()
    };
    for (int i = 0; i < 3; ++i)
    {
        srvDesc.Format = maps[i]->GetDesc().Format;
        device->CreateShaderResourceView(maps[i], &srvDesc, cpu);
        cpu.Offset(1, inc);
    }
    return true;
}

bool SponzaObject::LoadModel(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList)
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    char* lastSlash = strrchr(exePath, '\\');
    if (lastSlash) *lastSlash = 0;

    std::string modelsRoot = std::string(exePath) + "\\models";
    std::string objPath = modelsRoot + "\\sponza.obj";

    bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err,
        objPath.c_str(), modelsRoot.c_str(), true);

    if (!ret)
    {
        MessageBoxA(nullptr, "Failed to load OBJ file", "Error", MB_OK);
        return false;
    }

    struct MatMaps
    {
        std::shared_ptr<Texture> Diffuse;
        std::shared_ptr<Texture> Normal;
        std::shared_ptr<Texture> Displacement;
        XMFLOAT4 Kd = { 1.0f, 1.0f, 1.0f, 1.0f };
        float Metallic = 0.0f;
        float Roughness = 0.55f;
    };

    std::vector<MatMaps> matTable;
    if (materials.empty())
    {
        auto white = mTextureManager->GetWhiteTexture();
        if (!white)
            return false;
        matTable.push_back({ white, mFlatNormal, mFlatHeight, XMFLOAT4(0.75f, 0.75f, 0.75f, 1.0f), 0.0f, 0.55f });
    }
    else
    {
        for (const auto& m : materials)
        {
            XMFLOAT4 kd(
                (float)m.diffuse[0],
                (float)m.diffuse[1],
                (float)m.diffuse[2],
                1.0f);

            std::shared_ptr<Texture> diffuse = mTextureManager->GetWhiteTexture();
            std::string albedoPath;
            if (!m.diffuse_texname.empty())
            {
                albedoPath = JoinUnderModels(modelsRoot, m.diffuse_texname);
                auto tex = mTextureManager->LoadTexture(albedoPath);
                if (tex)
                    diffuse = tex;
            }
            if (!diffuse)
                return false;

            std::shared_ptr<Texture> normal = mFlatNormal;
            std::string npath = ToNormalMapPath(albedoPath);
            if (!npath.empty())
            {
                auto ntex = mTextureManager->LoadTexture(npath);
                if (ntex)
                    normal = ntex;
            }

            auto disp = LoadDisplacement(device, cmdList, albedoPath);
            float ns = (float)m.shininess;
            float rough = 1.0f - (std::max)(0.0f, (std::min)(1.0f, ns / 256.0f));
            if (ns <= 1.0f)
                rough = 0.62f;
            rough = (std::max)(0.18f, (std::min)(0.92f, rough));
            float metal = 0.0f;
            std::string nm = m.name;
            if (nm.find("metal") != std::string::npos || nm.find("gold") != std::string::npos ||
                nm.find("bronze") != std::string::npos || nm.find("chain") != std::string::npos)
                metal = 0.85f;
            matTable.push_back({ diffuse, normal, disp, kd, metal, rough });
        }
    }

    std::vector<TessVertex> vertices;
    std::vector<uint32_t> indices;
    mMaterialBatches.clear();

    XMFLOAT3 boundMin(FLT_MAX, FLT_MAX, FLT_MAX);
    XMFLOAT3 boundMax(-FLT_MAX, -FLT_MAX, -FLT_MAX);

    bool hasTexCoords = !attrib.texcoords.empty();

    int activeMat = -1;
    UINT rangeStart = 0;
    std::vector<LampPart> lampParts;
    mExtractedChandeliers.clear();

    auto flushRange = [&](UINT rangeEnd)
    {
        if (rangeEnd <= rangeStart || activeMat < 0)
            return;
        int mid = activeMat;
        if (mid >= (int)matTable.size())
            mid = 0;

        MaterialDrawBatch batch;
        batch.StartIndexLocation = rangeStart;
        batch.IndexCount = rangeEnd - rangeStart;
        batch.DiffuseTexture = matTable[mid].Diffuse;
        batch.NormalTexture = matTable[mid].Normal;
        batch.DisplacementTexture = matTable[mid].Displacement;
        batch.DiffuseFactor = matTable[mid].Kd;
        batch.Metallic = matTable[mid].Metallic;
        batch.Roughness = matTable[mid].Roughness;
        if (!BuildBatchHeap(device, batch))
            return;
        mMaterialBatches.push_back(std::move(batch));
        rangeStart = rangeEnd;
    };

    for (size_t s = 0; s < shapes.size(); s++)
    {
        size_t index_offset = 0;
        const auto& midVec = shapes[s].mesh.material_ids;

        if (IsSponzaChandelierShape(shapes[s].name))
        {
            LampPart part;
            for (size_t f = 0; f < shapes[s].mesh.num_face_vertices.size(); f++)
            {
                int fv = shapes[s].mesh.num_face_vertices[f];
                for (size_t v = 0; v < (size_t)fv; v++)
                {
                    tinyobj::index_t idx = shapes[s].mesh.indices[index_offset + v];
                    float vx = attrib.vertices[3 * idx.vertex_index + 0];
                    float vy = attrib.vertices[3 * idx.vertex_index + 1];
                    float vz = attrib.vertices[3 * idx.vertex_index + 2];
                    float nx = 0.0f, ny = 1.0f, nz = 0.0f;
                    if (idx.normal_index >= 0)
                    {
                        nx = attrib.normals[3 * idx.normal_index + 0];
                        ny = attrib.normals[3 * idx.normal_index + 1];
                        nz = attrib.normals[3 * idx.normal_index + 2];
                    }
                    float tx = 0.0f, ty = 0.0f;
                    if (hasTexCoords && idx.texcoord_index >= 0)
                    {
                        tx = attrib.texcoords[2 * idx.texcoord_index + 0];
                        ty = attrib.texcoords[2 * idx.texcoord_index + 1];
                    }
                    ObjVertex vertex;
                    vertex.Pos = XMFLOAT3(vx, vy, vz);
                    vertex.Normal = XMFLOAT3(nx, ny, nz);
                    vertex.TexC = XMFLOAT2(tx, ty);
                    part.Vertices.push_back(vertex);
                    part.SumX += vx;
                    part.SumY += vy;
                    part.SumZ += vz;
                }
                index_offset += (size_t)fv;
            }
            if (!part.Vertices.empty())
                lampParts.push_back(std::move(part));
            continue;
        }

        for (size_t f = 0; f < shapes[s].mesh.num_face_vertices.size(); f++)
        {
            int fv = shapes[s].mesh.num_face_vertices[f];

            int mid = 0;
            if (f < midVec.size())
                mid = midVec[f];
            if (mid < 0 || mid >= (int)matTable.size())
                mid = 0;

            UINT idxBefore = (UINT)indices.size();

            if (activeMat < 0)
            {
                activeMat = mid;
                rangeStart = idxBefore;
            }
            else if (mid != activeMat)
            {
                flushRange(idxBefore);
                activeMat = mid;
                rangeStart = idxBefore;
            }

            for (size_t v = 0; v < (size_t)fv; v++)
            {
                tinyobj::index_t idx = shapes[s].mesh.indices[index_offset + v];

                float vx = attrib.vertices[3 * idx.vertex_index + 0];
                float vy = attrib.vertices[3 * idx.vertex_index + 1];
                float vz = attrib.vertices[3 * idx.vertex_index + 2];

                float nx = 0.0f, ny = 1.0f, nz = 0.0f;
                if (idx.normal_index >= 0)
                {
                    nx = attrib.normals[3 * idx.normal_index + 0];
                    ny = attrib.normals[3 * idx.normal_index + 1];
                    nz = attrib.normals[3 * idx.normal_index + 2];
                }

                float tx = 0.0f, ty = 0.0f;
                if (hasTexCoords && idx.texcoord_index >= 0)
                {
                    tx = attrib.texcoords[2 * idx.texcoord_index + 0];
                    ty = attrib.texcoords[2 * idx.texcoord_index + 1];
                }

                TessVertex vertex;
                vertex.Pos = XMFLOAT3(vx, vy, vz);
                vertex.Normal = XMFLOAT3(nx, ny, nz);
                vertex.Tangent = XMFLOAT3(1.0f, 0.0f, 0.0f);
                vertex.TexC = XMFLOAT2(tx, ty);

                boundMin.x = (std::min)(boundMin.x, vx);
                boundMin.y = (std::min)(boundMin.y, vy);
                boundMin.z = (std::min)(boundMin.z, vz);
                boundMax.x = (std::max)(boundMax.x, vx);
                boundMax.y = (std::max)(boundMax.y, vy);
                boundMax.z = (std::max)(boundMax.z, vz);

                vertices.push_back(vertex);
                indices.push_back(static_cast<uint32_t>(vertices.size() - 1));
            }
            index_offset += (size_t)fv;
        }
    }

    flushRange((UINT)indices.size());

    {
        const float mergeDistSq = 80.0f * 80.0f;
        struct Cluster
        {
            std::vector<int> Ids;
            float Cx = 0.0f;
            float Cz = 0.0f;
            int Count = 0;
        };
        std::vector<Cluster> clusters;
        for (int i = 0; i < (int)lampParts.size(); ++i)
        {
            const LampPart& part = lampParts[i];
            const float inv = 1.0f / (float)part.Vertices.size();
            const float cx = part.SumX * inv;
            const float cz = part.SumZ * inv;
            int found = -1;
            float best = mergeDistSq;
            for (int c = 0; c < (int)clusters.size(); ++c)
            {
                const float dx = cx - clusters[c].Cx;
                const float dz = cz - clusters[c].Cz;
                const float d2 = dx * dx + dz * dz;
                if (d2 < best)
                {
                    best = d2;
                    found = c;
                }
            }
            if (found < 0)
            {
                Cluster cl;
                cl.Ids.push_back(i);
                cl.Cx = cx;
                cl.Cz = cz;
                cl.Count = 1;
                clusters.push_back(cl);
            }
            else
            {
                Cluster& cl = clusters[found];
                cl.Ids.push_back(i);
                cl.Cx = (cl.Cx * (float)cl.Count + cx) / (float)(cl.Count + 1);
                cl.Cz = (cl.Cz * (float)cl.Count + cz) / (float)(cl.Count + 1);
                cl.Count += 1;
            }
        }

        for (const Cluster& cl : clusters)
        {
            ExtractedChandelier lamp;
            XMFLOAT3 bmin(FLT_MAX, FLT_MAX, FLT_MAX);
            XMFLOAT3 bmax(-FLT_MAX, -FLT_MAX, -FLT_MAX);
            for (int id : cl.Ids)
            {
                for (const ObjVertex& v : lampParts[id].Vertices)
                {
                    lamp.Vertices.push_back(v);
                    bmin.x = (std::min)(bmin.x, v.Pos.x);
                    bmin.y = (std::min)(bmin.y, v.Pos.y);
                    bmin.z = (std::min)(bmin.z, v.Pos.z);
                    bmax.x = (std::max)(bmax.x, v.Pos.x);
                    bmax.y = (std::max)(bmax.y, v.Pos.y);
                    bmax.z = (std::max)(bmax.z, v.Pos.z);
                }
            }
            if (lamp.Vertices.size() < 3 || lamp.Vertices.size() > 65535)
                continue;

            lamp.HangPivot = XMFLOAT3(0.5f * (bmin.x + bmax.x), bmax.y, 0.5f * (bmin.z + bmax.z));
            for (ObjVertex& v : lamp.Vertices)
            {
                v.Pos.x -= lamp.HangPivot.x;
                v.Pos.y -= lamp.HangPivot.y;
                v.Pos.z -= lamp.HangPivot.z;
            }
            const float localMinY = bmin.y - lamp.HangPivot.y;
            const float localMaxY = bmax.y - lamp.HangPivot.y;
            lamp.LightLocal = XMFLOAT3(0.0f, localMinY + 0.12f * (localMaxY - localMinY), 0.0f);
            lamp.Indices.resize(lamp.Vertices.size());
            for (uint16_t i = 0; i < (uint16_t)lamp.Indices.size(); ++i)
                lamp.Indices[i] = i;
            BoundingBox::CreateFromPoints(lamp.LocalBounds,
                XMVectorSet(bmin.x - lamp.HangPivot.x, localMinY, bmin.z - lamp.HangPivot.z, 0.0f),
                XMVectorSet(bmax.x - lamp.HangPivot.x, localMaxY, bmax.z - lamp.HangPivot.z, 0.0f));
            mExtractedChandeliers.push_back(std::move(lamp));
        }
    }

    if (vertices.empty())
    {
        MessageBoxA(nullptr, "Model has no vertices!", "Error", MB_OK);
        return false;
    }

    ComputeTangents(vertices, indices);

    const UINT vbByteSize = static_cast<UINT>(vertices.size()) * sizeof(TessVertex);
    const UINT ibByteSize = static_cast<UINT>(indices.size()) * sizeof(uint32_t);

    mGeometry = std::make_unique<MeshGeometry>();
    mGeometry->Name = "sponza";

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &mGeometry->VertexBufferCPU));
    CopyMemory(mGeometry->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &mGeometry->IndexBufferCPU));
    CopyMemory(mGeometry->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    mGeometry->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(device,
        cmdList, vertices.data(), vbByteSize, mGeometry->VertexBufferUploader);

    mGeometry->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(device,
        cmdList, indices.data(), ibByteSize, mGeometry->IndexBufferUploader);

    mGeometry->VertexByteStride = sizeof(TessVertex);
    mGeometry->VertexBufferByteSize = vbByteSize;
    mGeometry->IndexFormat = DXGI_FORMAT_R32_UINT;
    mGeometry->IndexBufferByteSize = ibByteSize;

    SubmeshGeometry submesh;
    submesh.IndexCount = static_cast<UINT>(indices.size());
    submesh.StartIndexLocation = 0;
    submesh.BaseVertexLocation = 0;
    mGeometry->DrawArgs["sponza"] = submesh;

    BoundingBox local;
    BoundingBox::CreateFromPoints(local, XMLoadFloat3(&boundMin), XMLoadFloat3(&boundMax));
    SetLocalBounds(local);

    return true;
}
