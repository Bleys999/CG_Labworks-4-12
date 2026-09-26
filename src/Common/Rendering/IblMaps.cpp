#include "IblMaps.h"
#include <cmath>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdint>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace
{
    constexpr float kPi = 3.14159265359f;
    constexpr UINT kIrrSize = 32;
    constexpr UINT kPrefSize = 64;
    constexpr UINT kPrefMips = 5;
    constexpr UINT kLutSize = 128;
    constexpr UINT kIrrSamples = 48;
    constexpr UINT kPrefSamples = 32;
    constexpr UINT kLutSamples = 64;

    float Saturate(float x)
    {
        return (std::max)(0.0f, (std::min)(1.0f, x));
    }

    void FaceDir(int face, float u, float v, XMFLOAT3& d)
    {
        const float uc = 2.0f * u - 1.0f;
        const float vc = 2.0f * v - 1.0f;
        switch (face)
        {
        case 0: d = XMFLOAT3(1.0f, -vc, -uc); break;
        case 1: d = XMFLOAT3(-1.0f, -vc, uc); break;
        case 2: d = XMFLOAT3(uc, 1.0f, vc); break;
        case 3: d = XMFLOAT3(uc, -1.0f, -vc); break;
        case 4: d = XMFLOAT3(uc, -vc, 1.0f); break;
        default: d = XMFLOAT3(-uc, -vc, -1.0f); break;
        }
        XMVECTOR n = XMVector3Normalize(XMLoadFloat3(&d));
        XMStoreFloat3(&d, n);
    }

    XMFLOAT3 SampleEnv(const XMFLOAT3& dir)
    {
        const float y = dir.y;
        const XMFLOAT3 zenith(0.18f, 0.32f, 0.72f);
        const XMFLOAT3 horizon(0.78f, 0.82f, 0.90f);
        const XMFLOAT3 ground(0.10f, 0.09f, 0.07f);
        XMFLOAT3 col;
        if (y >= 0.0f)
        {
            const float t = y * y;
            col.x = horizon.x + (zenith.x - horizon.x) * t;
            col.y = horizon.y + (zenith.y - horizon.y) * t;
            col.z = horizon.z + (zenith.z - horizon.z) * t;
        }
        else
        {
            const float t = Saturate(-y);
            col.x = horizon.x + (ground.x - horizon.x) * t;
            col.y = horizon.y + (ground.y - horizon.y) * t;
            col.z = horizon.z + (ground.z - horizon.z) * t;
        }

        XMFLOAT3 sunDir(-0.42f, 0.86f, -0.22f);
        XMVECTOR sd = XMVector3Normalize(XMLoadFloat3(&sunDir));
        XMVECTOR dd = XMLoadFloat3(&dir);
        float sun = powf(Saturate(XMVectorGetX(XMVector3Dot(sd, dd))), 220.0f);
        col.x += sun * 18.0f;
        col.y += sun * 16.0f;
        col.z += sun * 12.0f;
        return col;
    }

    float RadicalInverse(uint32_t bits)
    {
        bits = (bits << 16u) | (bits >> 16u);
        bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
        bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
        bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
        bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
        return (float)bits * 2.3283064365386963e-10f;
    }

    void Hammersley(uint32_t i, uint32_t n, float& u, float& v)
    {
        u = (float)i / (float)n;
        v = RadicalInverse(i);
    }

    XMVECTOR TangentToWorld(XMVECTOR n, float x, float y, float z)
    {
        XMVECTOR up = fabsf(XMVectorGetZ(n)) < 0.999f
            ? XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f)
            : XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
        XMVECTOR t = XMVector3Normalize(XMVector3Cross(up, n));
        XMVECTOR b = XMVector3Cross(n, t);
        return XMVector3Normalize(t * x + b * y + n * z);
    }

    XMVECTOR ImportanceSampleGGX(float u1, float u2, float roughness, XMVECTOR n)
    {
        const float a = roughness * roughness;
        const float phi = 2.0f * kPi * u1;
        const float cosTheta = sqrtf((1.0f - u2) / (1.0f + (a * a - 1.0f) * u2));
        const float sinTheta = sqrtf((std::max)(0.0f, 1.0f - cosTheta * cosTheta));
        return TangentToWorld(n, sinTheta * cosf(phi), sinTheta * sinf(phi), cosTheta);
    }

    float GeometrySchlickGGX(float nDotV, float roughness)
    {
        const float k = (roughness * roughness) / 2.0f;
        return nDotV / (nDotV * (1.0f - k) + k);
    }

    float GeometrySmith(float nDotV, float nDotL, float roughness)
    {
        return GeometrySchlickGGX(nDotV, roughness) * GeometrySchlickGGX(nDotL, roughness);
    }

    bool TryLoad(const wchar_t* path, UINT expectW, UINT expectH, UINT expectFaces, UINT expectMips, std::vector<float>& out)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return false;
        char magic[4] = {};
        UINT w = 0, h = 0, faces = 0, mips = 0;
        in.read(magic, 4);
        in.read(reinterpret_cast<char*>(&w), 4);
        in.read(reinterpret_cast<char*>(&h), 4);
        in.read(reinterpret_cast<char*>(&faces), 4);
        in.read(reinterpret_cast<char*>(&mips), 4);
        if (!in || memcmp(magic, "IBL1", 4) != 0 || w != expectW || h != expectH || faces != expectFaces || mips != expectMips)
            return false;
        size_t count = 0;
        for (UINT m = 0; m < mips; ++m)
        {
            const UINT s = (std::max)(1u, w >> m);
            const UINT t = (std::max)(1u, h >> m);
            count += (size_t)s * t * faces * 4;
        }
        out.resize(count);
        in.read(reinterpret_cast<char*>(out.data()), count * sizeof(float));
        return (bool)in;
    }

    void TrySave(const wchar_t* path, UINT w, UINT h, UINT faces, UINT mips, const std::vector<float>& data)
    {
        CreateDirectoryW(L"..\\Ibl", nullptr);
        CreateDirectoryW(L"Ibl", nullptr);
        std::ofstream out(path, std::ios::binary);
        if (!out)
            return;
        out.write("IBL1", 4);
        out.write(reinterpret_cast<const char*>(&w), 4);
        out.write(reinterpret_cast<const char*>(&h), 4);
        out.write(reinterpret_cast<const char*>(&faces), 4);
        out.write(reinterpret_cast<const char*>(&mips), 4);
        out.write(reinterpret_cast<const char*>(data.data()), data.size() * sizeof(float));
    }

    bool LoadOrEmpty(const wchar_t* a, const wchar_t* b, UINT w, UINT h, UINT faces, UINT mips, std::vector<float>& out)
    {
        if (TryLoad(a, w, h, faces, mips, out))
            return true;
        return TryLoad(b, w, h, faces, mips, out);
    }

