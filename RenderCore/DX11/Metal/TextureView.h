// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "DX11.h"
#include "../../ResourceDesc.h"
#include "../../../Utility/IntrusivePtr.h"
#include "../../Types_Forward.h"
#include <memory>

namespace RenderCore { class IResource; }

namespace RenderCore { namespace Metal_DX11
{
    class DeviceContext;
	class ObjectFactory;

    class RenderTargetView
    {
    public:
        explicit RenderTargetView(const std::shared_ptr<IResource>& resource, const TextureViewDesc& window = TextureViewDesc());
		RenderTargetView(ObjectFactory& objFactory, const std::shared_ptr<IResource>& resource, const TextureViewDesc& window = TextureViewDesc());
		explicit RenderTargetView(DeviceContext& context);
        
        std::shared_ptr<IResource>				GetResource() const;

        using UnderlyingType = ID3D::RenderTargetView*;
        UnderlyingType                          GetUnderlying() const { return _underlying.get(); }
        bool                                    IsGood() const { return _underlying.get() != nullptr; }

		RenderTargetView(intrusive_ptr<ID3D::RenderTargetView>&& resource);
		RenderTargetView(const intrusive_ptr<ID3D::RenderTargetView>& resource);
		RenderTargetView();
		~RenderTargetView();
		RenderTargetView(const RenderTargetView& cloneFrom);
		RenderTargetView(RenderTargetView&& moveFrom) never_throws;
		RenderTargetView& operator=(const RenderTargetView& cloneFrom);
		RenderTargetView& operator=(RenderTargetView&& moveFrom) never_throws;

    private:
        intrusive_ptr<ID3D::RenderTargetView>      _underlying;
    };

    class DepthStencilView
    {
    public:
        explicit DepthStencilView(const std::shared_ptr<IResource>& resource, const TextureViewDesc& window = TextureViewDesc());
		DepthStencilView(ObjectFactory& objFactory, const std::shared_ptr<IResource>& resource, const TextureViewDesc& window = TextureViewDesc());
		explicit DepthStencilView(DeviceContext& context);

        std::shared_ptr<IResource>				GetResource() const;

        using UnderlyingType = ID3D::DepthStencilView*;
        UnderlyingType                          GetUnderlying() const { return _underlying.get(); }
        bool                                    IsGood() const { return _underlying.get() != nullptr; }

		DepthStencilView(intrusive_ptr<ID3D::DepthStencilView>&& resource);
		DepthStencilView(const intrusive_ptr<ID3D::DepthStencilView>& resource);
		DepthStencilView();
		~DepthStencilView();
		DepthStencilView(const DepthStencilView& cloneFrom);
		DepthStencilView(DepthStencilView&& moveFrom) never_throws;
		DepthStencilView& operator=(const DepthStencilView& cloneFrom);
		DepthStencilView& operator=(DepthStencilView&& moveFrom) never_throws;
    private:
        intrusive_ptr<ID3D::DepthStencilView>      _underlying;
    };

    class UnorderedAccessView
    {
    public:
        explicit UnorderedAccessView(const std::shared_ptr<IResource>& resource, const TextureViewDesc& window = TextureViewDesc());
		UnorderedAccessView(ObjectFactory& objFactory, const std::shared_ptr<IResource>& resource, const TextureViewDesc& window = TextureViewDesc());
        
        std::shared_ptr<IResource>		GetResource() const;

        using UnderlyingType = ID3D::UnorderedAccessView*;
        UnderlyingType                          GetUnderlying() const { return _underlying.get(); }
        bool                                    IsGood() const { return _underlying.get() != nullptr; }

		UnorderedAccessView(intrusive_ptr<ID3D::UnorderedAccessView>&& resource);
		UnorderedAccessView(const intrusive_ptr<ID3D::UnorderedAccessView>& resource);
		UnorderedAccessView();
		~UnorderedAccessView();
		UnorderedAccessView(const UnorderedAccessView& cloneFrom);
		UnorderedAccessView(UnorderedAccessView&& moveFrom) never_throws;
		UnorderedAccessView& operator=(const UnorderedAccessView& cloneFrom);
		UnorderedAccessView& operator=(UnorderedAccessView&& moveFrom) never_throws;
    private:
        intrusive_ptr<ID3D::UnorderedAccessView>   _underlying;
    };

    class ShaderResourceView
    {
    public:
        explicit ShaderResourceView(const std::shared_ptr<IResource>& resource, const TextureViewDesc& window = TextureViewDesc());
		ShaderResourceView(ObjectFactory& objFactory, const std::shared_ptr<IResource>& resource, const TextureViewDesc& window = TextureViewDesc());

        static ShaderResourceView RawBuffer(const std::shared_ptr<IResource>& resource, unsigned sizeBytes, unsigned offsetBytes = 0);

        std::shared_ptr<IResource>				GetResource() const;
        
        using UnderlyingType = ID3D::ShaderResourceView*;
        UnderlyingType                          GetUnderlying() const { return _underlying.get(); }
        bool                                    IsGood() const { return _underlying.get() != nullptr; }

		ShaderResourceView(intrusive_ptr<ID3D::ShaderResourceView>&& resource);
		ShaderResourceView(const intrusive_ptr<ID3D::ShaderResourceView>& resource);
		ShaderResourceView();
		~ShaderResourceView();
		ShaderResourceView(const ShaderResourceView& cloneFrom);
		ShaderResourceView(ShaderResourceView&& moveFrom) never_throws;
		ShaderResourceView& operator=(const ShaderResourceView& cloneFrom);
		ShaderResourceView& operator=(ShaderResourceView&& moveFrom) never_throws;
    private:
        intrusive_ptr<ID3D::ShaderResourceView>   _underlying;
    };

        ////////////////////////////////////////////////////////////////////////////////////////////////

    
}}

