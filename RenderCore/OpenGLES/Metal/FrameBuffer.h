// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "TextureView.h"
#include "../../FrameBufferDesc.h"
#include "../../Types_Forward.h"
#include <memory>

namespace RenderCore { namespace Metal_OpenGLES
{
    class ObjectFactory;
    class RenderTargetView;
    class ShaderResourceView;
    class DepthStencilView;
    class DeviceContext;

    class FrameBuffer
	{
	public:
        void BindSubpass(DeviceContext& context, unsigned subpassIndex, IteratorRange<const ClearValue*> clearValues) const;
        void FinishSubpass(DeviceContext& context, unsigned subpassIndex) const;

        OpenGL::FrameBuffer* GetSubpassUnderlyingFramebuffer(unsigned subpassIndex);
        const OpenGL::FrameBuffer* GetSubpassUnderlyingFramebuffer(unsigned subpassIndex) const;
        unsigned GetSubpassCount() const { return (unsigned)_subpasses.size(); }

		FrameBuffer(
			ObjectFactory& factory,
            const FrameBufferDesc& desc,
            const INamedAttachments& namedResources);
		FrameBuffer();
		~FrameBuffer();
	private:
        static const unsigned s_maxMRTs = 4u;

        class Subpass
        {
        public:
			RenderTargetView _rtvs[s_maxMRTs];
			DepthStencilView _dsv;
			unsigned _rtvCount;

			LoadStore _rtvLoad[s_maxMRTs];
			unsigned _rtvClearValue[s_maxMRTs];
			LoadStore _dsvLoad;
			unsigned _dsvClearValue;
            bool _dsvHasDepth, _dsvHasStencil;

            intrusive_ptr<OpenGL::FrameBuffer> _frameBuffer;
            intrusive_ptr<OpenGL::FrameBuffer> _resolveTarget;
            unsigned _resolveFlags;
            unsigned _resolveWidth, _resolveHeight;
        };
        std::vector<Subpass> _subpasses;
	};

    void BeginRenderPass(
        DeviceContext& context,
        FrameBuffer& frameBuffer,
        IteratorRange<const ClearValue*> clearValues = {});

    void BeginNextSubpass(DeviceContext& context, FrameBuffer& frameBuffer);
    void EndSubpass(DeviceContext& context, FrameBuffer& frameBuffer);
    void EndRenderPass(DeviceContext& context);
    unsigned GetCurrentSubpassIndex(DeviceContext& context);

}}