#pragma pack(push, 1)
    struct DdsPixelFormat
    {
        UINT size;
        UINT flags;
        UINT fourCC;
        UINT rgbBitCount;
        UINT rMask;
        UINT gMask;
        UINT bMask;
        UINT aMask;
    };
    struct DdsHeader
    {
        UINT size;
        UINT flags;
        UINT height;
        UINT width;
        UINT pitch;
        UINT depth;
        UINT mipMapCount;
        UINT reserved1[11];
        DdsPixelFormat ddspf;
        UINT caps;
        UINT caps2;
        UINT caps3;
        UINT caps4;
        UINT reserved2;
    };
    struct DdsHeaderDx10
    {
        DXGI_FORMAT dxgiFormat;
        UINT resourceDimension;
        UINT miscFlag;
        UINT arraySize;
        UINT miscFlags2;
    };
#pragma pack(pop)

    DXGI_FORMAT TypedFormat(DXGI_FORMAT format)
    {
        if (format == DXGI_FORMAT_BC6H_TYPELESS)
            return DXGI_FORMAT_BC6H_UF16;
        return format;
    }

    void GetSurfaceInfo(UINT width, UINT height, DXGI_FORMAT format, UINT64& numBytes, UINT& rowBytes, UINT& numRows)
    {
        const bool bc = format == DXGI_FORMAT_BC6H_UF16 || format == DXGI_FORMAT_BC6H_SF16 || format == DXGI_FORMAT_BC6H_TYPELESS;
        if (bc)
        {
            const UINT nbw = (std::max)(1u, (width + 3u) / 4u);
            const UINT nbh = (std::max)(1u, (height + 3u) / 4u);
            rowBytes = nbw * 16u;
            numRows = nbh;
            numBytes = (UINT64)rowBytes * numRows;
            return;
        }
        UINT bpp = 32;
        if (format == DXGI_FORMAT_R32G32_FLOAT || format == DXGI_FORMAT_R16G16B16A16_FLOAT)
            bpp = 64;
        else if (format == DXGI_FORMAT_R32G32B32A32_FLOAT)
            bpp = 128;
        else if (format == DXGI_FORMAT_R8G8B8A8_UNORM)
            bpp = 32;
        rowBytes = (width * bpp + 7u) / 8u;
        numRows = height;
        numBytes = (UINT64)rowBytes * numRows;
    }

    bool ReadAllBytes(const wchar_t* a, const wchar_t* b, std::vector<uint8_t>& out)
    {
        const wchar_t* paths[2] = { a, b };
        for (int i = 0; i < 2; ++i)
        {
            std::ifstream in(paths[i], std::ios::binary | std::ios::ate);
            if (!in)
                continue;
            const std::streamoff n = in.tellg();
            if (n < 128)
                continue;
            out.resize((size_t)n);
            in.seekg(0);
            in.read(reinterpret_cast<char*>(out.data()), n);
            if (in)
                return true;
            out.clear();
        }
        return false;
    }

    bool CreateTextureFromDds(
        ID3D12Device* device,
        ID3D12GraphicsCommandList* cmdList,
        const std::vector<uint8_t>& file,
        bool wantCube,
        ComPtr<ID3D12Resource>& texture,
        ComPtr<ID3D12Resource>& upload)
    {
        if (file.size() < 4 + sizeof(DdsHeader))
            return false;
        UINT magic = 0;
        memcpy(&magic, file.data(), 4);
        if (magic != 0x20534444)
            return false;
        DdsHeader hdr{};
        memcpy(&hdr, file.data() + 4, sizeof(DdsHeader));
        size_t offset = 4 + sizeof(DdsHeader);
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        UINT arraySize = 1;
        bool isCube = (hdr.caps2 & 0x200) != 0;
        const UINT mips = hdr.mipMapCount ? hdr.mipMapCount : 1;
        const UINT width = hdr.width;
        const UINT height = hdr.height;
        if ((hdr.ddspf.flags & 0x4) && hdr.ddspf.fourCC == 0x30315844)
        {
            if (file.size() < offset + sizeof(DdsHeaderDx10))
                return false;
            DdsHeaderDx10 dx10{};
            memcpy(&dx10, file.data() + offset, sizeof(DdsHeaderDx10));
            offset += sizeof(DdsHeaderDx10);
            format = dx10.dxgiFormat;
            arraySize = dx10.arraySize ? dx10.arraySize : 1;
            if (dx10.miscFlag & 0x4)
                isCube = true;
        }
        else
            return false;
        if (wantCube != isCube)
            return false;
        format = TypedFormat(format);
        const UINT faces = isCube ? 6u * arraySize : arraySize;
        if (wantCube && faces != 6)
            return false;
        if (width == 0 || height == 0 || mips == 0 || faces == 0)
            return false;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = (UINT16)faces;
        desc.MipLevels = (UINT16)mips;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;
        ThrowIfFailed(device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&texture)));

        const UINT nsub = mips * faces;
        const UINT64 uploadSize = GetRequiredIntermediateSize(texture.Get(), 0, nsub);
        ThrowIfFailed(device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(uploadSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&upload)));

        std::vector<D3D12_SUBRESOURCE_DATA> subs(nsub);
        const uint8_t* ptr = file.data() + offset;
        const uint8_t* end = file.data() + file.size();
        for (UINT face = 0; face < faces; ++face)
        {
            UINT mw = width;
            UINT mh = height;
            for (UINT mip = 0; mip < mips; ++mip)
            {
                UINT64 numBytes = 0;
                UINT rowBytes = 0;
                UINT numRows = 0;
                GetSurfaceInfo(mw, mh, format, numBytes, rowBytes, numRows);
                if (ptr + numBytes > end)
                    return false;
                const UINT sub = mip + face * mips;
                subs[sub].pData = ptr;
                subs[sub].RowPitch = rowBytes;
                subs[sub].SlicePitch = (LONG)numBytes;
                ptr += numBytes;
                mw = (std::max)(1u, mw >> 1);
                mh = (std::max)(1u, mh >> 1);
            }
        }

        UpdateSubresources(cmdList, texture.Get(), upload.Get(), 0, 0, nsub, subs.data());
        cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
        return true;
    }

    void BakeIrradiance(std::vector<float>& out)
    {
        out.assign((size_t)kIrrSize * kIrrSize * 6 * 4, 0.0f);
        for (int face = 0; face < 6; ++face)
        {
            for (UINT y = 0; y < kIrrSize; ++y)
            {
                for (UINT x = 0; x < kIrrSize; ++x)
                {
                    XMFLOAT3 n;
                    FaceDir(face, (x + 0.5f) / kIrrSize, (y + 0.5f) / kIrrSize, n);
                    XMVECTOR N = XMLoadFloat3(&n);
                    XMFLOAT3 acc(0, 0, 0);
                    float wsum = 0.0f;
                    for (uint32_t i = 0; i < kIrrSamples; ++i)
                    {
                        float u, v;
                        Hammersley(i, kIrrSamples, u, v);
                        const float phi = 2.0f * kPi * u;
                        const float cosTheta = 1.0f - v;
                        const float sinTheta = sqrtf((std::max)(0.0f, 1.0f - cosTheta * cosTheta));
                        XMVECTOR L = TangentToWorld(N, sinTheta * cosf(phi), sinTheta * sinf(phi), cosTheta);
                        float ndotl = Saturate(XMVectorGetX(XMVector3Dot(N, L)));
                        if (ndotl <= 0.0f)
                            continue;
                        XMFLOAT3 ld;
                        XMStoreFloat3(&ld, L);
                        XMFLOAT3 e = SampleEnv(ld);
                        acc.x += e.x * ndotl;
                        acc.y += e.y * ndotl;
                        acc.z += e.z * ndotl;
                        wsum += ndotl;
                    }
                    if (wsum > 1e-5f)
                    {
                        acc.x /= wsum;
                        acc.y /= wsum;
                        acc.z /= wsum;
                    }
                    const size_t idx = ((size_t)face * kIrrSize * kIrrSize + (size_t)y * kIrrSize + x) * 4;
                    out[idx + 0] = acc.x;
                    out[idx + 1] = acc.y;
                    out[idx + 2] = acc.z;
                    out[idx + 3] = 1.0f;
                }
            }
        }
    }

    UINT MipSize(UINT base, UINT mip)
    {
        UINT s = base >> mip;
        return s > 0 ? s : 1;
    }

    void BakePrefilter(std::vector<float>& out)
    {
        size_t total = 0;
        for (UINT m = 0; m < kPrefMips; ++m)
        {
            const UINT s = MipSize(kPrefSize, m);
            total += (size_t)s * s * 6 * 4;
        }
        out.assign(total, 0.0f);
        size_t cursor = 0;
        for (UINT mip = 0; mip < kPrefMips; ++mip)
        {
            const UINT s = MipSize(kPrefSize, mip);
            const float roughness = (kPrefMips == 1) ? 0.0f : (float)mip / (float)(kPrefMips - 1);
            for (int face = 0; face < 6; ++face)
            {
                for (UINT y = 0; y < s; ++y)
                {
                    for (UINT x = 0; x < s; ++x)
                    {
                        XMFLOAT3 r;
                        FaceDir(face, (x + 0.5f) / s, (y + 0.5f) / s, r);
                        XMVECTOR R = XMLoadFloat3(&r);
                        XMVECTOR N = R;
                        XMFLOAT3 acc(0, 0, 0);
                        float wsum = 0.0f;
                        if (roughness < 0.02f)
                        {
                            acc = SampleEnv(r);
                            wsum = 1.0f;
                        }
                        else
                        {
                            for (uint32_t i = 0; i < kPrefSamples; ++i)
                            {
                                float u, v;
                                Hammersley(i, kPrefSamples, u, v);
                                XMVECTOR H = ImportanceSampleGGX(u, v, roughness, N);
                                XMVECTOR L = XMVector3Normalize(2.0f * XMVector3Dot(R, H) * H - R);
                                float nDotL = Saturate(XMVectorGetX(XMVector3Dot(N, L)));
                                if (nDotL <= 0.0f)
                                    continue;
                                XMFLOAT3 ld;
                                XMStoreFloat3(&ld, L);
                                XMFLOAT3 e = SampleEnv(ld);
                                acc.x += e.x * nDotL;
                                acc.y += e.y * nDotL;
                                acc.z += e.z * nDotL;
                                wsum += nDotL;
                            }
                        }
                        if (wsum > 1e-5f)
                        {
                            acc.x /= wsum;
                            acc.y /= wsum;
                            acc.z /= wsum;
                        }
                        const size_t idx = cursor + ((size_t)face * s * s + (size_t)y * s + x) * 4;
                        out[idx + 0] = acc.x;
                        out[idx + 1] = acc.y;
                        out[idx + 2] = acc.z;
                        out[idx + 3] = 1.0f;
                    }
                }
            }
            cursor += (size_t)s * s * 6 * 4;
        }
    }

    void BakeBrdf(std::vector<float>& out)
    {
        out.assign((size_t)kLutSize * kLutSize * 4, 0.0f);
        for (UINT y = 0; y < kLutSize; ++y)
        {
            const float roughness = (y + 0.5f) / kLutSize;
            for (UINT x = 0; x < kLutSize; ++x)
            {
                const float nDotV = (x + 0.5f) / kLutSize;
                XMVECTOR V = XMVectorSet(sqrtf((std::max)(0.0f, 1.0f - nDotV * nDotV)), 0.0f, nDotV, 0.0f);
                XMVECTOR N = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
                float a = 0.0f;
                float b = 0.0f;
                for (uint32_t i = 0; i < kLutSamples; ++i)
                {
                    float u, v;
                    Hammersley(i, kLutSamples, u, v);
                    XMVECTOR H = ImportanceSampleGGX(u, v, roughness, N);
                    XMVECTOR L = XMVector3Normalize(2.0f * XMVector3Dot(V, H) * H - V);
                    float nDotL = Saturate(XMVectorGetZ(L));
                    float nDotH = Saturate(XMVectorGetZ(H));
                    float vDotH = Saturate(XMVectorGetX(XMVector3Dot(V, H)));
                    if (nDotL <= 0.0f)
                        continue;
                    float g = GeometrySmith(nDotV, nDotL, roughness);
                    float gVis = (g * vDotH) / (std::max)(nDotH * nDotV, 1e-5f);
                    float fc = powf(1.0f - vDotH, 5.0f);
                    a += (1.0f - fc) * gVis;
                    b += fc * gVis;
                }
                a /= (float)kLutSamples;
                b /= (float)kLutSamples;
                const size_t idx = ((size_t)y * kLutSize + x) * 4;
                out[idx + 0] = a;
                out[idx + 1] = b;
                out[idx + 2] = 0.0f;
                out[idx + 3] = 1.0f;
            }
        }
    }
}

