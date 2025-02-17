// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../RenderUtils.h"
#include "../ShaderLangUtil.h"
#include "../../Math/Vector.h"
#include "../../Math/Matrix.h"
#include "../../Math/ProjectionMath.h"
#include "../../Utility/IteratorUtils.h"

namespace Utility { class ParameterBox; }

namespace RenderCore { class InputElementDesc; class MiniInputElementDesc; }
namespace RenderCore { namespace Techniques
{

    class CameraDesc
    {
    public:
        Float4x4    _cameraToWorld = Identity<Float4x4>();
        float       _nearClip = 0.1f, _farClip = 100000.f;

        enum class Projection { Perspective, Orthogonal };
        Projection  _projection = Projection::Perspective;

        // perspective settings
        float       _verticalFieldOfView = Deg2Rad(34.8246f);

        // orthogonal settings
        float       _left = -1.f, _top = -1.f;
        float       _right = 1.f, _bottom = 1.f;
    };

    // Culling tests use an optimized implementation that takes advantage of
    // the fact that this structure is aligned.
    class alignas(16) ProjectionDesc
    {
    public:
        Float4x4        _worldToProjection;
        Float4x4        _cameraToProjection;
        Float4x4        _cameraToWorld;
        float           _verticalFov;
        float           _aspectRatio;
        float           _nearClip;
        float           _farClip;

        ProjectionDesc();

		#pragma push_macro("new")
		#undef new
			void* operator new(size_t);
		#pragma pop_macro("new")
		void operator delete(void*);
    };

    class GlobalTransformConstants
    {
    public:
        Float4x4    _worldToClip;
        Float4      _frustumCorners[4];
        Float3      _worldSpaceView;
        float       _farClip;
        Float4      _minimalProjection;
        Float4x4    _viewToWorld;
    };

    struct LocalTransformConstants
    {
    public:
        Float3x4    _localToWorld;
        Float3      _localSpaceView;
        unsigned    _dummy0;
    };

///////////////////////////////////////////////////////////////////////////////////////////////////

    Float4x4 Projection(const CameraDesc& sceneCamera, float viewportAspect);

    ClipSpaceType GetDefaultClipSpaceType();
	ShaderLanguage GetDefaultShaderLanguage();

    std::pair<Float3, Float3> BuildRayUnderCursor(
        Int2 mousePosition, CameraDesc& sceneCamera, 
        const std::pair<Float2, Float2>& viewport);
    
    GlobalTransformConstants BuildGlobalTransformConstants(const ProjectionDesc& projDesc);

    SharedPkt MakeLocalTransformPacket(const Float4x4& localToWorld, const CameraDesc& camera);
    SharedPkt MakeLocalTransformPacket(const Float4x4& localToWorld, const Float3& worldSpaceCameraPosition);
    LocalTransformConstants MakeLocalTransform(const Float4x4& localToWorld, const Float3& worldSpaceCameraPosition);

    bool HasHandinessFlip(const ProjectionDesc& projDesc);

	void SetGeoSelectors(ParameterBox& geoSelectors, IteratorRange<const InputElementDesc*> ia);
	void SetGeoSelectors(ParameterBox& geoSelectors, IteratorRange<const MiniInputElementDesc*> ia);

	/// <summary>Build a projection desc with parameters from a standard camera</summary>
	ProjectionDesc BuildProjectionDesc(const CameraDesc& sceneCamera, UInt2 viewportDims);

	/// <summary>Build a projection desc for an orthogonal camera</summary>
    ProjectionDesc BuildOrthogonalProjectionDesc(
        const Float4x4& cameraToWorld,
        float l, float t, float r, float b,
        float nearClip, float farClip);
}}

