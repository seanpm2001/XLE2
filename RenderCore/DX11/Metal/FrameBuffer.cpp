// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "FrameBuffer.h"
#include "Format.h"
#include "Resource.h"
#include "TextureView.h"
#include "DeviceContext.h"
#include "ObjectFactory.h"
#include "State.h"
#include "../../Format.h"
#include "../../ResourceUtils.h"
#include "../../../Utility/MemoryUtils.h"

#include "IncludeDX11.h"

namespace RenderCore { namespace Metal_DX11
{
	static bool HasClear(LoadStore ls) 
	{
		return ls == LoadStore::Clear || ls == LoadStore::Clear_ClearStencil || ls == LoadStore::Clear_RetainStencil || ls == LoadStore::DontCare_ClearStencil || ls == LoadStore::Retain_ClearStencil;
	}

    FrameBuffer::FrameBuffer(
		ObjectFactory&,
        const FrameBufferDesc& fbDesc,
        const INamedAttachments& namedResources)
    {
        // We must create the frame buffer, including all resources and views required.
        // Here, some resources can come from the presentation chain. But other resources will
        // be created an attached to this object.
        auto subpasses = fbDesc.GetSubpasses();

		ViewPool<RenderTargetView> rtvPool;
		ViewPool<DepthStencilView> dsvPool;
		unsigned clearValueIterator = 0;
        
        _subpasses.resize(subpasses.size());
        for (unsigned c=0; c<(unsigned)subpasses.size(); ++c) {
            const auto& spDesc = subpasses[c];
            auto& sp = _subpasses[c];

			unsigned initialViewportWidth = D3D11_VIEWPORT_BOUNDS_MAX;
			unsigned initialViewportHeight = D3D11_VIEWPORT_BOUNDS_MAX;

            sp._rtvCount = std::min((unsigned)spDesc.GetOutputs().size(), s_maxMRTs);
            for (unsigned r=0; r<sp._rtvCount; ++r) {
				const auto& attachmentView = spDesc.GetOutputs()[r];
				auto resource = namedResources.GetResource(attachmentView._resourceName);
				auto* attachmentDesc = namedResources.GetDesc(attachmentView._resourceName);
				if (!resource || !attachmentDesc)
					Throw(::Exceptions::BasicLabel("Could not find attachment resource for RTV in FrameBuffer::FrameBuffer"));
				auto completeView = CompleteTextureViewDesc(*attachmentDesc, attachmentView._window, (AsTypelessFormat(attachmentDesc->_format) == attachmentDesc->_format) ? TextureViewDesc::Aspect::ColorLinear : TextureViewDesc::Aspect::UndefinedAspect);
                sp._rtvs[r] = *rtvPool.GetView(resource, completeView);
				sp._rtvLoad[r] = attachmentView._loadFromPreviousPhase;
				if (HasClear(sp._rtvLoad[r])) {
					sp._rtvClearValue[r] = clearValueIterator++;
				} else {
					sp._rtvClearValue[r] = ~0u;
				}

				auto resDesc = resource->GetDesc();
				assert(resDesc._type == ResourceDesc::Type::Texture);
				initialViewportWidth = std::min(resDesc._textureDesc._width, initialViewportWidth);
				initialViewportHeight = std::min(resDesc._textureDesc._height, initialViewportHeight);
			}

			if (spDesc.GetDepthStencil()._resourceName != ~0u) {
				auto resource = namedResources.GetResource(spDesc.GetDepthStencil()._resourceName);
				auto* attachmentDesc = namedResources.GetDesc(spDesc.GetDepthStencil()._resourceName);
				if (!resource || !attachmentDesc)
					Throw(::Exceptions::BasicLabel("Could not find attachment resource for DSV in FrameBuffer::FrameBuffer"));
				auto completeView = CompleteTextureViewDesc(*attachmentDesc, spDesc.GetDepthStencil()._window, TextureViewDesc::Aspect::DepthStencil);
				sp._dsv = *dsvPool.GetView(resource, completeView);
				sp._dsvLoad = spDesc.GetDepthStencil()._loadFromPreviousPhase;
				if (HasClear(sp._dsvLoad)) {
					sp._dsvClearValue = clearValueIterator++;
				} else {
					sp._dsvClearValue = ~0u;
				}

				auto resDesc = resource->GetDesc();
				assert(resDesc._type == ResourceDesc::Type::Texture);
				initialViewportWidth = std::min(resDesc._textureDesc._width, initialViewportWidth);
				initialViewportHeight = std::min(resDesc._textureDesc._height, initialViewportHeight);
			}

			sp._initialViewportWidth = initialViewportWidth;
			sp._initialViewportHeight = initialViewportHeight;
        }
    }

