// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "InputLayout.h"
#include "DeviceContext.h"
#include "Shader.h"
#include "Format.h"
#include "TextureView.h"
#include "PipelineLayout.h"
#include "State.h"
#include "Resource.h"
#include "../../Types.h"
#include "../../Format.h"
#include "../../BufferView.h"
#include "../../../ConsoleRig/Log.h"
#include "../../../ConsoleRig/LogUtil.h"
#include "../../../Utility/StringUtils.h"
#include "../../../Utility/StringFormat.h"
#include "../../../Utility/PtrUtils.h"
#include "../../../Utility/ArithmeticUtils.h"
#include <map>
#include <exception>

#include "IncludeAppleMetal.h"

namespace RenderCore { namespace Metal_AppleMetal
{
    MTLVertexFormat AsMTLVertexFormat(RenderCore::Format fmt);

    static const Resource& GetResource(const IResource* rp)
    {
        static Resource dummy;
        auto* res = (Resource*)const_cast<IResource*>(rp)->QueryInterface(typeid(Resource).hash_code());
        if (res)
            return *res;
        return dummy;
    }

    void BoundVertexBuffers::Apply(DeviceContext& context, IteratorRange<const VertexBufferView*> vertexBuffers) const never_throws
    {
        unsigned i = 0;
        for (const auto& vbv : vertexBuffers) {
            id<MTLBuffer> buffer = GetResource(vbv._resource).GetBuffer();
            if (!buffer)
                Throw(::Exceptions::BasicLabel("Attempting to apply vertex buffer view with invalid resource"));
            context.BindVS(buffer, vbv._offset, i);
            ++i;
        }
    }

    void BoundInputLayout::Apply(DeviceContext& context, IteratorRange<const VertexBufferView*> vertexBuffers) const never_throws
    {
        context.SetInputLayout(*this);
        _boundVertexBuffers.Apply(context, vertexBuffers);
    }

    /* KenD -- cleanup TODO -- this was copied from LightWeightModel */
    static uint64 BuildSemanticHash(const char semantic[])
    {
        // strip off digits on the end of the string (these are optionally included and are used as
        // the semantic index)
        auto len = std::strlen(semantic);
        while (len > 0 && std::isdigit(semantic[len-1])) len--;
        uint64_t hash = Hash64(semantic, &semantic[len]);
        hash += std::atoi(&semantic[len]);
        return hash;
    }

    static uint64_t MakeHash(MTLVertexAttributeDescriptor* vertexAttribute)
    {
        // assert(vertexAttribute.format <= 0xffff); (always true)
        assert(vertexAttribute.bufferIndex <= 0xffff);
        assert(vertexAttribute.offset <= 0xffffffff);
        return uint64_t(vertexAttribute.format)
            | (uint64_t(vertexAttribute.bufferIndex) << 16)
            | (uint64_t(vertexAttribute.offset) << 32);
    }

    static uint64_t MakeHash(MTLVertexBufferLayoutDescriptor* bufferLayout)
    {
        assert(bufferLayout.stride <= 0xffff);
        // assert(bufferLayout.stepFunction <= 0xffff); (always true)
        assert(bufferLayout.stepRate <= 0xffffffff);
        return uint64_t(bufferLayout.stride)
            | (uint64_t(bufferLayout.stepFunction) << 16)
            | (uint64_t(bufferLayout.stepRate) << 32);
    }

