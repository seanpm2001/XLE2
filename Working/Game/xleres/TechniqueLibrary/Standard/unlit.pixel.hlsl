// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Framework/MainGeometry.hlsl"
#include "../Framework/Binding.hlsl"
#include "../Math/TextureAlgorithm.hlsl"
#include "../Framework/CommonResources.hlsl"

Texture2D		DiffuseTexture          BIND_MAT_T0;

    //  This cbuffer contains basic constants are used frequently enough that
    //  we can add support for them in most shaders.
cbuffer BasicMaterialConstants BIND_MAT_B0
{
	float3  MaterialDiffuse;
    float   Opacity;
}

#if !((VSOUT_HAS_TEXCOORD>=1) && (MAT_ALPHA_TEST==1)) && (VULKAN!=1)
	[earlydepthstencil]
#endif
float4 main(VSOUT geo, SystemInputs sys) : SV_Target0
{
	float4 result = float4(MaterialDiffuse.rgb, Opacity);

    #if (VSOUT_HAS_TEXCOORD>=1) && (RES_HAS_DiffuseTexture!=0)
        result *= DiffuseTexture.Sample(MaybeAnisotropicSampler, geo.texCoord);
    #endif

    #if (VSOUT_HAS_COLOR==1) && MAT_MODULATE_VERTEX_ALPHA
        result.a *= geo.color.a;
    #endif

    #if (VSOUT_HAS_COLOR>=1)
        result.rgb *= geo.color.rgb;
    #endif

    return result;
}
