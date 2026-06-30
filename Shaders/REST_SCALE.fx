#include "ReShade.fxh"

texture2D REST_ScaleSource < semantic = "REST_SCALE_SOURCE"; >;
sampler2D sREST_ScaleSource { Texture = REST_ScaleSource; };

void ScalePS(in float4 pos : SV_Position, in float2 texcoord : Texcoord, out float4 o : SV_Target0)
{
    o = tex2D(sREST_ScaleSource, texcoord);
}

technique REST_SCALE
{
    pass
    {
        VertexShader = PostProcessVS;
        PixelShader = ScalePS; 
    }
}