    BoundInputLayout::BoundInputLayout(IteratorRange<const SlotBinding*> layouts, const ShaderProgram& program)
    {
        /* KenD -- Metal TODO -- validate the layout of vertex data.
         * The MTLVertexFormat for some attributes may be a float4 although the native format
         * (RenderCore format) is only a float3.
         * In the shader, the attribute should be a float3.
         * It will still be correctly accessed in the shader (because the offset is specified),
         * but frame capture will show those attributes as float4s with some overlap.
         *
         * For the bufferIndex, we are currently just using the index of the layout.
         * That works, provided that the input to the shader function does not use those same
         * buffers for other data.
         *
         * Validate the input layout by getting the function arguments (via reflection) and comparing
         * them with the input layout.  Ensure that the offsets of elements in the input layout
         * match the offset of the corresponding elements in the shader arguments.
         */

        // Create a MTLVertexDescriptor to describe the input format for vertices
        _vertexDescriptor = TBC::OCPtr<MTLVertexDescriptor>(TBC::moveptr([[MTLVertexDescriptor alloc] init]));
        auto* desc = _vertexDescriptor.get();

        // Map each vertex attribute's semantic hash to its attribute index
        id<MTLFunction> vf = program._vf.get();

        _hash = DefaultSeed64;

        std::map<uint64_t, unsigned> hashToLocation;
        _allAttributesBound = true;
        for (unsigned a=0; a<vf.vertexAttributes.count; ++a) {
            if (!vf.vertexAttributes[a].active)
                continue;

            auto hash = BuildSemanticHash(vf.vertexAttributes[a].name.UTF8String);

            bool foundBinding = false;
            for (unsigned l=0; l < layouts.size() && !foundBinding; ++l) {
                for (const auto& e : layouts[l]._elements) {
                    if (e._semanticHash != hash)
                        continue;

                    auto attributeIdx = vf.vertexAttributes[a].attributeIndex;
                    desc.attributes[attributeIdx].bufferIndex = l;
                    desc.attributes[attributeIdx].format = AsMTLVertexFormat(e._nativeFormat);
                    desc.attributes[attributeIdx].offset = CalculateVertexStride(MakeIteratorRange(layouts[l]._elements.begin(), &e), false);
                    foundBinding = true;

                    _hash = HashCombine(_hash, MakeHash(desc.attributes[attributeIdx]));
                    _hash = HashCombine(_hash, attributeIdx);
                    break;
                }
            }

            if (!foundBinding) {
                _allAttributesBound = false;
            }
        }

        for (unsigned l=0; l < layouts.size(); ++l) {
            desc.layouts[l].stride = CalculateVertexStride(layouts[l]._elements, false);
            if (layouts[l]._instanceStepDataRate == 0) {
                desc.layouts[l].stepFunction = MTLVertexStepFunctionPerVertex;
            } else {
                desc.layouts[l].stepFunction = MTLVertexStepFunctionPerInstance;
                desc.layouts[l].stepRate = layouts[l]._instanceStepDataRate;
            }

            _hash = HashCombine(_hash, MakeHash(desc.layouts[l]));
        }

        _hash = HashCombine(_hash, _allAttributesBound);

        #if defined(_DEBUG)
            if (!_allAttributesBound) {
                Log(Warning) << "Some attributes not bound for vertex shader:" << [program._vf.get() label] << std::endl;
                Log(Warning) << "Attributes on shader: " << std::endl;
                for (unsigned a=0; a<vf.vertexAttributes.count; ++a) {
                    Log(Warning) << "  [" << vf.vertexAttributes[a].attributeIndex << "] " << vf.vertexAttributes[a].name << std::endl;
                }
                Log(Warning) << "Attributes on provided from input layout: " << std::endl;
                for (unsigned l=0; l < layouts.size(); ++l) {
                    for (unsigned e=0; e < layouts[l]._elements.size(); ++e) {
                        Log(Warning) << "  [" << l << ", " << e << "] 0x" << std::hex << layouts[l]._elements[e]._semanticHash << std::dec << std::endl;
                    }
                }
            }
        #endif
    }

