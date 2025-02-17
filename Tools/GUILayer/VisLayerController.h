// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "UITypesBinding.h"
#include "EngineDevice.h"

namespace GUILayer 
{
    class VisLayerControllerPimpl;

    ref class ModelVisSettings;
    ref class VisMouseOver;
	ref class VisOverlaySettings;
	ref class LayerControl;
	ref class VisAnimationState;
	ref class RawMaterial;
	ref class TechniqueDelegateWrapper;
	ref class CompiledShaderPatchCollectionWrapper;

	public ref class VisLayerController : public IOnEngineShutdown
    {
	public:
		property VisMouseOver^ MouseOver { VisMouseOver^ get(); }
		property VisAnimationState^ AnimationState { VisAnimationState^ get(); }

		void SetScene(ModelVisSettings^ settings);
		void SetScene(MaterialVisSettings^ settings);
		void SetPreviewRegistryScene(System::String^ name);

		void SetOverlaySettings(VisOverlaySettings^ settings);
		VisOverlaySettings^ GetOverlaySettings();

		void SetPatchCollectionOverrides(CompiledShaderPatchCollectionWrapper^ patchCollection);

		void ResetCamera();

		void AttachToView(LayerControl^ view);
		void DetachFromView(LayerControl^ view);

		VisLayerController();
		~VisLayerController();
		!VisLayerController();
		virtual void OnEngineShutdown();

	private:
		clix::auto_ptr<VisLayerControllerPimpl> _pimpl;
	};

}
