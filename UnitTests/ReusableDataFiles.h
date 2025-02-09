#pragma once

//
// This is a collection of strings, which get mounted as virtual files when running unit tests
// It's a little more convenient than having actual data files hanging around in the filesystem
// somewhere. This way the data is associated with the code, and will not get in the way when
// running non-unit test executables.
//

static const char* s_examplePerPixelShaderFile = R"--(
	#include "xleres/TechniqueLibrary/Framework/MainGeometry.hlsl"
	#include "xleres/TechniqueLibrary/Framework/CommonResources.hlsl"
	#include "xleres/TechniqueLibrary/Framework/gbuffer.hlsl"

	Texture2D       Texture0		BIND_MAT_T0;		// Diffuse

	cbuffer BasicMaterialConstants
	{
		float4 HairColor;
	}

	GBufferValues PerPixelInShaderFile(VSOUT geo)
	{
		GBufferValues result = GBufferValues_Default();

		float4 diffuseTextureSample = 1.0.xxxx;
		#if (VSOUT_HAS_TEXCOORD>=1) && (RES_HAS_Texture0!=0)
			diffuseTextureSample = Texture0.Sample(MaybeAnisotropicSampler, geo.texCoord);
			result.diffuseAlbedo = diffuseTextureSample.rgb;
			result.blendingAlpha = diffuseTextureSample.a;
		#endif

		return result;
	}
)--";


static const char* s_exampleGraphFile = R"--(
	import example_perpixel = "example-perpixel.pixel.hlsl";
	import templates = "xleres/nodes/templates.sh"

	GBufferValues Bind_PerPixel(VSOUT geo) implements templates::PerPixel
	{
		return example_perpixel::PerPixelInShaderFile(geo:geo).result;
	}
)--";

static const char* s_complicatedGraphFile = R"--(
	import simple_example = "example.graph";
	import simple_example_dupe = "ut-data/example.graph";
	import example_perpixel = "example-perpixel.pixel.hlsl";
	import templates = "xleres/nodes/templates.sh";
	import conditions = "xleres/nodes/conditions.sh";
	import internalComplicatedGraph = "internalComplicatedGraph.graph";

	GBufferValues Internal_PerPixel(VSOUT geo)
	{
		return example_perpixel::PerPixelInShaderFile(geo:geo).result;
	}

	GBufferValues Bind2_PerPixel(VSOUT geo) implements templates::PerPixel
	{
		captures MaterialUniforms = ( float3 DiffuseColor, float SomeFloat = "0.25" );
		captures AnotherCaptures = ( float2 Test0, float4 Test2 = "{1,2,3,4}", float SecondaryCaptures = "0.7f" );
		if "defined(SIMPLE_BIND)" return simple_example::Bind_PerPixel(geo:geo).result;
		if "!defined(SIMPLE_BIND)" return Internal_PerPixel(geo:geo).result;
	}

	bool Bind_EarlyRejectionTest(VSOUT geo) implements templates::EarlyRejectionTest
	{
		captures MaterialUniforms = ( float AlphaWeight = "0.5" );
		if "defined(ALPHA_TEST)" return conditions::LessThan(lhs:MaterialUniforms.AlphaWeight, rhs:"0.5").result;
		return internalComplicatedGraph::Bind_EarlyRejectionTest(geo:geo).result;
	}
)--";

static const char* s_internalShaderFile = R"--(
	#include "xleres/TechniqueLibrary/Framework/MainGeometry.hlsl"

	bool ShouldBeRejected(VSOUT geo, float threshold)
	{
		#if defined(SELECTOR_0) && defined(SELECTOR_1)
			return true;
		#else
			return false;
		#endif
	}
)--";

static const char* s_internalComplicatedGraph = R"--(
	import internal_shader_file = "internalShaderFile.pixel.hlsl";
	
	bool Bind_EarlyRejectionTest(VSOUT geo) implements templates::EarlyRejectionTest
	{
		captures MaterialUniforms = ( float AnotherHiddenUniform = "0.5" );
		return internal_shader_file::ShouldBeRejected(geo:geo, threshold:MaterialUniforms.AnotherHiddenUniform).result;
	}
)--";

static const char* s_basicTechniqueFile = R"--(
	~Shared
		~Selectors
			CLASSIFY_NORMAL_MAP
			SKIP_MATERIAL_DIFFUSE=0

	~Deferred_NoPatches
		~Inherit; Shared
		VertexShader=xleres/TechniqueLibrary/Standard/main.vertex.hlsl:frameworkEntry
		PixelShader=xleres/TechniqueLibrary/Standard/nopatches.pixel.hlsl:deferred

	~Deferred_PerPixel
		~Inherit; Shared
		VertexShader=xleres/TechniqueLibrary/Standard/main.vertex.hlsl:frameworkEntry
		PixelShader=xleres/TechniqueLibrary/Standard/deferred.pixel.hlsl:frameworkEntry

	~Deferred_PerPixelAndEarlyRejection
		~Inherit; Shared
		VertexShader=xleres/TechniqueLibrary/Standard/main.vertex.hlsl:frameworkEntry
		PixelShader=xleres/TechniqueLibrary/Standard/deferred.pixel.hlsl:frameworkEntryWithEarlyRejection
)--";