    void FrameBuffer::BindSubpass(DeviceContext& context, unsigned subpassIndex, IteratorRange<const ClearValue*> clearValues) const
    {
        if (subpassIndex >= _subpasses.size())
            Throw(::Exceptions::BasicLabel("Attempting to set invalid subpass"));

        const auto& s = _subpasses[subpassIndex];
		for (unsigned c = 0; c < s._rtvCount; ++c) {
			ClearValue cv = (s._rtvClearValue[c] < clearValues.size()) ? clearValues[s._rtvClearValue[c]] : MakeClearValue(0.f, 0.f, 0.f, 1.f);
			if (s._rtvLoad[c] == LoadStore::Clear)
				context.Clear(s._rtvs[c], cv._float);
		}

		if (s._dsvLoad == LoadStore::Clear_ClearStencil) {
			ClearValue cv = (s._dsvClearValue < clearValues.size()) ? clearValues[s._dsvClearValue] : MakeClearValue(1.0f, 0);
			context.Clear(
				s._dsv, DeviceContext::ClearFilter::Depth | DeviceContext::ClearFilter::Stencil,
				cv._depthStencil._depth, cv._depthStencil._stencil);
        } else if (s._dsvLoad == LoadStore::Clear || s._dsvLoad == LoadStore::Clear_RetainStencil) {
			ClearValue cv = (s._dsvClearValue < clearValues.size()) ? clearValues[s._dsvClearValue] : MakeClearValue(1.0f, 0);
			context.Clear(
				s._dsv, DeviceContext::ClearFilter::Depth,
				cv._depthStencil._depth, cv._depthStencil._stencil);
        } else if (s._dsvLoad == LoadStore::DontCare_ClearStencil || s._dsvLoad == LoadStore::Retain_ClearStencil) {
			ClearValue cv = (s._dsvClearValue < clearValues.size()) ? clearValues[s._dsvClearValue] : MakeClearValue(1.0f, 0);
			context.Clear(
				s._dsv, DeviceContext::ClearFilter::Stencil,
				cv._depthStencil._depth, cv._depthStencil._stencil);
        }

		ID3D::RenderTargetView* bindRTVs[s_maxMRTs];
		ID3D::DepthStencilView* bindDSV = nullptr;
		for (unsigned c = 0; c<s._rtvCount; ++c)
			bindRTVs[c] = s._rtvs[c].GetUnderlying();
		bindDSV = s._dsv.GetUnderlying();
        context.GetUnderlying()->OMSetRenderTargets(s._rtvCount, bindRTVs, bindDSV);

		{
            context.BeginSubpass(s._initialViewportWidth, s._initialViewportHeight);

			// Ensure that we always initialize the viewports to the maximum size of the subpass when it's
			// bound. This keeps compatibility with other APIs where the viewports and scissor rects are
			// effectively rebound on a new subpass (and it's just convenient, anyway)
			ViewportDesc viewports[1];
            viewports[0] = ViewportDesc{0.f, 0.f, (float)s._initialViewportWidth, (float)s._initialViewportHeight};
            ScissorRect scissorRects[1];
            scissorRects[0] = ScissorRect{0, 0, s._initialViewportWidth, s._initialViewportHeight};
            context.SetViewportAndScissorRects(MakeIteratorRange(viewports), MakeIteratorRange(scissorRects));
		}
    }

	void FrameBuffer::FinishSubpass(DeviceContext& context, unsigned subpassIndex) const
    {
		if (subpassIndex >= (unsigned)_subpasses.size()) return;

		context.EndSubpass();
	}

	FrameBuffer::FrameBuffer() {}
    FrameBuffer::~FrameBuffer() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

	static unsigned s_nextSubpass = 0;
	static std::vector<ClearValue> s_clearValues;

    void BeginRenderPass(
        DeviceContext& context,
        FrameBuffer& frameBuffer,
        const FrameBufferDesc& layout,
        const FrameBufferProperties& props,
        IteratorRange<const ClearValue*> clearValues)
    {
		s_nextSubpass = 0;
		s_clearValues.clear();
		s_clearValues.insert(s_clearValues.end(), clearValues.begin(), clearValues.end());
		context.BeginRenderPass();
		BeginNextSubpass(context, frameBuffer);
    }

    void BeginNextSubpass(
        DeviceContext& context,
        FrameBuffer& frameBuffer)
    {
        // Queue up the next render targets
		auto subpassIndex = s_nextSubpass;
		if (subpassIndex < frameBuffer.GetSubpassCount())
			frameBuffer.BindSubpass(context, subpassIndex, MakeIteratorRange(s_clearValues));
		++s_nextSubpass;
    }

	void EndSubpass(DeviceContext& context, FrameBuffer& frameBuffer)
	{
	}

    void EndRenderPass(DeviceContext& context)
    {
        // For compatibility with Vulkan, it makes sense to unbind render targets here. This is important
        // if the render targets will be used as compute shader outputs in follow up steps. It also prevents
        // rendering outside of render passes. But sometimes it will produce redundant calls to OMSetRenderTargets().
		context.GetUnderlying()->OMSetRenderTargets(0, nullptr, nullptr);
		context.EndRenderPass();
    }

	unsigned GetCurrentSubpassIndex(DeviceContext& context)
	{
		return s_nextSubpass-1;
	}	

}}