bool IblMaps::CreateCube(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList,
    UINT size, UINT mips, const float* rgba,
    ComPtr<ID3D12Resource>& texture,
    ComPtr<ID3D12Resource>& upload)
{
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = size;
    desc.Height = size;
    desc.DepthOrArraySize = 6;
    desc.MipLevels = (UINT16)mips;
    desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ThrowIfFailed(device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&texture)));

    const UINT nsub = 6 * mips;
    const UINT64 uploadSize = GetRequiredIntermediateSize(texture.Get(), 0, nsub);
    ThrowIfFailed(device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(uploadSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&upload)));

    std::vector<D3D12_SUBRESOURCE_DATA> subs(nsub);
    const float* src = rgba;
    for (UINT mip = 0; mip < mips; ++mip)
    {
        const UINT s = (std::max)(1u, size >> mip);
        const LONG row = (LONG)s * 16;
        const LONG slice = row * (LONG)s;
        for (UINT face = 0; face < 6; ++face)
        {
            const UINT sub = mip + face * mips;
            subs[sub].pData = src;
            subs[sub].RowPitch = row;
            subs[sub].SlicePitch = slice;
            src += (size_t)s * s * 4;
        }
    }

    UpdateSubresources(cmdList, texture.Get(), upload.Get(), 0, 0, nsub, subs.data());
    cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
    return true;
}

