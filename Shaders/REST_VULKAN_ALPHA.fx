#if __RENDERER__ >= 0x20000 && __RENDERER__ < 0x30000

#include "REST_Common.fxh"
texture2D REST_SavedAlpha { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = R8; };
sampler2D REST_AlphaSampler { Texture = REST_SavedAlpha; };

float4 REST_SaveAlphaPS(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    return tex2D(REST_BackBuffer, uv).aaaa;
}

float4 REST_RestoreAlphaPS(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    return tex2D(REST_AlphaSampler, uv).rrrr;
}

technique REST_SAVE_ALPHA
{
    pass { VertexShader = REST_FullscreenVS; PixelShader = REST_SaveAlphaPS; RenderTarget = REST_SavedAlpha; }
}

technique REST_RESTORE_ALPHA
{
    pass { VertexShader = REST_FullscreenVS; PixelShader = REST_RestoreAlphaPS; ColorWriteMask = 8; }
}
#endif
