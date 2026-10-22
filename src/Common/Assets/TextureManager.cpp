#include "TextureManager.h"
#include <cstdio>

TextureManager::TextureManager()
    : mDevice(nullptr)
    , mCmdList(nullptr)
{
}

TextureManager::~TextureManager()
{
    Shutdown();
}

bool TextureManager::Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList)
{
    mDevice = device;
    mCmdList = cmdList;
    return true;
}

void TextureManager::Shutdown()
{
    mTextures.clear();
    mWhiteTexture.reset();
    mDevice = nullptr;
    mCmdList = nullptr;
}

std::shared_ptr<Texture> TextureManager::LoadTexture(const std::string& filename)
{
    char debugMsg[512];
    sprintf_s(debugMsg, "Loading texture: %s\n", filename.c_str());
    OutputDebugStringA(debugMsg);

    auto it = mTextures.find(filename);
    if (it != mTextures.end())
    {
        OutputDebugStringA("Texture already loaded, returning cached\n");
        return it->second;
    }

    auto texture = std::make_shared<Texture>();
    if (!texture->Load(mDevice, mCmdList, filename))
    {
        OutputDebugStringA("FAILED to load texture!\n");
        return nullptr;
    }

    OutputDebugStringA("Texture loaded successfully!\n");
    mTextures[filename] = texture;
    return texture;
}

std::shared_ptr<Texture> TextureManager::GetTexture(const std::string& filename)
{
    auto it = mTextures.find(filename);
    if (it != mTextures.end())
    {
        return it->second;
    }
    return nullptr;
}

void TextureManager::ReleaseUploads()
{
    for (auto& entry : mTextures)
    {
        if (entry.second)
            entry.second->ReleaseUpload();
    }
    if (mWhiteTexture)
        mWhiteTexture->ReleaseUpload();
}

std::shared_ptr<Texture> TextureManager::GetWhiteTexture()
{
    if (mWhiteTexture)
        return mWhiteTexture;
    auto texture = std::make_shared<Texture>();
    if (!texture->Create1x1RGBA8(mDevice, mCmdList, 255, 255, 255, 255))
        return nullptr;
    mWhiteTexture = texture;
    return mWhiteTexture;
}