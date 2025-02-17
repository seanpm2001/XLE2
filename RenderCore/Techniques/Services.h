// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Utility/IteratorUtils.h"
#include <memory>
#include <cassert>

namespace RenderCore { class IDevice; }
namespace BufferUploads { class IManager; struct TexturePlugin; }

namespace RenderCore { namespace Techniques
{
	class DeformOperationFactory;

	class Services
	{
	public:
		static BufferUploads::IManager& GetBufferUploads() { return *s_instance->_bufferUploads; }
		static DeformOperationFactory& GetDeformOperationFactory() { assert(s_instance->_deformOpsFactory); return *s_instance->_deformOpsFactory; }
		static RenderCore::IDevice& GetDevice() { return *s_instance->_device; }
		static bool HasInstance() { return s_instance != nullptr; }
		static Services& GetInstance() { assert(s_instance); return *s_instance; }

		IteratorRange<const BufferUploads::TexturePlugin*> GetTexturePlugins();
		unsigned RegisterTexturePlugin(BufferUploads::TexturePlugin&& plugin);
		void DeregisterTexturePlugin(unsigned pluginId);
		
		Services(const std::shared_ptr<RenderCore::IDevice>& device);
		~Services();

		void AttachCurrentModule();
		void DetachCurrentModule();

		Services(const Services&) = delete;
		const Services& operator=(const Services&) = delete;

		BufferUploads::IManager& GetBufferUploadsInstance() { return *_bufferUploads; }
	protected:
		std::shared_ptr<RenderCore::IDevice> _device;
		std::shared_ptr<DeformOperationFactory> _deformOpsFactory;
		std::unique_ptr<BufferUploads::IManager> _bufferUploads;
		static Services* s_instance;

		class Pimpl;
		std::unique_ptr<Pimpl> _pimpl;
	};

}}
