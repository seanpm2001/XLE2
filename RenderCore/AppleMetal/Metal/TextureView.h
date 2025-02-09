// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "Resource.h"
#include "ObjectFactory.h"
#include "../../../Externals/Misc/OCPtr.h"

namespace RenderCore { namespace Metal_AppleMetal
{
    class ShaderResourceView
    {
    public:
        const TBC::OCPtr<AplMtlTexture>&    GetUnderlying() const { return _resource->GetTexture(); }
        bool                                IsGood() const { return _resource != nullptr && _resource->GetTexture().get() != nullptr; }
        bool                                HasMipMaps() const { return _hasMipMaps; }
        const std::shared_ptr<Resource>& GetResource() const { return _resource; }

        ShaderResourceView(const ObjectFactory& factory, const IResourcePtr& resource, const TextureViewDesc& window = TextureViewDesc());
        explicit ShaderResourceView(const IResourcePtr& resource, const TextureViewDesc& window = TextureViewDesc());
        ShaderResourceView();

        TextureViewDesc _window;

    private:
        bool _hasMipMaps;
        std::shared_ptr<Resource> _resource;
    };
}}
