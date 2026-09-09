#pragma once

struct Config
{
    static constexpr float CameraSpeed = 50.0f;
    static constexpr float CameraSensitivity = 0.2f;
    static constexpr float CameraFOV = 0.25f * 3.14159f;
    static constexpr float CameraNearZ = 1.0f;
    static constexpr float CameraFarZ = 1000.0f;
    static constexpr float ClearColorR = 0.48f;
    static constexpr float ClearColorG = 0.52f;
    static constexpr float ClearColorB = 0.80f;
    static constexpr float ClearColorA = 1.0f;

    static constexpr float TextureTileU = 3.0f;
    static constexpr float TextureTileV = 3.0f;
    static constexpr float TextureScrollSpeedU = 0.04f;
    static constexpr float TextureScrollSpeedV = 0.03f;

    static constexpr int ScatteredObjectCount = 200;
    static constexpr int OctreeMaxDepth = 6;
    static constexpr int OctreeMaxLeafObjects = 4;

    static constexpr float TessMin = 1.0f;
    static constexpr float TessMax = 8.0f;
    static constexpr float TessNear = 5.0f;
    static constexpr float TessFar = 40.0f;
    static constexpr float DisplacementScale = 0.06f;

    static constexpr int ShadowCascadeCount = 4;
    static constexpr UINT ShadowMapSize = 2048;
    static constexpr float ShadowDistance = 90.0f;
    static constexpr float ShadowSplitLambda = 0.8f;
    static constexpr float ShadowNearPad = 50.0f;
};