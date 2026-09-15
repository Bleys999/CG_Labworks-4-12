#include "particle.hlsli"

RWStructuredBuffer<Particle> gParticles : register(u0);
Buffer<uint> gCount : register(t0);

cbuffer cbParticleSim : register(b0)
{
    float gDeltaTime;
    float gTotalTime;
    uint gEmitCount;
    uint gMaxParticles;
    float3 gEmitterPos;
    float gEmitLifetime;
    float3 gGravity;
    float gSizeMin;
    float4 gColor0;
    float4 gColor1;
    float3 gEmitVelocity;
    float gEmitSpread;
    float3 gCameraPos;
    float gSizeMax;
};

cbuffer cbBitonic : register(b1)
{
    uint gCompareDist;
    uint gBlockSize;
};

uint NextPow2(uint v)
{
    v--;
    v |= v >> 1u;
    v |= v >> 2u;
    v |= v >> 4u;
    v |= v >> 8u;
    v |= v >> 16u;
    return v + 1u;
}

[numthreads(256, 1, 1)]
void BitonicCS(uint3 dtid : SV_DispatchThreadID)
{
    uint n = gCount[0];
    if (n == 0u)
        return;

    uint i = dtid.x;
    uint n2 = NextPow2(n);
    if (n2 > gMaxParticles)
        n2 = gMaxParticles;
    if (i >= n2)
        return;

    uint partner = i ^ gCompareDist;
    if (partner <= i || partner >= n2)
        return;

    float di = 1.0e10f;
    float dj = 1.0e10f;
    Particle pi;
    Particle pj;
    pi.Position = 0.0f;
    pi.Age = 0.0f;
    pi.Velocity = 0.0f;
    pi.Lifetime = 0.0f;
    pi.Color = 0.0f;
    pi.Size = 0.0f;
    pi.Pad = 0.0f;
    pj = pi;

    if (i < n)
    {
        pi = gParticles[i];
        float3 d = pi.Position - gCameraPos;
        di = dot(d, d);
    }
    if (partner < n)
    {
        pj = gParticles[partner];
        float3 d = pj.Position - gCameraPos;
        dj = dot(d, d);
    }

    bool ascending = (i & gBlockSize) == 0u;
    bool doSwap = ascending ? (di > dj) : (di < dj);
    if (!doSwap)
        return;

    if (i < gMaxParticles)
        gParticles[i] = pj;
    if (partner < gMaxParticles)
        gParticles[partner] = pi;
}
