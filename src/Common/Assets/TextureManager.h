#pragma once

#include "Texture.h"
#include <unordered_map>
#include <memory>

class TextureManager
{
public:
    TextureManager();
    ~TextureManager();

    bool Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);
    void Shutdown();

    std::shared_ptr<Texture> LoadTexture(const std::string& filename);
    std::shared_ptr<Texture> GetTexture(const std::string& filename);
    std::shared_ptr<Texture> GetWhiteTexture();
    void ReleaseUploads();

private:
    ID3D12Device* mDevice;
    ID3D12GraphicsCommandList* mCmdList;
    std::unordered_map<std::string, std::shared_ptr<Texture>> mTextures;
    std::shared_ptr<Texture> mWhiteTexture;
};