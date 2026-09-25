#include "fullscreen.hlsli"

Texture2D gAlbedo : register(t0);
Texture2D gNormal : register(t1);
Texture2D gWorldPos : register(t2);
Texture2DArray gShadowMaps : register(t3);
SamplerComparisonState gShadowSampler : register(s0);
SamplerState gShadowMapSampler : register(s1);

#define MAX_POINT_LIGHTS 8
#define MAX_SPOT_LIGHTS 4
#define SHADOW_CASCADES 4

cbuffer cbPass : register(b0)
{
    float4 gDirectionalDir;
    float4 gDirectionalColor;
    float4 gAmbientColor;
    float4 gCameraWorld;
    float4 gCameraLook;
    uint gPointCount;
    uint gSpotCount;
    uint gDebugMode;
    uint gPad;
    float4 gPointPosRange[MAX_POINT_LIGHTS];
    float4 gPointColorIntensity[MAX_POINT_LIGHTS];
    float4 gSpotPosRange[MAX_SPOT_LIGHTS];
    float4 gSpotDirCosine[MAX_SPOT_LIGHTS];
    float4 gSpotColorIntensity[MAX_SPOT_LIGHTS];
    float4x4 gCascadeViewProj[SHADOW_CASCADES];
    float4 gCascadeSplits;
    float4 gShadowParams;
};

float3 DecodeNormal(float3 enc)
{
    return normalize(enc * 2.0f - 1.0f);
}

float DistanceAttenuation(float dist, float range)
{
    float f = saturate(1.0f - dist / max(range, 1e-3f));
    return f * f;
}

float3 Lambert(float3 albedo, float3 N, float3 L, float3 radiance)
{
    return albedo * radiance * saturate(dot(N, L));
}

uint PickCascade(float viewZ)
{
    uint cascade = 3;
    if (viewZ < gCascadeSplits.x)
        cascade = 0;
    else if (viewZ < gCascadeSplits.y)
        cascade = 1;
    else if (viewZ < gCascadeSplits.z)
        cascade = 2;
    return cascade;
}

float SampleSunShadow(float3 worldPos)
{
    float texel = gShadowParams.x;
    float zBias = gShadowParams.y;

    [unroll]
    for (uint cascade = 0; cascade < SHADOW_CASCADES; ++cascade)
    {
        float4 ls = mul(float4(worldPos, 1.0f), gCascadeViewProj[cascade]);
        ls.xyz /= max(ls.w, 1e-6f);

        float2 uv = ls.xy * float2(0.5f, -0.5f) + 0.5f;
        float depth = ls.z;
        const float edge = 0.012f;
        if (uv.x < edge || uv.x > 1.0f - edge || uv.y < edge || uv.y > 1.0f - edge ||
            depth < 0.0f || depth > 1.0f)
            continue;

        float compareDepth = saturate(depth - zBias);
        float shadow = 0.0f;
        [unroll]
        for (int y = -1; y <= 1; ++y)
        {
            [unroll]
            for (int x = -1; x <= 1; ++x)
            {
                float2 offset = float2(x, y) * texel;
                shadow += gShadowMaps.SampleCmpLevelZero(gShadowSampler, float3(uv + offset, cascade), compareDepth);
            }
        }
        return shadow / 9.0f;
    }

    return 1.0f;
}

float3 CascadeColor(uint cascade)
{
    if (cascade == 0)
        return float3(1.0f, 0.0f, 0.0f);
    if (cascade == 1)
        return float3(0.0f, 1.0f, 0.0f);
    if (cascade == 2)
        return float3(1.0f, 1.0f, 0.0f);
    return float3(0.0f, 0.0f, 1.0f);
}

float4 PS(FSOut pin) : SV_Target
{
    int2 pixel = int2(pin.PosH.xy);

    float4 worldS = gWorldPos.Load(int3(pixel, 0));
    if (worldS.a < 0.5f)
        return float4(0.0f, 0.0f, 0.0f, 1.0f);

    float3 albedo = gAlbedo.Load(int3(pixel, 0)).rgb;
    float3 N = DecodeNormal(gNormal.Load(int3(pixel, 0)).xyz);
    float3 worldPos = worldS.xyz;
    float viewZ = dot(worldPos - gCameraWorld.xyz, normalize(gCameraLook.xyz));
    uint cascade = PickCascade(viewZ);

    float3 dirL = normalize(-gDirectionalDir.xyz);
    float3 dirRadiance = gDirectionalColor.rgb * gDirectionalColor.a;
    float shadow = 1.0f;
    if (gDebugMode != 1)
    {
        float3 samplePos = worldPos + N * gShadowParams.w + dirL * gShadowParams.w;
        shadow = saturate(SampleSunShadow(samplePos));
    }

    float3 Lo = albedo * gAmbientColor.rgb;
    Lo += Lambert(albedo, N, dirL, dirRadiance) * shadow;

    uint pointCount = min(gPointCount, MAX_POINT_LIGHTS);
    for (uint i = 0; i < pointCount; ++i)
    {
        float3 toLight = gPointPosRange[i].xyz - worldPos;
        float dist = length(toLight);
        float3 L = toLight / max(dist, 1e-5f);
        float atten = DistanceAttenuation(dist, gPointPosRange[i].w);
        float3 radiance = gPointColorIntensity[i].rgb * gPointColorIntensity[i].a * atten;
        Lo += Lambert(albedo, N, L, radiance);
    }

    if (gDebugMode == 2)
        Lo = lerp(saturate(Lo + albedo * 0.3f), CascadeColor(cascade), 0.5f);

    return float4(Lo, 1.0f);
}
