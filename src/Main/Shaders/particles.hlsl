#include "particle.hlsli"

StructuredBuffer<Particle> gParticles : register(t0);

cbuffer cbParticleDraw : register(b0)
{
    float4x4 gViewProj;
    float3 gCamRight;
    float gPad0;
    float3 gCamUp;
    float gPad1;
    float3 gCamForward;
    float gReceiveShadow;
    float gRoughness;
    float gMetallic;
    float2 gPadMat;
};

struct VSOut
{
    float3 PosW : POSITION;
    float4 Color : COLOR;
    float Size : SIZE;
};

struct GSOut
{
    float4 PosH : SV_POSITION;
    float3 NormalW : NORMAL;
    float3 PosW : POSITION1;
    float2 TexC : TEXCOORD;
    float4 Color : COLOR;
};

struct GBufferOut
{
    float4 Albedo : SV_Target0;
    float4 Normal : SV_Target1;
    float4 WorldPos : SV_Target2;
};

VSOut VS(uint vid : SV_VertexID)
{
    Particle p = gParticles[vid];
    VSOut o;
    o.PosW = p.Position;
    o.Color = p.Color;
    o.Size = p.Size;
    return o;
}

[maxvertexcount(4)]
void GS(point VSOut gin[1], inout TriangleStream<GSOut> triStream)
{
    float3 center = gin[0].PosW;
    float halfSize = gin[0].Size * 0.5f;
    float3 right = gCamRight * halfSize;
    float3 up = gCamUp * halfSize;
    float3 n = normalize(-gCamForward);

    float2 corners[4] =
    {
        float2(-1.0f, 1.0f),
        float2(-1.0f, -1.0f),
        float2(1.0f, 1.0f),
        float2(1.0f, -1.0f)
    };
    float2 uvs[4] =
    {
        float2(0.0f, 0.0f),
        float2(0.0f, 1.0f),
        float2(1.0f, 0.0f),
        float2(1.0f, 1.0f)
    };

    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        GSOut v;
        float3 posW = center + right * corners[i].x + up * corners[i].y;
        v.PosH = mul(float4(posW, 1.0f), gViewProj);
        v.NormalW = n;
        v.PosW = posW;
        v.TexC = uvs[i];
        v.Color = gin[0].Color;
        triStream.Append(v);
    }
}

GBufferOut PS(GSOut pin)
{
    float2 d = pin.TexC * 2.0f - 1.0f;
    if (dot(d, d) > 1.0f)
        discard;

    GBufferOut o;
    float3 n = normalize(pin.NormalW);
    o.Albedo = float4(pin.Color.rgb, gRoughness);
    o.Normal = float4(n * 0.5f + 0.5f, gMetallic);
    o.WorldPos = float4(pin.PosW, gReceiveShadow > 0.5f ? 1.0f : 0.75f);
    return o;
}
