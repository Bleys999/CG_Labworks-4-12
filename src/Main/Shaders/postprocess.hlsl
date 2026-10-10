#include "fullscreen.hlsli"

Texture2D gScene : register(t0);
Texture2D gAlbedo : register(t1);
Texture2D gNormal : register(t2);
Texture2D gDepth : register(t3);
SamplerState gLinearClamp : register(s0);
SamplerState gPointClamp : register(s1);

cbuffer cbPost : register(b0)
{
    float3 gCameraWorld;
    float gFogStart;
    float3 gFogColor;
    float gFogEnd;
    float gBloomThreshold;
    float gBloomIntensity;
    float2 gInvScreenSize;
    float gFogStrength;
    float gPostEnable;
    float gEdgeStrength;
    float gPad;
    float4x4 gInvViewProj;
};

float Luma(float3 c)
{
    return dot(c, float3(0.2126f, 0.7152f, 0.0722f));
}

float3 ApplyBloom(float3 color, float2 uv)
{
    if (gBloomIntensity <= 0.0f)
        return color;

    float2 texel = gInvScreenSize;
    float3 acc = 0.0f;
    float wsum = 0.0f;

    [unroll]
    for (int y = -2; y <= 2; ++y)
    {
        [unroll]
        for (int x = -2; x <= 2; ++x)
        {
            float2 o = float2(x, y) * texel * 1.6f;
            float3 s = gScene.SampleLevel(gLinearClamp, uv + o, 0).rgb;
            float lum = Luma(s);
            float3 bright = s * saturate(lum - gBloomThreshold);
            float w = exp(-0.35f * (x * x + y * y));
            acc += bright * w;
            wsum += w;
        }
    }

    return color + (acc / max(wsum, 1e-4f)) * gBloomIntensity;
}

float3 ApplyFog(float3 color, float depth, float3 worldPos, float3 albedo, float3 normalEnc)
{
    if (gFogStrength <= 0.0f)
        return color;

    float3 n = normalize(normalEnc * 2.0f - 1.0f);
    float3 fogCol = lerp(gFogColor, gFogColor * saturate(albedo + 0.35f), 0.18f);

    if (depth >= 1.0f)
        return lerp(color, fogCol, saturate(gFogStrength));

    float dist = length(worldPos - gCameraWorld);
    float t = saturate((dist - gFogStart) / max(gFogEnd - gFogStart, 1.0f));
    t = t * t * (3.0f - 2.0f * t);
    t *= lerp(1.0f, 1.12f, saturate(-n.y));
    return lerp(color, fogCol, t * gFogStrength);
}

float3 ToneMap(float3 x)
{
    return x / (x + 1.0f);
}

float3 DecodeNormal(float3 enc)
{
    return normalize(enc * 2.0f - 1.0f);
}

float EdgeFactor(float2 uv)
{
    float2 t = gInvScreenSize;
    float3 nC = DecodeNormal(gNormal.Sample(gPointClamp, uv).xyz);
    float dC = gDepth.Sample(gPointClamp, uv).r;
    float3 pC = ReconstructWorld(uv, dC, gInvViewProj);

    float2 offs[4] =
    {
        float2(t.x, 0.0f),
        float2(-t.x, 0.0f),
        float2(0.0f, t.y),
        float2(0.0f, -t.y)
    };

    float e = 0.0f;
    float dist = max(length(pC - gCameraWorld), 1.0f);
    float geomC = step(dC, 0.99999f);

    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        float3 nS = DecodeNormal(gNormal.Sample(gPointClamp, uv + offs[i]).xyz);
        float dS = gDepth.Sample(gPointClamp, uv + offs[i]).r;
        float3 pS = ReconstructWorld(uv + offs[i], dS, gInvViewProj);
        float geomS = step(dS, 0.99999f);
        e = max(e, 1.0f - saturate(dot(nC, nS)));
        e = max(e, abs(geomC - geomS));
        float gap = length(pC - pS) / dist;
        e = max(e, saturate(gap * 10.0f) * geomC * geomS);
    }

    return saturate(e);
}

float3 ApplyEdges(float3 color, float2 uv)
{
    if (gEdgeStrength <= 0.0f)
        return color;

    float e = EdgeFactor(uv);
    e = saturate((e - 0.12f) * 3.2f);
    float3 ink = color * 0.12f;
    return lerp(color, ink, e * gEdgeStrength);
}

float4 PS(FSOut pin) : SV_Target
{
    float3 color = gScene.Sample(gLinearClamp, pin.TexC).rgb;
    float3 albedo = gAlbedo.Sample(gPointClamp, pin.TexC).rgb;
    float3 normalEnc = gNormal.Sample(gPointClamp, pin.TexC).xyz;
    float depth = gDepth.Sample(gPointClamp, pin.TexC).r;
    float3 worldPos = ReconstructWorld(pin.TexC, depth, gInvViewProj);

    color = ApplyBloom(color, pin.TexC);
    color = ApplyFog(color, depth, worldPos, albedo, normalEnc);
    color = ApplyEdges(color, pin.TexC);

    return float4(saturate(ToneMap(color)), 1.0f);
}
