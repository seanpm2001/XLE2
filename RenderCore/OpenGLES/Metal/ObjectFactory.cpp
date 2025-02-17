// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ObjectFactory.h"
#include "Resource.h"       // (for DescribeUnknownObject)
#include "GPUSyncedAllocator.h"
#include "../../../Utility/StringFormat.h"
#include "../../../Utility/IteratorUtils.h"
#include "../IDeviceOpenGLES.h"
#include "IncludeGLES.h"
#include <assert.h>
#include <iomanip>
#include <map>

#include "../../../ConsoleRig/Log.h"
#include "GLWrappers.h"

namespace RenderCore { namespace Metal_OpenGLES
{
    static ObjectFactory* s_objectFactory_instance = nullptr;

    signed  IndexedGLType_AddRef(RawGLHandle object) never_throws;
    signed  IndexedGLType_Release(RawGLHandle object) never_throws;

        ////////////////////////////////////////////////////////////////////////////////////////////////

    static unsigned AsGLShaderType(ShaderType::Enum type)
    {
        switch (type) {
        case ShaderType::VertexShader:      return GL_VERTEX_SHADER;
        default:
            assert(0);  // (only VertexShader & FragmentShader supported so far)
        case ShaderType::FragmentShader:    return GL_FRAGMENT_SHADER;
        }
    }

    #if defined(_DEBUG)
        unsigned g_trackedResourceAllocation = ~0u;
        GlObject_Type::Enum g_trackedResourceAllocationType = (GlObject_Type::Enum)~0u;
        static void OnResourceAllocation(GlObject_Type::Enum type, unsigned count, const unsigned* resId)
        {
            for (unsigned c=0; c<count; ++c) {
                if (resId[c] == g_trackedResourceAllocation && type == g_trackedResourceAllocationType) {
                    Log(Warning) << "Triggered tracked resource allocation (id: " << resId[c] << ", typecode: " << (unsigned)type << ")" << std::endl;
                }
            }
        }
    #else
        static void OnResourceAllocation(GlObject_Type::Enum, unsigned, const unsigned*) {}
    #endif

    intrusive_ptr<GlObject<GlObject_Type::Shader> >             ObjectFactory::CreateShader(ShaderType::Enum type)
    {
        unsigned resId = glCreateShader(AsGLShaderType(type));
        OnResourceAllocation(GlObject_Type::Shader, 1, &resId);
        auto obj =  (GlObject<GlObject_Type::Shader>*)(size_t)resId;
        return intrusive_ptr<GlObject<GlObject_Type::Shader>>(obj);
    }

    intrusive_ptr<GlObject<GlObject_Type::ShaderProgram> >      ObjectFactory::CreateShaderProgram()
    {
        unsigned resId = glCreateProgram();
        OnResourceAllocation(GlObject_Type::ShaderProgram, 1, &resId);
        auto temp = (GlObject<GlObject_Type::ShaderProgram>*)(size_t)resId;
        return intrusive_ptr<GlObject<GlObject_Type::ShaderProgram> >(temp);
    }

    intrusive_ptr<GlObject<GlObject_Type::Texture> >            ObjectFactory::CreateTexture()
    {
        RawGLHandle result = RawGLHandle_Invalid;
        glGenTextures(1, &result);
        OnResourceAllocation(GlObject_Type::Texture, 1, &result);
        auto temp = (GlObject<GlObject_Type::Texture>*)(size_t)result;
        return intrusive_ptr<GlObject<GlObject_Type::Texture> >(temp);
    }

    intrusive_ptr<GlObject<GlObject_Type::RenderBuffer> >       ObjectFactory::CreateRenderBuffer()
    {
        RawGLHandle result = RawGLHandle_Invalid;
        glGenRenderbuffers(1, &result);
        OnResourceAllocation(GlObject_Type::RenderBuffer, 1, &result);
        auto temp = (GlObject<GlObject_Type::RenderBuffer>*)(size_t)result;
        return intrusive_ptr<GlObject<GlObject_Type::RenderBuffer> >(temp);
    }

