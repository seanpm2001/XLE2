// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)


#include "RenderStep_ResolveHDR.h"
#include "LightingTargets.h"
#include "LightingParserContext.h"
#include "LightingParser.h"
#include "Tonemap.h"
#include "../RenderCore/Techniques/RenderPass.h"
#include "../RenderCore/Techniques/CommonBindings.h"
#include "../RenderCore/Techniques/Techniques.h"
#include "../RenderCore/Techniques/ParsingContext.h"
#include "../RenderCore/Format.h"
#include "../RenderCore/FrameBufferDesc.h"
#include "../RenderCore/IAnnotator.h"
#include "../RenderCore/Metal/DeviceContext.h"

namespace SceneEngine
{
	using namespace RenderCore;

	const uint64_t Semantic_LuminanceResult = ConstHash64<'Lumi', 'nanc', 'eRes'>::Value;
	const uint64_t Semantic_BloomBuffer = ConstHash64<'Bloo', 'mBuf', 'fer'>::Value;

	void RenderStep_SampleLuminance::Execute(
		RenderCore::IThreadContext& threadContext,
		RenderCore::Techniques::ParsingContext& parsingContext,
		LightingParserContext& lightingParserContext,
		RenderStepFragmentInstance& rpi,
		IViewDelegate* viewDelegate)
	{
		auto* postLightingResolveInput = rpi.GetRenderPassInstance().GetInputAttachmentSRV(0);
		assert(postLightingResolveInput);

		auto toneMapSettings = lightingParserContext._delegate->GetToneMapSettings();
        LuminanceResult luminanceResult;
        if (toneMapSettings._flags & ToneMapSettings::Flags::EnableToneMap) {
                //  (must resolve luminance early, because we use it during the MSAA resolve)
            luminanceResult = ToneMap_SampleLuminance(
                threadContext, parsingContext, toneMapSettings,
				*postLightingResolveInput);
        }

		_downstream->SetLuminanceResult(std::move(luminanceResult));
	}

	RenderStep_SampleLuminance::RenderStep_SampleLuminance(const std::shared_ptr<RenderStep_ResolveHDR>& downstream)
	: _downstream(downstream)
	, _fragment(PipelineType::Compute)
	{
		auto hdrInput = _fragment.DefineAttachment(Techniques::AttachmentSemantics::ColorHDR);

		SubpassDesc subpass;
		subpass.AppendInput(hdrInput);
		_fragment.AddSubpass(std::move(subpass));
	}

	RenderStep_SampleLuminance::~RenderStep_SampleLuminance()
	{
	}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	RenderStep_ResolveHDR::RenderStep_ResolveHDR()
	: _fragment(RenderCore::PipelineType::Graphics)
	{
		auto hdrInput = _fragment.DefineAttachment(Techniques::AttachmentSemantics::ColorHDR);
		auto ldrOutput = _fragment.DefineAttachment(Techniques::AttachmentSemantics::ColorLDR);

		SubpassDesc subpass;
		subpass.AppendOutput({ ldrOutput, LoadStore::DontCare, LoadStore::Retain, {TextureViewDesc::Aspect::ColorSRGB} });
		subpass.AppendInput({ hdrInput, LoadStore::Retain_RetainStencil, LoadStore::DontCare });
		_fragment.AddSubpass(std::move(subpass));
	}

	RenderStep_ResolveHDR::~RenderStep_ResolveHDR() {}

#if 0
	static void LightingParser_ResolveMSAA(
        Metal::DeviceContext& context, 
        LightingParserContext& parserContext,
        RenderCore::Resource& destinationTexture,
        RenderCore::Resource& sourceTexture,
        Format resolveFormat)
    {
		#if GFXAPI_ACTIVE == GFXAPI_DX11	// platformtemp
				// todo -- support custom resolve (tone-map aware)
				// See AMD post on this topic:
				//      http://gpuopen.com/optimized-reversible-tonemapper-for-resolve/
			context.GetUnderlying()->ResolveSubresource(
				Metal::AsResource(destinationTexture).GetUnderlying().get(), D3D11CalcSubresource(0,0,0),
				Metal::AsResource(sourceTexture).GetUnderlying().get(), D3D11CalcSubresource(0,0,0),
				Metal::AsDXGIFormat(resolveFormat));
		#endif
    }
#endif

	void RenderStep_ResolveHDR::SetLuminanceResult(LuminanceResult&& luminanceResult)
	{
		_luminanceResult = std::move(luminanceResult);
	}

	void RenderStep_ResolveHDR::Execute(
		IThreadContext& threadContext,
		Techniques::ParsingContext& parsingContext,
		LightingParserContext& lightingParserContext,
		RenderStepFragmentInstance& rpi,
		IViewDelegate*)
	{
		GPUAnnotation anno(threadContext, "Resolve-MSAA-HDR");

		auto* postLightingResolveInput = rpi.GetRenderPassInstance().GetInputAttachmentSRV(0);
		assert(postLightingResolveInput);

#if 0   // platformtemp
            //
            //      Post lighting resolve operations...
            //          we must bind the depth buffer to whatever
            //          buffer contained the correct depth information from the
            //          previous rendering (might be the default depth buffer, or might
            //          not be)
            //
        if (qualitySettings._samplingCount > 1) {
            auto inputTextureDesc = Metal::ExtractDesc(postLightingResolveTexture->GetUnderlying());
			auto& msaaResolveRes = Techniques::FindCachedBox2<FinalResolveResources>(
				inputTextureDesc._textureDesc._width, inputTextureDesc._textureDesc._height, inputTextureDesc._textureDesc._format);
            LightingParser_ResolveMSAA(
                metalContext, parserContext,
                *msaaResolveRes._postMsaaResolveTexture->GetUnderlying(),
                *postLightingResolveTexture->GetUnderlying(),
				inputTextureDesc._textureDesc._format);

                // todo -- also resolve the depth buffer...!
                //      here; we switch the active textures to the msaa resolved textures
            postLightingResolve = IMainTargets::PostMSAALightResolve;
        }

        metalContext.Bind(MakeResourceList(postLightingResolveRTV), nullptr);       // we don't have a single-sample depths target at this time (only multisample)
        LightingParser_PostProcess(metalContext, parserContext);
#endif

        

            //  Write final colour to output texture
            //  We have to be careful about whether "SRGB" is enabled
            //  on the back buffer we're writing to. Depending on the
            //  tone mapping method, sometimes we want the SRGB conversion,
            //  other times we don't (because some tone map operations produce
            //  SRGB results, others give linear results)

		auto* targetDesc = rpi.GetRenderPassInstance().GetOutputAttachmentDesc(0);
		assert(targetDesc); (void)targetDesc;
		// This parameter should be tied to whether the texture view for the output texture has SRGB enabled
		// or not (ie, the hardware is writing out SRGB, rather than linear colors).
		// We're always writing to SRGB enabled output view currently
		bool hardwareSRGBEnabled = true;
        auto toneMapSettings = lightingParserContext._delegate->GetToneMapSettings();
		ToneMap_Execute(
            threadContext, parsingContext, _luminanceResult, toneMapSettings, 
			hardwareSRGBEnabled,
            *postLightingResolveInput);
	}
}


