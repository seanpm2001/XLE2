// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "IntersectionTest.h"
#include "RayVsModel.h"
#include "LightingParser.h"
#include "Terrain.h"
#include "PlacementsManager.h"
#include "SceneParser.h"
#include "LightingParserContext.h"
#include "RenderStep.h"

#include "../BufferUploads/DataPacket.h"
#include "../RenderCore/Metal/DeviceContext.h"
#include "../RenderCore/Metal/State.h"
#include "../RenderCore/RenderUtils.h"
#include "../RenderCore/IDevice.h"
#include "../RenderCore/Techniques/CommonBindings.h"
#include "../RenderCore/Techniques/ParsingContext.h"
#include "../RenderCore/Techniques/Techniques.h"

#include "../Math/Transformations.h"
#include "../Math/Vector.h"
#include "../Math/ProjectionMath.h"
#include "../Utility/BitUtils.h"

#pragma warning(disable:4505)

namespace SceneEngine
{
    

////////////////////////////////////////////////////////////////////////////////////////////////////////

    static std::pair<Float3, bool> FindTerrainIntersection(
        RenderCore::IThreadContext& context,
        RenderCore::Techniques::ParsingContext& parserContext,
        TerrainManager& terrainManager,
        std::pair<Float3, Float3> worldSpaceRay)
    {
        CATCH_ASSETS_BEGIN
            TerrainManager::IntersectionResult intersections[8];
            unsigned intersectionCount = terrainManager.CalculateIntersections(
                intersections, dimof(intersections), worldSpaceRay, context, parserContext);

            if (intersectionCount > 0)
                return std::make_pair(intersections[0]._intersectionPoint, true);
        CATCH_ASSETS_END(parserContext)
        return std::make_pair(Float3(0,0,0), false);
    }

////////////////////////////////////////////////////////////////////////////////////////////////////////

    static std::pair<Float3, bool> FindTerrainIntersection(
        const IntersectionTestContext& intersectionContext,
		RenderCore::Techniques::ParsingContext& parsingContext,
        TerrainManager& terrainManager,
        std::pair<Float3, Float3> worldSpaceRay)
    {
            //  create a new device context and lighting parser context, and use
            //  this to find an accurate terrain collision.
        RenderCore::Metal::ViewportDesc newViewport {
            float(intersectionContext._viewportMins[0]), float(intersectionContext._viewportMins[1]),
			float(intersectionContext._viewportMaxs[0]-intersectionContext._viewportMins[0]),
			float(intersectionContext._viewportMaxs[1]-intersectionContext._viewportMins[1]),
			0.f, 1.f };
		auto& threadContext = *RenderCore::Techniques::GetThreadContext();
        RenderCore::Metal::DeviceContext::Get(threadContext)->Bind(newViewport);
		// We need to set the camera to get correct culling and lodding of the terrain during intersection tests
		parsingContext.GetProjectionDesc() = RenderCore::Techniques::BuildProjectionDesc(
			intersectionContext._cameraDesc,
			UInt2{unsigned(intersectionContext._viewportMaxs[0]-intersectionContext._viewportMins[0]), unsigned(intersectionContext._viewportMaxs[1]-intersectionContext._viewportMins[1])});
		return FindTerrainIntersection(threadContext, parsingContext, terrainManager, worldSpaceRay);
    }