    intrusive_ptr<GlObject<GlObject_Type::FrameBuffer> >        ObjectFactory::CreateFrameBuffer()
    {
        RawGLHandle result = RawGLHandle_Invalid;
        glGenFramebuffers(1, &result);
        OnResourceAllocation(GlObject_Type::FrameBuffer, 1, &result);
        auto temp = (GlObject<GlObject_Type::FrameBuffer>*)(size_t)result;
        return intrusive_ptr<GlObject<GlObject_Type::FrameBuffer> >(temp);
    }

    intrusive_ptr<GlObject<GlObject_Type::Buffer> >             ObjectFactory::CreateBuffer()
    {
        RawGLHandle result = RawGLHandle_Invalid;
        glGenBuffers(1, &result);
        OnResourceAllocation(GlObject_Type::Buffer, 1, &result);
        auto temp = (GlObject<GlObject_Type::Buffer>*)(size_t)result;
        return intrusive_ptr<GlObject<GlObject_Type::Buffer> >(temp);
    }

    intrusive_ptr<GlObject<GlObject_Type::Sampler> >             ObjectFactory::CreateSampler()
    {
        if (_featureSet & FeatureSet::GLES300) {
            RawGLHandle result = RawGLHandle_Invalid;
            glGenSamplers(1, &result);
            OnResourceAllocation(GlObject_Type::Sampler, 1, &result);
            auto temp = (GlObject<GlObject_Type::Sampler>*)(size_t)result;
            return intrusive_ptr<GlObject<GlObject_Type::Sampler> >(temp);
        } else {
            return intrusive_ptr<GlObject<GlObject_Type::Sampler> >();
        }
    }

    intrusive_ptr<GlObject<GlObject_Type::VAO> >             ObjectFactory::CreateVAO()
    {
        if (!_vaosEnabled) {
            // VAOs has been disabled, do not generate one!
            return nullptr;
        } else if (_featureSet & FeatureSet::GLES300) {
            RawGLHandle result = RawGLHandle_Invalid;
            glGenVertexArrays(1, &result);
            OnResourceAllocation(GlObject_Type::VAO, 1, &result);
            auto temp = (GlObject<GlObject_Type::VAO>*)(size_t)result;
            return intrusive_ptr<GlObject<GlObject_Type::VAO> >(temp);
        } else {
            RawGLHandle result = RawGLHandle_Invalid;
            #if GL_APPLE_vertex_array_object
                glGenVertexArraysAPPLE(1, &result);
            #else
                glGenVertexArraysOES(1, &result);
            #endif
            OnResourceAllocation(GlObject_Type::VAO, 1, &result);
            auto temp = (GlObject<GlObject_Type::VAO>*)(size_t)result;
            return intrusive_ptr<GlObject<GlObject_Type::VAO> >(temp);
        }
    }

        ////////////////////////////////////////////////////////////////////////////////////////////////

    namespace Detail
    {
        template<int Type> void Destroy(RawGLHandle handle);

        template<> void Destroy<GlObject_Type::Shader>(RawGLHandle object)
        { 
            glDeleteShader(object);
        }
    
        template<> void Destroy<GlObject_Type::ShaderProgram>(RawGLHandle object)
        { 
            glDeleteProgram(object);
        }
    
        template<> void Destroy<GlObject_Type::Texture>(RawGLHandle object)
        { 
            GL_WRAP(DeleteTextures)(1, &object);
        }
    
        template<> void Destroy<GlObject_Type::RenderBuffer>(RawGLHandle object)
        { 
            GL_WRAP(DeleteRenderbuffers)(1, &object);
        }

        template<> void Destroy<GlObject_Type::FrameBuffer>(RawGLHandle object)
        { 
            glDeleteFramebuffers(1, &object);
        }
    
        template<> void Destroy<GlObject_Type::Buffer>(RawGLHandle object)
        { 
            GL_WRAP(DeleteBuffers)(1, (GLuint*)&object);
        }