    BoundInputLayout::BoundInputLayout(IteratorRange<const InputElementDesc*> layout, const ShaderProgram& program)
    {
        id<MTLFunction> vf = program._vf.get();

        // Create a MTLVertexDescriptor to describe the input format for vertices
        _vertexDescriptor = TBC::OCPtr<MTLVertexDescriptor>(TBC::moveptr([[MTLVertexDescriptor alloc] init]));
        auto* desc = _vertexDescriptor.get();

        unsigned maxSlot = 0;
        for (const auto& e:layout)
            maxSlot = std::max(maxSlot, e._inputSlot);

        std::vector<unsigned> boundAttributes;
        boundAttributes.reserve(layout.size());

        std::vector<uint64_t> attributeHashes;
        attributeHashes.reserve(vf.vertexAttributes.count);
        for (MTLVertexAttribute* vertexAttribute in vf.vertexAttributes)
            attributeHashes.push_back(BuildSemanticHash(vertexAttribute.name.UTF8String));

        _hash = DefaultSeed64;

        // Populate MTLVertexAttributeDescriptorArray
        for (unsigned slot=0; slot<maxSlot+1; ++slot) {
            unsigned workingStride = 0;

            unsigned inputDataRate = ~unsigned(0x0);
            unsigned inputStepFunction = ~unsigned(0x0);

            for (const auto& e:layout) {
                if (e._inputSlot != slot) continue;

                unsigned alignedOffset = e._alignedByteOffset;
                if (alignedOffset == ~unsigned(0x0)) {
                    alignedOffset = workingStride;
                }

                auto hash = BuildSemanticHash(e._semanticName.c_str()) + e._semanticIndex;

                auto i = std::find(attributeHashes.begin(), attributeHashes.end(), hash);
                if (i != attributeHashes.end()) {
                    auto* matchingAttribute = vf.vertexAttributes[std::distance(attributeHashes.begin(), i)];
                    if (matchingAttribute.active) {
                        auto attributeLoc = matchingAttribute.attributeIndex;
                        desc.attributes[attributeLoc].bufferIndex = e._inputSlot;
                        desc.attributes[attributeLoc].format = AsMTLVertexFormat(e._nativeFormat);
                        desc.attributes[attributeLoc].offset = alignedOffset;

                        _hash = HashCombine(_hash, MakeHash(desc.attributes[attributeLoc]));
                        _hash = HashCombine(_hash, attributeLoc);

                        // You will hit this assert if we attempt to bind the same attribute more
                        // than once
                        assert(std::find(boundAttributes.begin(), boundAttributes.end(), attributeLoc) == boundAttributes.end());
                        boundAttributes.push_back((unsigned)attributeLoc);
                    }
                }

                workingStride = alignedOffset + BitsPerPixel(e._nativeFormat) / 8;

                if (inputDataRate != ~unsigned(0x0) && e._instanceDataStepRate != inputDataRate)
                    Throw(std::runtime_error("Cannot create InputLayout because step rate not consistant across input slot"));
                if (inputStepFunction != ~unsigned(0x0) && unsigned(e._inputSlotClass) != inputStepFunction)
                    Throw(std::runtime_error("Cannot create InputLayout because step function not consistant across input slot"));
                inputDataRate = e._instanceDataStepRate;
                inputStepFunction = (unsigned)e._inputSlotClass;
            }

            if (inputDataRate == ~unsigned(0x0) && inputStepFunction == ~unsigned(0x0))
                continue;

            // Populate MTLVertexBufferLayoutDescriptorArray
            desc.layouts[slot].stride = CalculateVertexStrideForSlot(layout, slot);
            if (inputStepFunction == (unsigned)InputDataRate::PerVertex) {
                desc.layouts[slot].stepFunction = MTLVertexStepFunctionPerVertex;
            } else {
                desc.layouts[slot].stepFunction = MTLVertexStepFunctionPerInstance;
                desc.layouts[slot].stepRate = inputDataRate;
            }

            _hash = HashCombine(_hash, MakeHash(desc.layouts[slot]));
        }

        _allAttributesBound = true;
        for (MTLVertexAttribute* vertexAttribute in vf.vertexAttributes) {
            if (!vertexAttribute.active)
                continue;

            if (std::find(boundAttributes.begin(), boundAttributes.end(), vertexAttribute.attributeIndex) == boundAttributes.end()) {
                _allAttributesBound = false;
                break;
            }
        }

        _hash = HashCombine(_hash, _allAttributesBound);

        #if defined(_DEBUG)
            if (!_allAttributesBound) {
                Log(Warning) << "Some attributes not bound for vertex shader:" << [program._vf.get() label] << std::endl;
                Log(Warning) << "Attributes on shader: " << std::endl;
                for (unsigned a=0; a<vf.vertexAttributes.count; ++a) {
                    Log(Warning) << "  [" << vf.vertexAttributes[a].attributeIndex << "] " << vf.vertexAttributes[a].name << std::endl;
                }
                Log(Warning) << "Attributes on provided from input layout: " << std::endl;
                for (const auto&e:layout) {
                    Log(Warning) << "  [" << e._inputSlot << "] " << e._semanticName << " (" << e._semanticIndex << ")" << std::endl;
                }
            }
        #endif
    }

    BoundInputLayout::BoundInputLayout() {}

////////////////////////////////////////////////////////////////////////////////////////////////////

    static bool HasCBBinding(IteratorRange<const UniformsStreamInterface**> interfaces, uint64_t hash)
    {
        for (auto interf:interfaces) {
            auto i = std::find_if(
                interf->_cbBindings.begin(), interf->_cbBindings.end(),
                [hash](const UniformsStreamInterface::RetainedCBBinding& b) {
                    return b._hashName == hash;
                });
            if (i!=interf->_cbBindings.end())
                return true;
        }
        return false;
    }

    static bool HasSRVBinding(IteratorRange<const UniformsStreamInterface**> interfaces, uint64_t hash)
    {
        for (auto interf:interfaces) {
            auto i = std::find(interf->_srvBindings.begin(), interf->_srvBindings.end(), hash);
            if (i!=interf->_srvBindings.end())
                return true;
        }
        return false;
    }

    static bool HasCBBinding(IteratorRange<const UniformsStreamInterface*> interfaces, uint64_t hash)
    {
        for (const auto& interf:interfaces) {
            auto i = std::find_if(
                interf._cbBindings.begin(), interf._cbBindings.end(),
                [hash](const UniformsStreamInterface::RetainedCBBinding& b) {
                    return b._hashName == hash;
                });
            if (i!=interf._cbBindings.end())
                return true;
        }
        return false;
    }

    static bool HasSRVBinding(IteratorRange<const UniformsStreamInterface*> interfaces, uint64_t hash)
    {
        for (const auto& interf:interfaces) {
            auto i = std::find(interf._srvBindings.begin(), interf._srvBindings.end(), hash);
            if (i!=interf._srvBindings.end())
                return true;
        }
        return false;
    }

////////////////////////////////////////////////////////////////////////////////////////////////////