    static std::vector<ModelIntersectionStateContext::ResultEntry> PlacementsIntersection(
        RenderCore::IThreadContext& threadContext, 
		RenderCore::Techniques::ParsingContext& parsingContext,
		ModelIntersectionStateContext& stateContext,
        SceneEngine::PlacementsRenderer& placementsRenderer, SceneEngine::PlacementCellSet& cellSet,
        SceneEngine::PlacementGUID object)
    {
            // Using the GPU, look for intersections between the ray
            // and the given model. Since we're using the GPU, we need to
            // get a device context. 
            //
            // We'll have to use the immediate context
            // because we want to get the result get right. But that means the
            // immediate context can't be doing anything else in another thread.
            //
            // This will require more complex threading support in the future!
        // assert(metalContext.IsImmediate());

            //  We need to invoke the render for the given object
            //  now. Afterwards we can query the buffers for the result
		
		{
			using namespace RenderCore;
			using namespace SceneEngine;
			SceneExecuteContext sceneExeContext;
			auto viewDelegate = std::make_shared<BasicViewDelegate>();
			sceneExeContext.AddView({SceneView::Type::Other, parsingContext.GetProjectionDesc()}, viewDelegate);
			placementsRenderer.BuildDrawables(
				sceneExeContext,
				cellSet, &object, &object+1);
			Techniques::Draw(
				threadContext, parsingContext, 
				stateContext.MakeRayTestSequencerTechnique(), 
				viewDelegate->_pkt);
		}

        return stateContext.GetResults();
    }

////////////////////////////////////////////////////////////////////////////////////////////////////////

    auto IntersectionTestScene::FirstRayIntersection(
        const IntersectionTestContext& context,
        std::pair<Float3, Float3> worldSpaceRay,
        Type::BitField filter) const -> Result
    {
        Result result;

		auto& threadContext = *RenderCore::Techniques::GetThreadContext();
		RenderCore::Techniques::ParsingContext parsingContext(*context._techniqueContext);
		parsingContext._pipelineAcceleratorPool = context._pipelineAcceleratorPool.get();

        if ((filter & Type::Terrain) && _terrainManager) {
            auto intersection = FindTerrainIntersection(
                context, parsingContext, *_terrainManager.get(), worldSpaceRay);
            if (intersection.second) {
                float distance = Magnitude(intersection.first - worldSpaceRay.first);
                if (distance < result._distance) {
                    result = Result();
                    result._type = Type::Terrain;
                    result._worldSpaceCollision = intersection.first;
                    result._distance = distance;
                }
            }
        }

        if ((filter & Type::Placement) && _placements && _placementsEditor) {
            auto intersections = _placementsEditor->GetManager()->GetIntersections();
            auto roughIntersection = 
                intersections->Find_RayIntersection(*_placements, worldSpaceRay.first, worldSpaceRay.second, nullptr);

            if (!roughIntersection.empty()) {

                    // we can improve the intersection by doing ray-vs-triangle tests
                    // on the roughIntersection geometry

                    //  we need to create a temporary transaction to get
                    //  at the information for these objects.
                auto trans = _placementsEditor->Transaction_Begin(
                    AsPointer(roughIntersection.cbegin()), AsPointer(roughIntersection.cend()));

                TRY
                {
                    float rayLength = Magnitude(worldSpaceRay.second - worldSpaceRay.first);
                    ModelIntersectionStateContext intersectionContext(
                        ModelIntersectionStateContext::RayTest,
                        threadContext, parsingContext, &context._cameraDesc);
                    intersectionContext.SetRay(worldSpaceRay);

                    // note --  we could do this all in a single render call, except that there
                    //          is no way to associate a low level intersection result with a specific
                    //          draw call.
                    auto count = trans->GetObjectCount();
                    for (unsigned c=0; c<count; ++c) {
                        auto guid = trans->GetGuid(c);
                        auto results = PlacementsIntersection(
                            threadContext, parsingContext, intersectionContext, 
                            *_placementsEditor->GetManager()->GetRenderer(), *_placements,
                            guid);

                        bool gotGoodResult = false;
                        unsigned drawCallIndex = 0;
                        uint64 materialGuid = 0;
                        float intersectionDistance = FLT_MAX;
                        for (auto i=results.cbegin(); i!=results.cend(); ++i) {
                            if (i->_intersectionDepth < intersectionDistance) {
                                intersectionDistance = i->_intersectionDepth;
                                drawCallIndex = i->_drawCallIndex;
                                materialGuid = i->_materialGuid;
                                gotGoodResult = true;
                            }
                        }

                        if (gotGoodResult && intersectionDistance < result._distance) {
                            result = Result();
                            result._type = Type::Placement;
                            result._worldSpaceCollision = LinearInterpolate(
                                worldSpaceRay.first, worldSpaceRay.second, 
                                intersectionDistance / rayLength);
                            result._distance = intersectionDistance;
                            result._objectGuid = guid;
                            result._drawCallIndex = drawCallIndex;
                            result._materialGuid = materialGuid;
                            result._materialName = trans->GetMaterialName(c, materialGuid);
                            result._modelName = trans->GetObject(c)._model;
                        }
                    }
                }
                CATCH(const ::Assets::Exceptions::RetrievalError&) {}
                CATCH_END

                trans->Cancel();
            }
        }

        unsigned firstExtraBit = IntegerLog2(uint32(Type::Extra));
        for (size_t c=0; c<_extraTesters.size(); ++c) {
            if (!(filter & 1<<uint32(c+firstExtraBit))) continue;
            TRY
            {
                auto res = _extraTesters[c]->FirstRayIntersection(context, worldSpaceRay);
                if (res._distance >= 0.f && res._distance <result._distance) {
                    result = res;
                    result._type = (Type::Enum)(1<<uint32(c+firstExtraBit));
                }
            } 
            CATCH(const ::Assets::Exceptions::RetrievalError&) {}
            CATCH_END
        }

        return result;
    }