        template<> void Destroy<GlObject_Type::Resource>(RawGLHandle object)
        {
                //
                //      We don't know the type of this resource.
                //      fall back to the simpliest possible polymorphism...
                //
                //      Just check the type and 
                //
            if (glIsTexture(object)) {
                GL_WRAP(DeleteTextures)(1, &object);
            } else if (glIsBuffer(object)) {
                GL_WRAP(DeleteBuffers)(1, &object);
            } else if (glIsFramebuffer(object)) {
                glDeleteFramebuffers(1, &object);
            } else if (glIsRenderbuffer(object)) {
                GL_WRAP(DeleteRenderbuffers)(1, &object);
            }
        }

        template<> void Destroy<GlObject_Type::Sampler>(RawGLHandle object)
        {
            glDeleteSamplers(1, (GLuint*)&object);
        }

        template<> void Destroy<GlObject_Type::VAO>(RawGLHandle object)
        {
            if (GetObjectFactory().GetFeatureSet() & FeatureSet::GLES300) {
                glDeleteVertexArrays(1, (GLuint*)&object);
            } else {
                #if GL_APPLE_vertex_array_object
                    glDeleteVertexArraysAPPLE(1, &object);
                #else
                    glDeleteVertexArraysOES(1, &object);
                #endif
            }
        }
    }

        ////////////////////////////////////////////////////////////////////////////////////////////////

    template<int Type>
        signed      GlObject<Type>::AddRef() const never_throws
    {
        assert(s_objectFactory_instance);
        auto& objectFactory = GetObjectFactory();
        return objectFactory.IndexedGLType_AddRef(Type, AsRawGLHandle());
    }

    template<int Type>
        signed      GlObject<Type>::Release() const never_throws
    {
        if (s_objectFactory_instance) {
            auto& objectFactory = *s_objectFactory_instance;
            signed result = objectFactory.IndexedGLType_Release(Type, AsRawGLHandle());
            if (result == 0) {
                Detail::Destroy<Type>(AsRawGLHandle());
            }
            return result;
        } else {
            Log(Error) << "Attempting to release reference count on OpenGL object after the ObjectFactory has been destroyed. Falling back to immediate destroy" << std::endl;
            Detail::Destroy<Type>(AsRawGLHandle());
            return 0;
        }
    }

    template<int Type>
        GlObject<Type>::GlObject()
    {
    }
        
    template<int Type>
        GlObject<Type>::~GlObject()
    {
        Release();
    }

    template class GlObject<GlObject_Type::Shader>;
    template class GlObject<GlObject_Type::ShaderProgram>;
    template class GlObject<GlObject_Type::Texture>;
    template class GlObject<GlObject_Type::RenderBuffer>;
    template class GlObject<GlObject_Type::FrameBuffer>;
    template class GlObject<GlObject_Type::Buffer>;
    template class GlObject<GlObject_Type::Resource>;
    template class GlObject<GlObject_Type::Sampler>;
    template class GlObject<GlObject_Type::VAO>;

        ////////////////////////////////////////////////////////////////////////////////////////////////

    template<> template<> GlObject<GlObject_Type::Texture>*             GlObject<GlObject_Type::Resource>::ResourceCast() never_throws
    {
        if (glIsTexture(AsRawGLHandle())) {
            return (GlObject<GlObject_Type::Texture>*)(this);
        } else {
            return (GlObject<GlObject_Type::Texture>*)RawGLHandle_Invalid;
        }
    }

    template<> template<> GlObject<GlObject_Type::RenderBuffer>*        GlObject<GlObject_Type::Resource>::ResourceCast() never_throws
    {
        if (glIsRenderbuffer(AsRawGLHandle())) {
            return (GlObject<GlObject_Type::RenderBuffer>*)(this);
        } else {
            return (GlObject<GlObject_Type::RenderBuffer>*)RawGLHandle_Invalid;
        }
    }

    template<> template<> GlObject<GlObject_Type::FrameBuffer>*         GlObject<GlObject_Type::Resource>::ResourceCast() never_throws
    {
        if (glIsFramebuffer(AsRawGLHandle())) {
            return (GlObject<GlObject_Type::FrameBuffer>*)(this);
        } else {
            return (GlObject<GlObject_Type::FrameBuffer>*)RawGLHandle_Invalid;
        }
    }