    #if defined(_DEBUG)
        static void ValidateCBElements(
            IteratorRange<const ConstantBufferElementDesc*> elements,
            MTLStructType* structReflection)
        {
            // Every member of the struct must be in the "elements", and offsets and types must match
            for (MTLStructMember* member in structReflection.members) {
                if (member.arrayType) {
                } else {
                    auto hashName = Hash64(member.name.UTF8String);
                    auto i = std::find_if(
                        elements.begin(), elements.end(),
                        [hashName](const ConstantBufferElementDesc& t) { return t._semanticHash == hashName; });
                    if (i == elements.end())
                        Throw(::Exceptions::BasicLabel("Missing CB binding for element name (%s)", member.name.UTF8String));

                    if (i->_offset != member.offset)
                        Throw(::Exceptions::BasicLabel("CB element offset is incorrect for member (%s). It's (%i) in the shader, but (%i) in the binding provided",
                            member.name.UTF8String, member.offset, i->_offset));

                    auto f = AsFormat(AsTypeDesc(member.dataType));
                    if (i->_nativeFormat != f)
                        Throw(::Exceptions::BasicLabel("CB element type is incorrect for member (%s). It's (%s) in the shader, but (%s) in the binding provided",
                                member.name.UTF8String, AsString(f), AsString(i->_nativeFormat)));
                }
            }
        }
    #endif

////////////////////////////////////////////////////////////////////////////////////////////////////

    static StreamMapping MakeStreamMapping(
        MTLRenderPipelineReflection* reflection,
        unsigned streamIndex,
        const UniformsStreamInterface** interfaces,      // expecting exactly 4 entries
        ShaderStage stage)
    {
        assert(streamIndex < 4);
        assert(stage == ShaderStage::Vertex || stage == ShaderStage::Pixel);
        NSArray <MTLArgument *>* arguments = (stage == ShaderStage::Vertex) ? reflection.vertexArguments : reflection.fragmentArguments;
        StreamMapping result;

        auto argCount = arguments.count;
        for (unsigned argIdx=0; argIdx<argCount; ++argIdx) {
            MTLArgument* arg = arguments[argIdx];
            if (!arg.active)
                continue;

            const char* argName = [arg.name cStringUsingEncoding:NSUTF8StringEncoding];
            auto argHash = Hash64(argName);

            // Look for matching input in our interface
            if (arg.type == MTLArgumentTypeTexture) {

                unsigned matchingSlot = ~0u;
                for (unsigned c=0; c<(unsigned)interfaces[streamIndex]->_srvBindings.size(); ++c) {
                    if (interfaces[streamIndex]->_srvBindings[c] == argHash) {
                        matchingSlot = c;
                        break;
                    }
                }

                if (matchingSlot != ~0u) {
                    // look for a binding in a later stream interface
                    if (!HasSRVBinding(MakeIteratorRange(&interfaces[streamIndex+1], &interfaces[4]), argHash)) {
                        result._srvs.push_back({
                            matchingSlot,
                            (unsigned)arg.index,
                            (unsigned)arg.textureType,
                            (bool)arg.isDepthTexture
                            #if defined(_DEBUG)
                                , argName
                            #endif
                        });
                        assert(argIdx < 64);
                        result._boundArgs |= 1<<uint64_t(argIdx);
                        result._boundSRVSlots |= 1<<uint64_t(matchingSlot);
                    }
                }

            } else if (arg.type == MTLArgumentTypeSampler) {

                // We're expecting samplers to have the same name as the textures they apply to,
                // except with the "_sampler" postfix.
                // This is because the srv and sampler arrays are bound in parallel. There is one
                // binding name that applies to both. The texture and the sampler can't have the same
                // name in the shader, though, so we append "_sampler".
                // This allows us to conveniently support the OGL style combined texture/sampler
                // inputs, as well as the alternative separated texture/samplers design.
                auto range = [arg.name rangeOfString:@"_sampler"];
                if (range.location != NSNotFound && range.location == arg.name.length - range.length) {
                    argHash = Hash64([[arg.name substringToIndex:range.location] cStringUsingEncoding:NSUTF8StringEncoding]);
                }

                unsigned matchingSlot = ~0u;
                for (unsigned c=0; c<(unsigned)interfaces[streamIndex]->_srvBindings.size(); ++c) {
                    if (interfaces[streamIndex]->_srvBindings[c] == argHash) {
                        matchingSlot = c;
                        break;
                    }
                }

                if (matchingSlot != ~0u) {
                    // look for a binding in a later stream interface
                    if (!HasSRVBinding(MakeIteratorRange(&interfaces[streamIndex+1], &interfaces[4]), argHash)) {
                        result._samplers.push_back({
                            matchingSlot, (unsigned)arg.index
                            #if defined(_DEBUG)
                                , argName
                            #endif
                        });
                        assert(argIdx < 64);
                        result._boundArgs |= 1<<uint64_t(argIdx);
                    }
                }

            } else if (arg.type == MTLArgumentTypeBuffer) {

                unsigned matchingSlot = ~0u;
                for (unsigned c=0; c<(unsigned)interfaces[streamIndex]->_cbBindings.size(); ++c) {
                    if (interfaces[streamIndex]->_cbBindings[c]._hashName == argHash) {

                        #if defined(_DEBUG)
                            if (arg.bufferStructType) {
                                ValidateCBElements(
                                    MakeIteratorRange(interfaces[streamIndex]->_cbBindings[c]._elements),
                                    arg.bufferStructType);
                            }
                        #endif

                        matchingSlot = c;
                        break;
                    }
                }

                if (matchingSlot != ~0u) {
                    // look for a binding in a later stream interface
                    if (!HasCBBinding(MakeIteratorRange(&interfaces[streamIndex+1], &interfaces[4]), argHash)) {
                        result._cbs.push_back({
                            matchingSlot, (unsigned)arg.index,
                            (unsigned)arg.bufferDataSize
                            #if defined(_DEBUG)
                                , argName
                            #endif
                        });
                        assert(argIdx < 64);
                        result._boundArgs |= 1<<uint64_t(argIdx);
                        result._boundCBSlots |= 1<<uint64_t(matchingSlot);
                    }
                }

            }

        }

        return result;
    }