    auto IntersectionTestScene::FrustumIntersection(
        const IntersectionTestContext& context,
        const Float4x4& worldToProjection,
        Type::BitField filter) const -> std::vector<Result>
    {
        std::vector<Result> result;

        auto& threadContext = *RenderCore::Techniques::GetThreadContext();

        if ((filter & Type::Placement) && _placements && _placementsEditor) {
            auto intersections = _placementsEditor->GetManager()->GetIntersections();
            auto roughIntersection = 
                intersections->Find_FrustumIntersection(*_placements, worldToProjection, nullptr);

                // we can improve the intersection by doing ray-vs-triangle tests
                // on the roughIntersection geometry

            if (!roughIntersection.empty()) {
                    //  we need to create a temporary transaction to get
                    //  at the information for these objects.
                auto trans = _placementsEditor->Transaction_Begin(
                    AsPointer(roughIntersection.cbegin()), AsPointer(roughIntersection.cend()));

                TRY
                {
					RenderCore::Techniques::ParsingContext parsingContext(*context._techniqueContext);
					parsingContext._pipelineAcceleratorPool = context._pipelineAcceleratorPool.get();
                    ModelIntersectionStateContext intersectionContext(
                        ModelIntersectionStateContext::FrustumTest,
                        threadContext, parsingContext, &context._cameraDesc);
                    intersectionContext.SetFrustum(worldToProjection);

                    // note --  we could do this all in a single render call, except that there
                    //          is no way to associate a low level intersection result with a specific
                    //          draw call.
                    auto count = trans->GetObjectCount();
                    for (unsigned c=0; c<count; ++c) {
                        
                            //  We only need to test the triangles if the bounding box is 
                            //  intersecting the edge of the frustum... If the entire bounding
                            //  box is within the frustum, then we must have a hit
                        auto boundary = trans->GetLocalBoundingBox(c);
                        auto boundaryTest = TestAABB(
                            Combine(trans->GetObject(c)._localToWorld, worldToProjection),
                            boundary.first, boundary.second, RenderCore::Techniques::GetDefaultClipSpaceType());
                        if (boundaryTest == AABBIntersection::Culled) continue; // (could happen because earlier tests were on the world space bounding box)

                        auto guid = trans->GetGuid(c);
                        
                        bool isInside = boundaryTest == AABBIntersection::Within;
                        if (!isInside) {
                            auto results = PlacementsIntersection(
                                threadContext, parsingContext, intersectionContext, 
                                *_placementsEditor->GetManager()->GetRenderer(), *_placements, guid);
                            isInside = !results.empty();
                        }

                        if (isInside) {
                            Result r;
                            r._type = Type::Placement;
                            r._worldSpaceCollision = Float3(0.f, 0.f, 0.f);
                            r._distance = 0.f;
                            r._objectGuid = guid;
                            result.push_back(r);
                        }
                    }
                } 
                CATCH(const ::Assets::Exceptions::RetrievalError&) {}
                CATCH_END

                trans->Cancel();
            }
        }

        unsigned firstExtraBit = IntegerLog2(uint32(Type::Extra));
        for (size_t c=0; c<_extraTesters.size(); ++c) {
            if (!(filter & 1<<uint32(c+firstExtraBit))) continue;
            _extraTesters[c]->FrustumIntersection(result, context, worldToProjection);
        }

        return result;
    }

