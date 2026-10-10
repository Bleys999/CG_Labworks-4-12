struct FSOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

FSOut VSFullscreen(uint vid : SV_VertexID)
{
    FSOut o;
    float2 uv = float2((vid << 1) & 2, vid & 2);
    o.TexC = uv;
    o.PosH = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return o;
}

float3 ReconstructWorld(float2 uv, float depth, float4x4 invViewProj)
{
    float4 clip = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f);
    float4 worldH = mul(clip, invViewProj);
    return worldH.xyz / max(worldH.w, 1e-6f);
}
