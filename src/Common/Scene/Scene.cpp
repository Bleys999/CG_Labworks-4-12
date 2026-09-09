#include "Scene.h"
#include "../../Main/Objects/Box/BoxObject.h"
#include "../../Main/Objects/Sponza/SponzaObject.h"
#include "../Assets/TextureManager.h"
#include "../Core/Config.h"
#include <algorithm>
#include <random>
#include <string>

using namespace DirectX;

Scene::Scene()
    : mTextureManager(nullptr)
{
}

bool Scene::Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, TextureManager* textureManager)
{
    mTextureManager = textureManager;

    auto sponza = std::make_unique<SponzaObject>("Sponza");
    sponza->SetPosition(0.0f, 0.0f, 0.0f);
    sponza->SetRotation(0.0f, 0.0f, 0.0f);
    sponza->SetScale(1.0f, 1.0f, 1.0f);
    sponza->SetCullable(false);
    if (!sponza->Initialize(device, cmdList, textureManager))
        return false;
    AddObject(std::move(sponza));

    if (!SpawnScatteredBoxes(device, cmdList, textureManager))
        return false;

    BuildOctree();
    return true;
}

bool Scene::SpawnScatteredBoxes(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, TextureManager* textureManager)
{
    std::mt19937 rng(4);
    std::uniform_real_distribution<float> distX(-22.0f, 22.0f);
    std::uniform_real_distribution<float> distY(0.8f, 10.0f);
    std::uniform_real_distribution<float> distZ(-18.0f, 18.0f);
    std::uniform_real_distribution<float> distScale(0.18f, 0.45f);
    std::uniform_real_distribution<float> distColor(0.15f, 1.0f);
    std::uniform_real_distribution<float> distYaw(0.0f, 6.2831853f);

    mCullableCount = 0;

    for (int i = 0; i < Config::ScatteredObjectCount; ++i)
    {
        auto box = std::make_unique<BoxObject>("Box" + std::to_string(i));
        box->SetPosition(distX(rng), distY(rng), distZ(rng));
        box->SetRotation(0.0f, distYaw(rng), 0.0f);
        const float s = distScale(rng);
        box->SetScale(s, s, s);
        box->SetDiffuseFactor(XMFLOAT4(distColor(rng), distColor(rng), distColor(rng), 1.0f));
        box->SetCullable(true);

        if (!box->Initialize(device, cmdList, textureManager))
            return false;

        AddObject(std::move(box));
        ++mCullableCount;
    }

    return true;
}

void Scene::BuildOctree()
{
    std::vector<GameObject*> cullable;
    cullable.reserve(mGameObjects.size());
    for (auto& gameObject : mGameObjects)
    {
        if (gameObject->IsCullable())
            cullable.push_back(gameObject.get());
    }
    mOctree.Build(cullable);
}

void Scene::Update(const GameTimer& gt, const Camera& camera)
{
    XMMATRIX view = camera.GetView();
    XMMATRIX proj = camera.GetProj();

    for (auto& gameObject : mGameObjects)
    {
        gameObject->SetFrameCamera(camera.GetPosition3f());
        gameObject->Update(gt);
        gameObject->UpdateConstantBuffer(view, proj);
    }
}

bool Scene::ShouldDrawObject(GameObject* object, const BoundingFrustum& frustum) const
{
    if (!object->IsVisible())
        return false;
    if (!object->IsCullable())
        return true;
    if (mOctreeCulling)
        return false;
    if (!mFrustumCulling)
        return true;
    return frustum.Contains(object->GetWorldBounds()) != DISJOINT;
}

void Scene::Draw(ID3D12GraphicsCommandList* cmdList, const Camera& camera)
{
    BoundingFrustum frustum = camera.GetWorldFrustum();
    mDrawnCount = 0;

    if (mOctreeCulling)
    {
        for (auto& gameObject : mGameObjects)
        {
            if (gameObject->UsesTessellation())
                continue;
            if (!gameObject->IsCullable() && gameObject->IsVisible())
            {
                gameObject->Draw(cmdList);
                ++mDrawnCount;
            }
        }

        std::vector<GameObject*> visible;
        mOctree.Query(frustum, visible);
        for (GameObject* object : visible)
        {
            if (!object->IsVisible() || object->UsesTessellation())
                continue;
            object->Draw(cmdList);
            ++mDrawnCount;
        }
        return;
    }

    for (auto& gameObject : mGameObjects)
    {
        if (gameObject->UsesTessellation())
            continue;
        if (!ShouldDrawObject(gameObject.get(), frustum))
            continue;
        gameObject->Draw(cmdList);
        ++mDrawnCount;
    }
}

void Scene::DrawTessellated(ID3D12GraphicsCommandList* cmdList, const Camera& camera)
{
    BoundingFrustum frustum = camera.GetWorldFrustum();

    for (auto& gameObject : mGameObjects)
    {
        if (!gameObject->UsesTessellation() || !gameObject->IsVisible())
            continue;
        if (gameObject->IsCullable() && (mFrustumCulling || mOctreeCulling))
        {
            if (frustum.Contains(gameObject->GetWorldBounds()) == DISJOINT)
                continue;
        }
        gameObject->Draw(cmdList);
        ++mDrawnCount;
    }
}

void Scene::DrawShadow(ID3D12GraphicsCommandList* cmdList)
{
    for (auto& gameObject : mGameObjects)
    {
        if (!gameObject->IsVisible() || gameObject->UsesTessellation())
            continue;
        gameObject->DrawShadow(cmdList);
    }
}

void Scene::ToggleFrustumCulling()
{
    mFrustumCulling = !mFrustumCulling;
}

void Scene::ToggleOctreeCulling()
{
    mOctreeCulling = !mOctreeCulling;
}

void Scene::AddObject(std::unique_ptr<GameObject> object)
{
    if (object)
    {
        mGameObjects.push_back(std::move(object));
    }
}

void Scene::RemoveObject(const std::string& name)
{
    auto it = std::find_if(mGameObjects.begin(), mGameObjects.end(),
        [&name](const std::unique_ptr<GameObject>& obj) {
            return obj->GetName() == name;
        });

    if (it != mGameObjects.end())
    {
        mGameObjects.erase(it);
    }
}

GameObject* Scene::GetObject(const std::string& name)
{
    auto it = std::find_if(mGameObjects.begin(), mGameObjects.end(),
        [&name](const std::unique_ptr<GameObject>& obj) {
            return obj->GetName() == name;
        });

    if (it != mGameObjects.end())
    {
        return it->get();
    }
    return nullptr;
}
