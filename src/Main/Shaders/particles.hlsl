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
    uint gTriCount;
    float gPadMat;
};

struct VSOut
{
    float3 PosW : POSITION;
    float4 Color : COLOR;
    float Size : SIZE;
};

struct MonkeyVert
{
    float3 Pos;
    float Pad0;
    float3 Normal;
    float Pad1;
};

StructuredBuffer<MonkeyVert> gMonkey : register(t1);

struct GSOut
{
    float4 PosH : SV_POSITION;
    float3 NormalW : NORMAL;
    float3 Color : COLOR;
};

cbuffer cbChunk : register(b1)
{
    uint gTriStart;
};

struct GBufferOut
{
    float4 Albedo : SV_Target0;
    float4 Normal : SV_Target1;
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

[maxvertexcount(84)]
void GS(point VSOut gin[1], inout TriangleStream<GSOut> triStream)
{
    float3 center = gin[0].PosW;
    float scale = gin[0].Size;

    [loop]
    for (uint t = 0; t < 28; ++t)
    {
        uint tri = gTriStart + t;
        if (tri >= gTriCount)
            break;

        [unroll]
        for (uint c = 0; c < 3; ++c)
        {
            MonkeyVert mv = gMonkey[tri * 3 + c];
            GSOut v;
            float3 posW = center + mv.Pos * scale;
            v.PosH = mul(float4(posW, 1.0f), gViewProj);
            v.NormalW = mv.Normal;
            v.Color = gin[0].Color.rgb;
            triStream.Append(v);
        }
        triStream.RestartStrip();
    }
}

GBufferOut PS(GSOut pin)
{
    GBufferOut o;
    float3 n = normalize(pin.NormalW);
    o.Albedo = float4(pin.Color, gRoughness);
    o.Normal = float4(n * 0.5f + 0.5f, gMetallic);
    return o;
}