bool IblMaps::CreateLut(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList,
    UINT size, const float* rgba,
    ComPtr<ID3D12Resource>& texture,
    ComPtr<ID3D12Resource>& upload)
{
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = size;
    desc.Height = size;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    ThrowIfFailed(device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&texture)));

    const UINT64 uploadSize = GetRequiredIntermediateSize(texture.Get(), 0, 1);
    ThrowIfFailed(device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(uploadSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&upload)));

    D3D12_SUBRESOURCE_DATA sub = {};
    sub.pData = rgba;
    sub.RowPitch = (LONG)size * 16;
    sub.SlicePitch = sub.RowPitch * size;
    UpdateSubresources(cmdList, texture.Get(), upload.Get(), 0, 0, 1, &sub);
    cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
    return true;
}

bool IblMaps::Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList)
{
    mPrefilterMips = kPrefMips;

    std::vector<uint8_t> dds;
    const bool irrDds = ReadAllBytes(L"..\\Ibl\\IrradianceMap_BC6U.dds", L"Ibl\\IrradianceMap_BC6U.dds", dds)
        && CreateTextureFromDds(device, cmdList, dds, true, mIrradiance, mIrradianceUpload);
    if (!irrDds)
    {
        std::vector<float> irr;
        if (!LoadOrEmpty(L"..\\Ibl\\irradiance.ibl", L"Ibl\\irradiance.ibl", kIrrSize, kIrrSize, 6, 1, irr))
        {
            BakeIrradiance(irr);
            TrySave(L"..\\Ibl\\irradiance.ibl", kIrrSize, kIrrSize, 6, 1, irr);
            TrySave(L"Ibl\\irradiance.ibl", kIrrSize, kIrrSize, 6, 1, irr);
        }
        if (!CreateCube(device, cmdList, kIrrSize, 1, irr.data(), mIrradiance, mIrradianceUpload))
            return false;
    }

    dds.clear();
    const bool prefDds = ReadAllBytes(L"..\\Ibl\\PreFilteredEnvMap_BC6U.dds", L"Ibl\\PreFilteredEnvMap_BC6U.dds", dds)
        && CreateTextureFromDds(device, cmdList, dds, true, mPrefilter, mPrefilterUpload);
    if (!prefDds)
    {
        std::vector<float> pref;
        if (!LoadOrEmpty(L"..\\Ibl\\prefiltered.ibl", L"Ibl\\prefiltered.ibl", kPrefSize, kPrefSize, 6, kPrefMips, pref))
        {
            BakePrefilter(pref);
            TrySave(L"..\\Ibl\\prefiltered.ibl", kPrefSize, kPrefSize, 6, kPrefMips, pref);
            TrySave(L"Ibl\\prefiltered.ibl", kPrefSize, kPrefSize, 6, kPrefMips, pref);
        }
        if (!CreateCube(device, cmdList, kPrefSize, kPrefMips, pref.data(), mPrefilter, mPrefilterUpload))
            return false;
    }

    dds.clear();
    const bool lutDds = ReadAllBytes(L"..\\Ibl\\IntegrationMap.dds", L"Ibl\\IntegrationMap.dds", dds)
        && CreateTextureFromDds(device, cmdList, dds, false, mBrdfLut, mBrdfUpload);
    if (!lutDds)
    {
        std::vector<float> lut;
        if (!LoadOrEmpty(L"..\\Ibl\\brdf.ibl", L"Ibl\\brdf.ibl", kLutSize, kLutSize, 1, 1, lut))
        {
            BakeBrdf(lut);
            TrySave(L"..\\Ibl\\brdf.ibl", kLutSize, kLutSize, 1, 1, lut);
            TrySave(L"Ibl\\brdf.ibl", kLutSize, kLutSize, 1, 1, lut);
        }
        if (!CreateLut(device, cmdList, kLutSize, lut.data(), mBrdfLut, mBrdfUpload))
            return false;
    }

    if (!mIrradiance || !mPrefilter || !mBrdfLut)
        return false;
    mPrefilterMips = (UINT)mPrefilter->GetDesc().MipLevels;
    if (mPrefilterMips == 0)
        mPrefilterMips = 1;
    return true;
}