    static void ApplyUniformStreamVS(
        DeviceContext& context, const UniformsStream& stream,
        const StreamMapping& streamMapping)
    {
        id<MTLRenderCommandEncoder> encoder = context.GetCommandEncoder();

        for (const auto& b:streamMapping._cbs) {
            assert(b._uniformStreamSlot < stream._constantBuffers.size());
            const auto& constantBuffer = stream._constantBuffers[b._uniformStreamSlot];
            const auto& pkt = constantBuffer._packet;
            if (!pkt.size()) {
                #if defined(_DEBUG)
                    assert(constantBuffer._prebuiltBuffer);
                    auto bufferDesc = constantBuffer._prebuiltBuffer->GetDesc();
                    assert(bufferDesc._type == ResourceDesc::Type::LinearBuffer);
                    assert(bufferDesc._linearBufferDesc._sizeInBytes >= b._cbSize);
                #endif
                [encoder setVertexBuffer:checked_cast<const Resource*>(constantBuffer._prebuiltBuffer)->GetBuffer().get()
                                  offset:constantBuffer._prebuiltRangeBegin
                                 atIndex:b._shaderSlot];
            } else {
                if (pkt.size() < b._cbSize) {
                    char extendedBuffer[b._cbSize];
                    std::memcpy(extendedBuffer, pkt.get(), pkt.size());
                    std::memset(PtrAdd(extendedBuffer, pkt.size()), 0, b._cbSize-pkt.size());
                    [encoder setVertexBytes:extendedBuffer length:b._cbSize atIndex:b._shaderSlot];
                } else {
                    [encoder setVertexBytes:pkt.get() length:(unsigned)pkt.size() atIndex:b._shaderSlot];
                }
            }
        }

        for (const auto& b:streamMapping._srvs) {
            const auto& shaderResource = *(const ShaderResourceView*)stream._resources[b._uniformStreamSlot];
            if (!shaderResource.IsGood()) {
                #if defined(_DEBUG)
                    Log(Verbose) << "==================> ShaderResource is bad/invalid while binding shader sampler (" << b._name << ")" << std::endl;
                #endif
            } else {
                const auto& texture = shaderResource.GetUnderlying();
                [encoder setVertexTexture:texture.get() atIndex:b._shaderSlot];
            }
        }

        for (const auto& b:streamMapping._samplers) {
            const auto& sampler = *(const SamplerState*)stream._samplers[b._uniformStreamSlot];
            // Note -- assuming parallel sampler / srv arrays
            const auto& shaderResource = *(const ShaderResourceView*)stream._resources[b._uniformStreamSlot];
            sampler.Apply(context, shaderResource.HasMipMaps(), b._shaderSlot, ShaderStage::Vertex);
        }
    }