    template<> template<> GlObject<GlObject_Type::Buffer>*              GlObject<GlObject_Type::Resource>::ResourceCast() never_throws
    {
        if (glIsBuffer(AsRawGLHandle())) {
            return (GlObject<GlObject_Type::Buffer>*)(this);
        } else {
            return (GlObject<GlObject_Type::Buffer>*)RawGLHandle_Invalid;
        }
    }

    template<> template<> const GlObject<GlObject_Type::Texture>*       GlObject<GlObject_Type::Resource>::ResourceCast() const never_throws
    {
        if (glIsTexture(AsRawGLHandle())) {
            return (GlObject<GlObject_Type::Texture>*)(this);
        } else {
            return (GlObject<GlObject_Type::Texture>*)RawGLHandle_Invalid;
        }
    }

    template<> template<> const GlObject<GlObject_Type::RenderBuffer>*    GlObject<GlObject_Type::Resource>::ResourceCast() const never_throws
    {
        if (glIsRenderbuffer(AsRawGLHandle())) {
            return (GlObject<GlObject_Type::RenderBuffer>*)(this);
        } else {
            return (GlObject<GlObject_Type::RenderBuffer>*)RawGLHandle_Invalid;
        }
    }

    template<> template<> const GlObject<GlObject_Type::FrameBuffer>*     GlObject<GlObject_Type::Resource>::ResourceCast() const never_throws
    {
        if (glIsFramebuffer(AsRawGLHandle())) {
            return (GlObject<GlObject_Type::FrameBuffer>*)(this);
        } else {
            return (GlObject<GlObject_Type::FrameBuffer>*)RawGLHandle_Invalid;
        }
    }

    template<> template<> const GlObject<GlObject_Type::Buffer>*          GlObject<GlObject_Type::Resource>::ResourceCast() const never_throws
    {
        if (glIsBuffer(AsRawGLHandle())) {
            return (GlObject<GlObject_Type::Buffer>*)(this);
        } else {
            return (GlObject<GlObject_Type::Buffer>*)RawGLHandle_Invalid;
        }
    }

    template<> template<> const GlObject<GlObject_Type::Sampler>*          GlObject<GlObject_Type::Sampler>::ResourceCast() const never_throws
    {
        if (glIsSampler(AsRawGLHandle())) {
            return (GlObject<GlObject_Type::Sampler>*)(this);
        } else {
            return (GlObject<GlObject_Type::Sampler>*)RawGLHandle_Invalid;
        }
    }

    template<int Type> template <int OtherType> GlObject<OtherType>*        GlObject<Type>::ResourceCast() never_throws         { return RawGLHandle_Invalid; }
    template<int Type> template <int OtherType> const GlObject<OtherType>*  GlObject<Type>::ResourceCast() const never_throws   { return RawGLHandle_Invalid; }

        ////////////////////////////////////////////////////////////////////////////////////////////////

    static uint64_t RefCountId(unsigned type, unsigned object)
    {
        return (uint64_t(type) << 32ull) | object;
    }

    static std::pair<unsigned, unsigned> SeparateRefCountId(uint64_t id)
    {
        return {id >> 32ull, unsigned(id)};
    }

    signed ObjectFactory::IndexedGLType_AddRef(unsigned type, unsigned object) never_throws
    {
        ScopedLock(_refCountTableLock);
        auto id = RefCountId(type, object);
        auto i = LowerBound(_refCountTable, id);
        if (i==_refCountTable.end() || i->first != id) {
            _refCountTable.insert(i, std::make_pair(id, 1));
            return 1;
        }
        auto result = ++i->second;
        return result;
    }

    signed ObjectFactory::IndexedGLType_Release(unsigned type, unsigned object) never_throws
    {
        ScopedLock(_refCountTableLock);
        auto id = RefCountId(type, object);
        auto i = LowerBound(_refCountTable, id);
        if (i==_refCountTable.end() || i->first != id) {
            assert(0);
            return -1;
        }
        signed result = --i->second;
        if (!result) {
            _refCountTable.erase(i);
        }
        return result;
    }

