Texture2D gBase : register(t0);
Texture2D gNormalMap : register(t1);
Texture2D gMetallicMap : register(t2);
Texture2D gRoughnessMap : register(t3);
SamplerState gSampler : register(s0);

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorldViewProj;
    float4x4 gWorld;
    float4 gDiffuseMul;
    float2 gUVScale;
    float2 gUVOffset;
    float3 gCameraWorld;
    float gDisplacementScale;
    float gTessMin;
    float gTessMax;
    float gTessNear;
    float gTessFar;
    float gReceiveShadow;
    float gMetallic;
    float gRoughness;
    float gPad0;
    float4 gPad[3];
};

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
    float4 TangentL : TANGENT;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 NormalW : NORMAL;
    float3 TangentW : TANGENT;
    float TangentSign : TEXCOORD1;
    float2 TexC : TEXCOORD;
};

struct GBufferOut
{
    float4 Albedo : SV_Target0;
    float4 Normal : SV_Target1;
};

VertexOut VS(VertexIn vin)
{
    VertexOut o;
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    o.PosH = mul(float4(vin.PosL, 1.0f), gWorldViewProj);
    o.NormalW = mul(float4(vin.NormalL, 0.0f), gWorld).xyz;
    o.TangentW = mul(float4(vin.TangentL.xyz, 0.0f), gWorld).xyz;
    o.TangentSign = vin.TangentL.w;
    o.TexC = vin.TexC * gUVScale + gUVOffset;
    return o;
}

GBufferOut PS(VertexOut pin)
{
    GBufferOut o;
    float3 base = gBase.Sample(gSampler, pin.TexC).rgb * gDiffuseMul.rgb;
    float metal = saturate(gMetallicMap.Sample(gSampler, pin.TexC).r);
    float rough = saturate(gRoughnessMap.Sample(gSampler, pin.TexC).r);
    rough = lerp(rough, min(rough, 0.12f), smoothstep(0.45f, 0.8f, metal));
    rough = max(rough, 0.045f);

    float3 nTex = gNormalMap.Sample(gSampler, pin.TexC).xyz * 2.0f - 1.0f;
    nTex.y = -nTex.y;

    float3 N = normalize(pin.NormalW);
    float3 T = pin.TangentW - N * dot(N, pin.TangentW);
    float tLen2 = dot(T, T);
    if (tLen2 > 1e-8f)
        T *= rsqrt(tLen2);
    else
        T = float3(1.0f, 0.0f, 0.0f);
    float3 B = normalize(cross(N, T) * pin.TangentSign);
    float3 nMap = normalize(mul(nTex, float3x3(T, B, N)));
    float3 n = normalize(lerp(nMap, N, smoothstep(0.45f, 0.8f, metal)));

    o.Albedo = float4(base, rough);
    o.Normal = float4(n * 0.5f + 0.5f, metal);
    return o;
}