    static void ApplyUniformStreamPS(
        DeviceContext& context, const UniformsStream& stream,
        const StreamMapping& streamMapping)
    {
        id<MTLRenderCommandEncoder> encoder = context.GetCommandEncoder();

        for (const auto& b:streamMapping._cbs) {
            #if defined(_DEBUG)
                if (b._uniformStreamSlot >= stream._constantBuffers.size())
                    Throw(::Exceptions::BasicLabel("Uniform stream does not include Constant Buffer for bound resource. Expected CB bound at index (%u) of stream (%s). Only (%u) CBs were provided in the UniformsStream passed to BoundUniforms::Apply", b._uniformStreamSlot, b._name.c_str(), stream._constantBuffers.size()));
            #endif

            const auto& constantBuffer = stream._constantBuffers[b._uniformStreamSlot];
            const auto& pkt = constantBuffer._packet;
            if (!pkt.size()) {
                #if defined(_DEBUG)
                    assert(constantBuffer._prebuiltBuffer);
                    auto bufferDesc = constantBuffer._prebuiltBuffer->GetDesc();
                    assert(bufferDesc._type == ResourceDesc::Type::LinearBuffer);
                    assert(bufferDesc._linearBufferDesc._sizeInBytes >= b._cbSize);
                #endif
                [encoder setFragmentBuffer:checked_cast<const Resource*>(constantBuffer._prebuiltBuffer)->GetBuffer().get()
                                  offset:constantBuffer._prebuiltRangeBegin
                                 atIndex:b._shaderSlot];
            } else {
                if (pkt.size() < b._cbSize) {
                    char extendedBuffer[b._cbSize];
                    std::memcpy(extendedBuffer, pkt.get(), pkt.size());
                    std::memset(PtrAdd(extendedBuffer, pkt.size()), 0, b._cbSize-pkt.size());
                    [encoder setFragmentBytes:extendedBuffer length:b._cbSize atIndex:b._shaderSlot];
                } else {
                    [encoder setFragmentBytes:pkt.get() length:(unsigned)pkt.size() atIndex:b._shaderSlot];
                }
            }
        }

        for (const auto& b:streamMapping._srvs) {
            #if defined(_DEBUG)
                if (b._uniformStreamSlot >= stream._resources.size())
                    Throw(::Exceptions::BasicLabel("Uniform stream does not include SRV for bound resource. Expected SRV bound at index (%u) of stream (%s). Only (%u) SRVs were provided in the UniformsStream passed to BoundUniforms::Apply", b._uniformStreamSlot, b._name.c_str(), stream._resources.size()));
            #endif

            const auto& shaderResource = *(const ShaderResourceView*)stream._resources[b._uniformStreamSlot];
            if (!shaderResource.IsGood()) {
                [encoder setFragmentTexture:GetObjectFactory().GetStandInTexture(b._textureType, b._isDepth).get() atIndex:b._shaderSlot];
                #if defined(_DEBUG)
                    Log(Verbose) << "==================> ShaderResource is bad/invalid while binding shader sampler (" << b._name << ")" << std::endl;
                #endif
            } else {
                const auto& texture = shaderResource.GetUnderlying();
                [encoder setFragmentTexture:texture.get() atIndex:b._shaderSlot];
            }
        }

        for (const auto& b:streamMapping._samplers) {
            const auto& sampler = *(const SamplerState*)stream._samplers[b._uniformStreamSlot];
            // Note -- assuming parallel sampler / srv arrays
            const auto& shaderResource = *(const ShaderResourceView*)stream._resources[b._uniformStreamSlot];
            sampler.Apply(context, shaderResource.HasMipMaps(), b._shaderSlot, ShaderStage::Pixel);
        }
    }

    void BoundUniforms::Apply(DeviceContext& context, unsigned streamIdx, const UniformsStream& stream) const
    {
        if (_unboundInterface) {
            context.QueueUniformSet(_unboundInterface, streamIdx, stream);
            return;
        }

        assert(streamIdx < dimof(_preboundInterfaceVS));
        ApplyUniformStreamVS(context, stream, _preboundInterfaceVS[streamIdx]);
        ApplyUniformStreamPS(context, stream, _preboundInterfacePS[streamIdx]);

        if (streamIdx == 0) {
            auto* encoder = context.GetCommandEncoder();
            for (const auto& b:_unbound2DSRVs) {
                const AplMtlTexture* texture = GetObjectFactory().GetStandInTexture((unsigned)MTLTextureType2D, std::get<2>(b)).get();
                if (std::get<0>(b) == ShaderStage::Vertex) {
                    [encoder setVertexTexture:texture atIndex: std::get<1>(b)];
                } else
                    [encoder setFragmentTexture:texture atIndex: std::get<1>(b)];
            }
            for (const auto& b:_unboundCubeSRVs) {
                const AplMtlTexture* cubeTexture = GetObjectFactory().StandInCubeTexture().get();
                if (b.first == ShaderStage::Vertex) {
                    [encoder setVertexTexture:cubeTexture atIndex:b.second];
                } else
                    [encoder setFragmentTexture:cubeTexture atIndex:b.second];
            }
            for (const auto& b:_unboundSamplers) {
                const AplMtlSamplerState* samplerState = GetObjectFactory().StandInSamplerState();
                if (std::get<0>(b) == ShaderStage::Vertex) {
                    [encoder setVertexSamplerState:samplerState atIndex:std::get<1>(b)];
                } else
                    [encoder setFragmentSamplerState:samplerState atIndex:std::get<1>(b)];
            }
        }
    }

