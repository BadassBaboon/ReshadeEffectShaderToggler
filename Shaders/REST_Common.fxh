#ifndef REST_COMMON_FXH
#define REST_COMMON_FXH

texture2D REST_Color : COLOR;
sampler2D REST_BackBuffer { Texture = REST_Color; };

void REST_FullscreenVS(uint id : SV_VertexID, out float4 position : SV_Position, out float2 uv : TEXCOORD0)
{
    uv = float2((id == 2) ? 2.0 : 0.0, (id == 1) ? 2.0 : 0.0);
    position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

#endif
