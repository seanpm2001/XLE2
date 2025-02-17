// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "RenderingUtils.h"
#include "SceneEngineUtils.h"
#include "../RenderCore/Techniques/Techniques.h"
#include "../RenderCore/Techniques/CommonBindings.h"
#include "../RenderCore/Techniques/ParsingContext.h"
#include "../RenderCore/Metal/DeviceContext.h"
#include "../RenderCore/Metal/InputLayout.h"
#include "../RenderCore/Metal/Shader.h"
#include "../RenderCore/Metal/Buffer.h"
#include "../RenderCore/Format.h"
#include "../RenderCore/Types.h"
#include "../Assets/Assets.h"
#include "../Math/Transformations.h"
#include "../xleres/FileList.h"

namespace SceneEngine
{
    void DrawBasisAxes(RenderCore::IThreadContext& context, RenderCore::Techniques::ParsingContext& parserContext, const Float3& offset)
    {
        CATCH_ASSETS_BEGIN

                //
                //      Draw world space X, Y, Z axes (to make it easier to see what's going on)
                //

            class Vertex
            {
            public:
                Float3      _position;
                unsigned    _color;
            };

            const float     size = 100.f;
            const Vertex    vertices[] = 
            {
                {   offset + Float3(0.f,     0.f,     0.f),  0xff0000ff },
                {   offset + Float3(size,    0.f,     0.f),  0xff0000ff },
                {   offset + Float3(0.f,     0.f,     0.f),  0xff00ff00 },
                {   offset + Float3(0.f,    size,     0.f),  0xff00ff00 },
                {   offset + Float3(0.f,     0.f,     0.f),  0xffff0000 },
                {   offset + Float3(0.f,     0.f,    size),  0xffff0000 }
            };

            using namespace RenderCore;
            using namespace RenderCore::Metal;
            InputElementDesc vertexInputLayout[] = {
                InputElementDesc( "POSITION", 0, Format::R32G32B32_FLOAT ),
                InputElementDesc( "COLOR", 0, Format::R8G8B8A8_UNORM )
            };

            auto vertexBuffer = MakeMetalVB(vertices, sizeof(vertices));

            using namespace RenderCore::Metal;
            using namespace RenderCore::Techniques;
            ConstantBufferView constantBufferPackets[2];
            constantBufferPackets[0] = MakeLocalTransformPacket(Identity<Float4x4>(), ExtractTranslation(parserContext.GetProjectionDesc()._cameraToWorld));

			auto& metalContext = *Metal::DeviceContext::Get(context);

            const auto& shaderProgram = ::Assets::GetAsset<ShaderProgram>(
                NO_PATCHES_VERTEX_HLSL ":main:" VS_DefShaderModel, 
                NO_PATCHES_PIXEL_HLSL ":forward", 
                "GEO_HAS_COLOR=1");
            BoundInputLayout boundVertexInputLayout(MakeIteratorRange(vertexInputLayout), shaderProgram);
			VertexBufferView vbvs[] = {&vertexBuffer};
			boundVertexInputLayout.Apply(metalContext, MakeIteratorRange(vbvs));
            metalContext.Bind(shaderProgram);

			UniformsStreamInterface usi;
			usi.BindConstantBuffer(0, {ObjectCB::LocalTransform});
			BoundUniforms boundLayout(
				shaderProgram,
				Metal::PipelineLayoutConfig{},
				TechniqueContext::GetGlobalUniformsStreamInterface(),
				usi);
            boundLayout.Apply(metalContext, 0, parserContext.GetGlobalUniformsStream());
			boundLayout.Apply(metalContext, 1, UniformsStream{MakeIteratorRange(constantBufferPackets)});

            metalContext.Bind(Topology::LineList);
            metalContext.Draw(dimof(vertices));

        CATCH_ASSETS_END(parserContext)
    }
}