    BoundUniforms::BoundArguments BoundUniforms::Apply_UnboundInterfacePath(
        DeviceContext& context,
        MTLRenderPipelineReflection* pipelineReflection,
        const UnboundInterface& unboundInterface,
        unsigned streamIdx,
        const UniformsStream& stream)
    {
        const UniformsStreamInterface* interfaces[] = { &unboundInterface._interface[0], &unboundInterface._interface[1], &unboundInterface._interface[2], &unboundInterface._interface[3] };
        auto bindingVS = MakeStreamMapping(pipelineReflection, streamIdx, interfaces, ShaderStage::Vertex);
        ApplyUniformStreamVS(context, stream, bindingVS);
        auto bindingPS = MakeStreamMapping(pipelineReflection, streamIdx, interfaces, ShaderStage::Pixel);
        ApplyUniformStreamPS(context, stream, bindingPS);

        return { bindingVS._boundArgs, bindingPS._boundArgs };
    }

    void BoundUniforms::Apply_Standins(
        DeviceContext& context,
        MTLRenderPipelineReflection* pipelineReflection,
        uint64_t vsArguments, uint64_t psArguments)
    {
        id<MTLRenderCommandEncoder> encoder = context.GetCommandEncoder();
        auto* vsArgs = pipelineReflection.vertexArguments;

        unsigned vsArgCount = std::min(64u - xl_clz8(vsArguments), (unsigned)vsArgs.count);
        for (unsigned argIdx=0; argIdx<vsArgCount; ++argIdx) {
            MTLArgument* arg = vsArgs[argIdx];
            if (!arg.active || !(vsArguments & (1<<uint64_t(argIdx))))
                continue;

            if (arg.type == MTLArgumentTypeTexture) {
                [encoder setVertexTexture:GetObjectFactory().GetStandInTexture((unsigned)arg.textureType, (bool)arg.isDepthTexture).get()
                                  atIndex:arg.index];
            } else if (arg.type == MTLArgumentTypeSampler) {
                [encoder setVertexSamplerState:GetObjectFactory().StandInSamplerState().get() atIndex:arg.index];
            }
        }

        auto* psArgs = pipelineReflection.fragmentArguments;

        unsigned psArgCount = std::min(64u - xl_clz8(psArguments), (unsigned)psArgs.count);
        for (unsigned argIdx=0; argIdx<psArgCount; ++argIdx) {
            MTLArgument* arg = psArgs[argIdx];
            if (!arg.active || !(psArguments & (1<<uint64_t(argIdx))))
                continue;

            if (arg.type == MTLArgumentTypeTexture) {
                [encoder setFragmentTexture:GetObjectFactory().GetStandInTexture((unsigned)arg.textureType, (bool)arg.isDepthTexture).get()
                                    atIndex:arg.index];
            } else if (arg.type == MTLArgumentTypeSampler) {
                [encoder setFragmentSamplerState:GetObjectFactory().StandInSamplerState().get() atIndex:arg.index];
            }
        }
    }

    BoundUniforms::BoundUniforms(
        const ShaderProgram& shader,
        const PipelineLayoutConfig& pipelineLayout,
        const UniformsStreamInterface& interface0,
        const UniformsStreamInterface& interface1,
        const UniformsStreamInterface& interface2,
        const UniformsStreamInterface& interface3)
    {
        for (auto& s : _boundUniformBufferSlots) s = 0ull;
        for (auto& s : _boundResourceSlots) s = 0ull;

        const UniformsStreamInterface* interfaces[] = { &interface0, &interface1, &interface2, &interface3 };
        auto streamCount = dimof(interfaces);
        for (unsigned s=0; s < streamCount; ++s) {
            const auto& interface = *interfaces[s];

            for (unsigned slot = 0; slot < interface._cbBindings.size(); ++slot) {
                const auto& cbBinding = interface._cbBindings[slot];

                // Skip if future binding should take precedence
                if (HasCBBinding(MakeIteratorRange(&interfaces[s+1], &interfaces[streamCount]), cbBinding._hashName))
                    continue;
                _boundUniformBufferSlots[s] |= (1ull << uint64_t(slot));
            }

            for (unsigned slot=0; slot < interface._srvBindings.size(); ++slot) {
                auto& srvBinding = interface._srvBindings[slot];

                // Skip if future binding should take precedence
                if (HasSRVBinding(MakeIteratorRange(&interfaces[s+1], &interfaces[streamCount]), srvBinding))
                    continue;
                _boundResourceSlots[s] |= (1ull << uint64_t(slot));
            }
        }

        _unboundInterface = std::make_unique<UnboundInterface>();
        _unboundInterface->_interface[0] = interface0;
        _unboundInterface->_interface[1] = interface1;
        _unboundInterface->_interface[2] = interface2;
        _unboundInterface->_interface[3] = interface3;
    }