    auto IntersectionTestScene::UnderCursor(
        const IntersectionTestContext& context,
        Int2 cursorPosition, Type::BitField filter) const -> Result
    {
        return FirstRayIntersection(
            context, context.CalculateWorldSpaceRay(cursorPosition), filter);
    }

    IntersectionTestScene::IntersectionTestScene(
        std::shared_ptr<TerrainManager> terrainManager,
        std::shared_ptr<PlacementCellSet> placements,
        std::shared_ptr<PlacementsEditor> placementsEditor,
        std::initializer_list<std::shared_ptr<IIntersectionTester>> extraTesters)
    : _terrainManager(std::move(terrainManager))
    , _placements(std::move(placements))
    , _placementsEditor(std::move(placementsEditor))
    {
        for (size_t c=0; c<extraTesters.size(); ++c) 
            _extraTesters.push_back(std::move(extraTesters.begin()[c]));
    }

    IntersectionTestScene::~IntersectionTestScene()
    {}

////////////////////////////////////////////////////////////////////////////////////////////////////////
    
    IIntersectionTester::~IIntersectionTester() {}
    
    static Float4x4 CalculateWorldToProjection(const RenderCore::Techniques::CameraDesc& sceneCamera, float viewportAspect)
    {
        auto projectionMatrix = RenderCore::Techniques::Projection(sceneCamera, viewportAspect);
        return Combine(InvertOrthonormalTransform(sceneCamera._cameraToWorld), projectionMatrix);
    }

    std::pair<Float3, Float3> IntersectionTestContext::CalculateWorldSpaceRay(
        const RenderCore::Techniques::CameraDesc& sceneCamera,
        Int2 screenCoord, UInt2 viewMins, UInt2 viewMaxs)
    {
		UInt2 viewportDims = viewMaxs - viewMins;
		assert(viewportDims[0] > 0 && viewportDims[1] > 0);	// expecting a non-empty viewport here, otherwise we'll get a divide by zero below
        auto worldToProjection = CalculateWorldToProjection(sceneCamera, viewportDims[0] / float(viewportDims[1]));

        Float3 frustumCorners[8];
        CalculateAbsFrustumCorners(frustumCorners, worldToProjection, RenderCore::Techniques::GetDefaultClipSpaceType());
        Float3 cameraPosition = ExtractTranslation(sceneCamera._cameraToWorld);

        return BuildRayUnderCursor(
            screenCoord, frustumCorners, cameraPosition, 
            sceneCamera._nearClip, sceneCamera._farClip,
            std::make_pair(viewMins, viewMaxs));
    }

////////////////////////////////////////////////////////////////////////////////////////////////////////
    
    std::pair<Float3, Float3> IntersectionTestContext::CalculateWorldSpaceRay(Int2 screenCoord) const
    {
		return CalculateWorldSpaceRay(_cameraDesc, screenCoord, _viewportMins, _viewportMaxs);
    }

    Float2 IntersectionTestContext::ProjectToScreenSpace(const Float3& worldSpaceCoord) const
    {
        Int2 viewport = _viewportMaxs - _viewportMins;
        auto worldToProjection = CalculateWorldToProjection(_cameraDesc, viewport[0] / float(viewport[1]));
        auto projCoords = worldToProjection * Expand(worldSpaceCoord, 1.f);

        return Float2(
            _viewportMins[0] + (projCoords[0] / projCoords[3] * 0.5f + 0.5f) * float(viewport[0]),
            _viewportMins[1] + (projCoords[1] / projCoords[3] * -0.5f + 0.5f) * float(viewport[1]));
    }

}

