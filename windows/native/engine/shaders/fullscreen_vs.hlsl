struct VSOut {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

VSOut main(uint vertexId : SV_VertexID) {
    VSOut o;
    float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    o.uv = uv;
    o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}