    void ObjectFactory::ReportLeaks()
    {
        ScopedLock(_refCountTableLock);
        if (!_refCountTable.empty()) {
            Log(Warning) << "[OpenGL Leaks] " << _refCountTable.size() << " objects remain" << std::endl;
            auto count=0u;
            for (auto i=_refCountTable.cbegin(); i!=_refCountTable.cend(); ++i, ++count) {
                auto typeAndId = SeparateRefCountId(i->first);
                Log(Warning) << "  [" << count << "] Object (" << typeAndId.second << ") has (" << i->second << ") refs (typecode " << typeAndId.first << ") [" << DescribeUnknownObject(typeAndId.second) << "]" << std::endl;
            }
        }
    }

    ObjectFactory::ObjectFactory(FeatureSet::BitField featureSet)
    : _featureSet(featureSet), _vaosEnabled(true)
    {
        assert(s_objectFactory_instance == nullptr);
        s_objectFactory_instance = this;

        CheckGLError("Before initializing stand in textures");

        _standIn2DTexture = CreateTexture();
        glBindTexture(GL_TEXTURE_2D, _standIn2DTexture->AsRawGLHandle());
        unsigned dummyData[4*4];
        for (unsigned c=0; c<dimof(dummyData); ++c)
            dummyData[c] = 0xffffffff;
        glTexImage2D(
            GL_TEXTURE_2D,
            0, GL_RGBA,
            4, 4, 0,
            GL_RGBA, GL_UNSIGNED_BYTE,
            dummyData);
        glBindTexture(GL_TEXTURE_2D, 0);

        _standInCubeTexture = CreateTexture();
        GLenum faceBinding[] = {
            GL_TEXTURE_CUBE_MAP_POSITIVE_X,
            GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
            GL_TEXTURE_CUBE_MAP_POSITIVE_Y,
            GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
            GL_TEXTURE_CUBE_MAP_POSITIVE_Z,
            GL_TEXTURE_CUBE_MAP_NEGATIVE_Z };
        glBindTexture(GL_TEXTURE_CUBE_MAP, _standInCubeTexture->AsRawGLHandle());
        for (unsigned c=0; c<dimof(faceBinding); ++c) {
            glTexImage2D(
                faceBinding[c],
                0, GL_RGBA,
                4, 4, 0,
                GL_RGBA, GL_UNSIGNED_BYTE,
                dummyData);
        }
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

        CheckGLError("After initializing stand in textures");

        if (_featureSet & FeatureSet::GLES300) {
            _reusableCBSpace = std::make_unique<DynamicBuffer>(
                RenderCore::LinearBufferDesc::Create(2048 * 1024), RenderCore::BindFlag::ConstantBuffer, "TempCBBuffer", true, nullptr);
        }
    }
    ObjectFactory::~ObjectFactory()
    {
        assert(s_objectFactory_instance == this);

        _reusableCBSpace.reset();
        _standInCubeTexture.reset();
        _standIn2DTexture.reset();

        {
            ScopedLock(_compiledShadersLock);
            decltype(_compiledShaders)().swap(_compiledShaders);
        }

        #if defined(_DEBUG)
            ReportLeaks();
        #endif

        s_objectFactory_instance = nullptr;
    }

    ObjectFactory& GetObjectFactory(IDevice& device) { assert(s_objectFactory_instance); return *s_objectFactory_instance; }
    ObjectFactory& GetObjectFactory(DeviceContext&) { assert(s_objectFactory_instance); return *s_objectFactory_instance; }
    ObjectFactory& GetObjectFactory(IResource&) { assert(s_objectFactory_instance); return *s_objectFactory_instance; }
    ObjectFactory& GetObjectFactory() { assert(s_objectFactory_instance); return *s_objectFactory_instance; }

        ////////////////////////////////////////////////////////////////////////////////////////////////

    #if defined(_DEBUG)
        void CheckGLError(const char context[])
        {
            auto e = glGetError();
            if (e) {
                Log(Error) << "Encountered OpenGL error (" << "0x" << std::hex << std::setfill('0') << std::setw(4) << e << ") in context (" << context << ")" << std::endl;
                // OutputDebugString(XlDynFormatString("Encountered OpenGL error (%i) in context (%s)", e, context).c_str());
            }
        }
    #endif

}}

