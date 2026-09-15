#include "particle.hlsli"

ConsumeStructuredBuffer<Particle> gInput : register(u0);
AppendStructuredBuffer<Particle> gOutput : register(u1);

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

float Hash11(uint n)
{
    n = (n << 13u) ^ n;
    n = n * (n * n * 15731u + 789221u) + 1376312589u;
    return float(n & 0x7fffffffu) / float(0x7fffffff);
}

float3 Hash13(uint n)
{
    return float3(Hash11(n), Hash11(n * 17u + 3u), Hash11(n * 31u + 11u));
}

[numthreads(1, 1, 1)]
void UpdateCS(uint3 dtid : SV_DispatchThreadID)
{
    Particle p = gInput.Consume();
    p.Age += gDeltaTime;
    p.Velocity += gGravity * gDeltaTime;
    p.Position += p.Velocity * gDeltaTime;
    if (p.Age < p.Lifetime && p.Position.y > -0.05f)
        gOutput.Append(p);
}

[numthreads(64, 1, 1)]
void EmitCS(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= gEmitCount)
        return;

    uint seed = dtid.x + asuint(gTotalTime * 1000.0f) * 747796405u + 2891336453u;
    float3 r = Hash13(seed);
    float3 r2 = Hash13(seed * 13u + 7u);

    Particle p;
    p.Position = gEmitterPos + (r - 0.5f) * 0.9f;
    p.Age = 0.0f;
    p.Velocity = gEmitVelocity + float3(r.x - 0.5f, r.y * 0.35f, r.z - 0.5f) * gEmitSpread;
    p.Lifetime = gEmitLifetime * (0.75f + r2.x * 0.5f);
    p.Color = lerp(gColor0, gColor1, r2.y);
    p.Size = lerp(gSizeMin, gSizeMax, r2.z);
    p.Pad = float3(0.0f, 0.0f, 0.0f);
    gOutput.Append(p);
}