    BoundUniforms::BoundUniforms(
        const GraphicsPipeline& pipeline,
        const PipelineLayoutConfig& pipelineLayout,
        const UniformsStreamInterface& interface0,
        const UniformsStreamInterface& interface1,
        const UniformsStreamInterface& interface2,
        const UniformsStreamInterface& interface3)
    {
        const UniformsStreamInterface* interfaces[] = { &interface0, &interface1, &interface2, &interface3 };

        uint64_t boundVS = 0, boundPS = 0;
        for (unsigned c=0; c<dimof(interfaces); ++c) {
            _preboundInterfaceVS[c] = MakeStreamMapping(pipeline._reflection, c, interfaces, ShaderStage::Vertex);
            _preboundInterfacePS[c] = MakeStreamMapping(pipeline._reflection, c, interfaces, ShaderStage::Pixel);

            boundVS |= _preboundInterfaceVS[c]._boundArgs;
            boundPS |= _preboundInterfacePS[c]._boundArgs;

            _boundUniformBufferSlots[c] = _preboundInterfaceVS[c]._boundCBSlots | _preboundInterfacePS[c]._boundCBSlots;
            _boundResourceSlots[c] = _preboundInterfaceVS[c]._boundSRVSlots | _preboundInterfacePS[c]._boundSRVSlots;
        }

        auto* vsArgs = pipeline._reflection.get().vertexArguments;
        auto* psArgs = pipeline._reflection.get().fragmentArguments;
        for (unsigned argIdx=0; argIdx<vsArgs.count; ++argIdx) {
            MTLArgument* arg = vsArgs[argIdx];
            if (!arg.active || (boundVS & (1<<uint64_t(argIdx))))
                continue;

            if (arg.type == MTLArgumentTypeTexture) {
                if (arg.textureType == MTLTextureTypeCube) {
                    _unboundCubeSRVs.push_back({ShaderStage::Vertex, arg.index});
                } else {
                    _unbound2DSRVs.push_back({ShaderStage::Vertex, arg.index, arg.isDepthTexture});
                }
            } else if (arg.type == MTLArgumentTypeSampler) {
                _unboundSamplers.push_back({ShaderStage::Vertex, arg.index});
            }
        }

        for (unsigned argIdx=0; argIdx<psArgs.count; ++argIdx) {
            MTLArgument* arg = psArgs[argIdx];
            if (!arg.active || (boundPS & (1<<uint64_t(argIdx))))
                continue;

            if (arg.type == MTLArgumentTypeTexture) {
                if (arg.textureType == MTLTextureTypeCube) {
                    _unboundCubeSRVs.push_back({ShaderStage::Pixel, arg.index});
                } else {
                    _unbound2DSRVs.push_back({ShaderStage::Pixel, arg.index, arg.isDepthTexture});
                }
            } else if (arg.type == MTLArgumentTypeSampler) {
                _unboundSamplers.push_back({ShaderStage::Pixel, arg.index});
            }
        }
    }

    BoundUniforms::~BoundUniforms() {}

    BoundUniforms::BoundUniforms()
    {
        for (auto& s : _boundUniformBufferSlots) s = 0ull;
        for (auto& s : _boundResourceSlots) s = 0ull;
    }

    BoundUniforms::BoundUniforms(BoundUniforms&& moveFrom) never_throws
    : _unboundInterface(std::move(moveFrom._unboundInterface))
    {
        for (unsigned c=0; c<dimof(moveFrom._boundUniformBufferSlots); ++c) {
            _boundUniformBufferSlots[c] = moveFrom._boundUniformBufferSlots[c];
            _boundResourceSlots[c] = moveFrom._boundResourceSlots[c];
            moveFrom._boundUniformBufferSlots[c] = 0ull;
            moveFrom._boundResourceSlots[c] = 0ull;

            _preboundInterfaceVS[c] = std::move(moveFrom._preboundInterfaceVS[c]);
            _preboundInterfacePS[c] = std::move(moveFrom._preboundInterfacePS[c]);
        }
        _unboundCBs = std::move(moveFrom._unboundCBs);
        _unbound2DSRVs = std::move(moveFrom._unbound2DSRVs);
        _unboundCubeSRVs = std::move(moveFrom._unboundCubeSRVs);
        _unboundSamplers = std::move(moveFrom._unboundSamplers);
    }

    BoundUniforms& BoundUniforms::operator=(BoundUniforms&& moveFrom) never_throws
    {
        _unboundInterface = std::move(moveFrom._unboundInterface);
        for (unsigned c=0; c<dimof(moveFrom._boundUniformBufferSlots); ++c) {
            _boundUniformBufferSlots[c] = moveFrom._boundUniformBufferSlots[c];
            _boundResourceSlots[c] = moveFrom._boundResourceSlots[c];
            moveFrom._boundUniformBufferSlots[c] = 0ull;
            moveFrom._boundResourceSlots[c] = 0ull;
        }
        for (unsigned c=0; c<dimof(moveFrom._preboundInterfaceVS); ++c) {
            _preboundInterfaceVS[c] = std::move(moveFrom._preboundInterfaceVS[c]);
        }
        for (unsigned c=0; c<dimof(moveFrom._preboundInterfacePS); ++c) {
            _preboundInterfacePS[c] = std::move(moveFrom._preboundInterfacePS[c]);
        }
        return *this;
    }
}}
