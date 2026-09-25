struct FSOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

FSOut VSFullscreen(uint vid : SV_VertexID)
{
    FSOut o;
    float2 uv = float2(vid & 1, vid >> 1);
    o.TexC = uv;
    o.PosH = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return o;
}
