#include "stdafx.h"

#include "android_gles_renderer.h"

// Render.h needs ImTextureID, but this target does not have ImGui's include
// path. Dear ImGui's default texture id is void*. This is only for the stub.
typedef void* ImTextureID;

#include "xrEngine/Render.h"
#include "xrEngine/EngineAPI.h"

#include "Include/xrRender/RenderFactory.h"
#include "Include/xrRender/ImGuiRender.h"
#include "Include/xrRender/FontRender.h"
#include "Include/xrRender/StatGraphRender.h"

#include "Include/xrAPI/xrAPI.h"
#include "xrCommon/xr_vector.h"

#include <SDL.h>
#include <GLES3/gl3.h>
#include <cstdio>
#include <cstring>
#include <utility>

namespace xray::render::android_gles
{
namespace
{
constexpr pcstr kRendererMode = "renderer_gles";
constexpr int kFrameLimit = 300;
constexpr Uint32 kMinElapsedMs = 5000;

static pcstr SafeGLGetString(GLenum name)
{
    const char* s = reinterpret_cast<const char*>(glGetString(name));
    return s ? s : "(null)";
}

static void DrGLError() { while (glGetError() != GL_NO_ERROR) {} }

static GLint ProbeColorAttachment(GLenum internalformat, GLenum format, GLenum type)
{
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, internalformat, 4, 4, 0, format, type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    GLint st = glCheckFramebufferStatus(GL_FRAMEBUFFER);

    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &tex);
    return st;
}

static bool HasGLExtension(const char* name)
{
    GLint n = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (GLint i = 0; i < n; ++i)
    {
        const char* ext = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
        if (ext && strcmp(ext, name) == 0)
            return true;
    }
    return false;
}

static void DumpFboCap(FILE* f, const char* name, GLenum internalformat, GLenum format, GLenum type)
{
    const GLint st = ProbeColorAttachment(internalformat, format, type);
    fprintf(f, "FBO_%s_STATUS=%d\n", name, static_cast<int>(st));
    fprintf(f, "FBO_%s_COMPLETE=%d\n", name, st == GL_FRAMEBUFFER_COMPLETE ? 1 : 0);
}

static void DumpGLCaps()
{
    FILE* f = fopen("/sdcard/XRay/gl_caps.txt", "w");
    if (!f)
    {
        Msg("[GLES-STUB] gl_caps fopen FALHOU (permissoes/dir?), telemetria ignorada");
        return;
    }

    fprintf(f, "GL_VERSION=%s\n", SafeGLGetString(GL_VERSION));
    fprintf(f, "GL_RENDERER=%s\n", SafeGLGetString(GL_RENDERER));
    fprintf(f, "GL_VENDOR=%s\n", SafeGLGetString(GL_VENDOR));
    fprintf(f, "GL_SHADING_LANGUAGE_VERSION=%s\n", SafeGLGetString(GL_SHADING_LANGUAGE_VERSION));

    GLint n = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (GLint i = 0; i < n; ++i)
    {
        const char* ext = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
        fprintf(f, "EXT_%s=1\n", ext ? ext : "(null)");
    }

    fprintf(f, "HAS_S3TC=%d\n", HasGLExtension("GL_EXT_texture_compression_s3tc") ? 1 : 0);
    fprintf(f, "HAS_ASTC_LDR=%d\n", HasGLExtension("GL_KHR_texture_compression_astc_ldr") ? 1 : 0);
    fprintf(f, "HAS_ASTC_HDR=%d\n", HasGLExtension("GL_KHR_texture_compression_astc_hdr") ? 1 : 0);
    fprintf(f, "HAS_ETC2=%d\n", HasGLExtension("GL_OES_compressed_ETC2_RGB8_texture") ? 1 : 0);
    fprintf(f, "HAS_PVRTC=%d\n", HasGLExtension("GL_IMG_texture_compression_pvrtc") ? 1 : 0);
    fprintf(f, "HAS_FLOAT_TEX=%d\n", HasGLExtension("GL_OES_texture_float") ? 1 : 0);
    fprintf(f, "HAS_HALF_FLOAT_COLOR=%d\n", HasGLExtension("GL_EXT_color_buffer_half_float") ? 1 : 0);
    fprintf(f, "HAS_FULL_FLOAT_COLOR=%d\n", HasGLExtension("GL_EXT_color_buffer_float") ? 1 : 0);
    fprintf(f, "HAS_ANISOTROPY=%d\n", HasGLExtension("GL_EXT_texture_filter_anisotropic") ? 1 : 0);

    GLint v = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &v);
    fprintf(f, "MAX_TEXTURE_SIZE=%d\n", static_cast<int>(v));
    glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &v);
    fprintf(f, "MAX_CUBE_MAP_TEXTURE_SIZE=%d\n", static_cast<int>(v));
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &v);
    fprintf(f, "MAX_RENDERBUFFER_SIZE=%d\n", static_cast<int>(v));
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &v);
    fprintf(f, "MAX_COMBINED_TEXTURE_IMAGE_UNITS=%d\n", static_cast<int>(v));
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &v);
    fprintf(f, "MAX_VERTEX_ATTRIBS=%d\n", static_cast<int>(v));
    glGetIntegerv(GL_MAX_VARYING_VECTORS, &v);
    fprintf(f, "MAX_VARYING_VECTORS=%d\n", static_cast<int>(v));
    glGetIntegerv(GL_MAX_VERTEX_UNIFORM_VECTORS, &v);
    fprintf(f, "MAX_VERTEX_UNIFORM_VECTORS=%d\n", static_cast<int>(v));
    glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_VECTORS, &v);
    fprintf(f, "MAX_FRAGMENT_UNIFORM_VECTORS=%d\n", static_cast<int>(v));
    glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &v);
    fprintf(f, "MAX_UNIFORM_BLOCK_SIZE=%d\n", static_cast<int>(v));
    glGetIntegerv(GL_MAX_SAMPLES, &v);
    fprintf(f, "MAX_SAMPLES=%d\n", static_cast<int>(v));
    glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &v);
    fprintf(f, "MAX_COLOR_ATTACHMENTS=%d\n", static_cast<int>(v));
    glGetIntegerv(GL_MAX_DRAW_BUFFERS, &v);
    fprintf(f, "MAX_DRAW_BUFFERS=%d\n", static_cast<int>(v));

    DumpFboCap(f, "RGBA8", GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
    DumpFboCap(f, "RGB10_A2", GL_RGB10_A2, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV);
    DumpFboCap(f, "RGBA16F", GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
    DumpFboCap(f, "RGB16F", GL_RGB16F, GL_RGB, GL_HALF_FLOAT);
    DumpFboCap(f, "R16F", GL_R16F, GL_RED, GL_HALF_FLOAT);
    DumpFboCap(f, "RG16F", GL_RG16F, GL_RG, GL_HALF_FLOAT);
    DumpFboCap(f, "RGBA32F", GL_RGBA32F, GL_RGBA, GL_FLOAT);
    DumpFboCap(f, "DEPTH_COMPONENT24", GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT);
    DumpFboCap(f, "DEPTH24_STENCIL8", GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8);
    DumpFboCap(f, "STENCIL_INDEX8", GL_STENCIL_INDEX8, GL_STENCIL_INDEX8, GL_UNSIGNED_BYTE);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);

    Msg("[GLES-STUB] gl_caps.txt escrito, glGetError: %d", static_cast<int>(glGetError()));
    fclose(f);
}

static GLuint CompileShader(GLenum type, const char* src)
{
    GLuint shader = glCreateShader(type);
    if (!shader)
    {
        Msg("[GLES-STUB] glCreateShader failed, glGetError: %d", static_cast<int>(glGetError()));
        return 0;
    }

    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[512] = {};
        glGetShaderInfoLog(shader, static_cast<GLsizei>(sizeof(log)), nullptr, log);
        Msg("[GLES-STUB] shader compile failed: %s", log);
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

static GLuint LinkProgram(GLuint vs, GLuint fs)
{
    if (!vs || !fs)
        return 0;

    GLuint program = glCreateProgram();
    if (!program)
    {
        Msg("[GLES-STUB] glCreateProgram failed, glGetError: %d", static_cast<int>(glGetError()));
        return 0;
    }

    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[512] = {};
        glGetProgramInfoLog(program, static_cast<GLsizei>(sizeof(log)), nullptr, log);
        Msg("[GLES-STUB] program link failed: %s", log);
        glDeleteProgram(program);
        return 0;
    }

    return program;
}

class CGlesStubImGuiRender final : public IImGuiRender
{
public:
    void Copy(IImGuiRender& /*in*/) override {}
    void Frame() override {}
    void Render(ImDrawData* /*data*/) override {}
    void OnDeviceCreate(ImGuiContext* /*context*/) override {}
    void OnDeviceDestroy() override {}
    void OnDeviceResetBegin() override {}
    void OnDeviceResetEnd() override {}
};

class CGlesStubFontRender final : public IFontRender
{
public:
    void Initialize(cpcstr /*cShader*/, cpcstr /*cTexture*/) override {}
    void OnRender(CGameFont& /*owner*/) override {}
};

class CGlesStubStatGraphRender final : public IStatGraphRender
{
public:
    void Copy(IStatGraphRender& /*in*/) override {}
    void OnDeviceCreate() override {}
    void OnDeviceDestroy() override {}
    void OnRender(CStatGraph& /*owner*/) override {}
};

// Factory stub: guards mirror Include/xrRender/RenderFactory.h.
// Only the three Create* that return a real object have a matching xr_delete;
// every other Destroy* is a no-op because the stub never allocated that object.
class CGlesStubRenderFactory final : public IRenderFactory
{
public:
#ifndef _EDITOR
    IUISequenceVideoItem* CreateUISequenceVideoItem() override { return nullptr; }
    void DestroyUISequenceVideoItem(IUISequenceVideoItem* /*pObject*/) override {}

    IUIShader* CreateUIShader() override { return nullptr; }
    void DestroyUIShader(IUIShader* /*pObject*/) override {}

    IStatGraphRender* CreateStatGraphRender() override { return xr_new<CGlesStubStatGraphRender>(); }
    void DestroyStatGraphRender(IStatGraphRender* pObject) override { xr_delete(pObject); }

#ifdef DEBUG
    IObjectSpaceRender* CreateObjectSpaceRender() override { return nullptr; }
    void DestroyObjectSpaceRender(IObjectSpaceRender* /*pObject*/) override {}
#endif // DEBUG

    IWallMarkArray* CreateWallMarkArray() override { return nullptr; }
    void DestroyWallMarkArray(IWallMarkArray* /*pObject*/) override {}

    IEnvironmentRender* CreateEnvironmentRender() override { return nullptr; }
    void DestroyEnvironmentRender(IEnvironmentRender* /*pObject*/) override {}

    IEnvDescriptorRender* CreateEnvDescriptorRender() override { return nullptr; }
    void DestroyEnvDescriptorRender(IEnvDescriptorRender* /*pObject*/) override {}

    IRainRender* CreateRainRender() override { return nullptr; }
    void DestroyRainRender(IRainRender* /*pObject*/) override {}

    ILensFlareRender* CreateLensFlareRender() override { return nullptr; }
    void DestroyLensFlareRender(ILensFlareRender* /*pObject*/) override {}

    IImGuiRender* CreateImGuiRender() override { return xr_new<CGlesStubImGuiRender>(); }
    void DestroyImGuiRender(IImGuiRender* pObject) override { xr_delete(pObject); }

    IThunderboltRender* CreateThunderboltRender() override { return nullptr; }
    void DestroyThunderboltRender(IThunderboltRender* /*pObject*/) override {}

    IThunderboltDescRender* CreateThunderboltDescRender() override { return nullptr; }
    void DestroyThunderboltDescRender(IThunderboltDescRender* /*pObject*/) override {}

    IFlareRender* CreateFlareRender() override { return nullptr; }
    void DestroyFlareRender(IFlareRender* /*pObject*/) override {}
#endif // _EDITOR

    // FontRender lives outside the _EDITOR guard in the base.
    IFontRender* CreateFontRender() override { return xr_new<CGlesStubFontRender>(); }
    void DestroyFontRender(IFontRender* pObject) override { xr_delete(pObject); }
};

class CGlesStubRender final : public IRender
{
    SDL_Window* m_window{};
    SDL_GLContext m_context{};
    int m_frameCount{};
    bool m_quitRequested{};
    GLuint m_program{};
    GLuint m_vao{};
    GLuint m_vbo{};
    Uint32 m_startTicks{};
    GLuint m_texture{};
    GLuint m_quad_program{};
    GLuint m_quad_vao{};
    GLuint m_quad_vbo{};

    void RequestQuit()
    {
        if (m_quitRequested)
            return;

        m_quitRequested = true;
        SDL_Event event{};
        event.type = SDL_QUIT;
        SDL_PushEvent(&event);
        Msg("[GLES-STUB] quit requested after %d frames", m_frameCount);
    }

public:
    GenerationLevel GetGeneration() const override { return GENERATION_R2; }
    BackendAPI GetBackendAPI() const override { return BackendAPI::OpenGL; }

    bool is_sun_static() override { return false; }
    u32 get_dx_level() override { return 9; }

    void create() override {}
    void destroy() override {}
    void reset_begin() override {}
    void reset_end() override {}

    void level_Load(IReader* /*fs*/) override {}
    void level_Unload() override {}

    HRESULT shader_compile(pcstr /*name*/, IReader* /*fs*/, pcstr /*pFunctionName*/, pcstr /*pTarget*/,
        u32 /*Flags*/, void*& /*result*/) override
    {
        return 0;
    }

    void DumpStatistics(IGameFont& /*font*/, IPerformanceAlert* /*alert*/) override {}

    pcstr getShaderPath() override { return ""; }
    IRenderVisual* getVisual(int /*id*/) override { return nullptr; }
    xrImTextureData GetImGuiTextureId(pcstr /*texture_name*/) override { return {}; }

    void add_Visual(u32 /*context_id*/, IRenderable* /*root*/, IRenderVisual* /*V*/, Fmatrix& /*m*/) override {}
    void add_StaticWallmark(
        const wm_shader& /*S*/, const Fvector& /*P*/, float /*s*/, CDB::TRI* /*T*/, Fvector* /*V*/) override
    {
    }
    void add_StaticWallmark(
        IWallMarkArray* /*pArray*/, const Fvector& /*P*/, float /*s*/, CDB::TRI* /*T*/, Fvector* /*V*/) override
    {
    }
    void clear_static_wallmarks() override {}
    void add_SkeletonWallmark(const Fmatrix* /*xf*/, IKinematics* /*obj*/, IWallMarkArray* /*pArray*/,
        const Fvector& /*start*/, const Fvector& /*dir*/, float /*size*/) override
    {
    }

    IRender_ObjectSpecific* ros_create(IRenderable* /*parent*/) override { return nullptr; }
    void ros_destroy(IRender_ObjectSpecific*& p) override { p = nullptr; }

    IRender_Light* light_create() override { return nullptr; }
    IRender_Glow* glow_create() override { return nullptr; }

    IRenderVisual* model_CreateParticles(pcstr /*name*/) override { return nullptr; }
    IRenderVisual* model_Create(pcstr /*name*/, IReader* /*data*/) override { return nullptr; }
    IRenderVisual* model_CreateChild(pcstr /*name*/, IReader* /*data*/) override { return nullptr; }
    IRenderVisual* model_Duplicate(IRenderVisual* /*V*/) override { return nullptr; }
    void model_Delete(IRenderVisual*& V, bool /*bDiscard*/) override { V = nullptr; }
    void model_Logging(bool /*bEnable*/) override {}
    void models_Prefetch() override {}
    void models_Clear(bool /*b_complete*/) override {}

    bool occ_visible(vis_data& /*V*/) override { return false; }
    bool occ_visible(Fbox& /*B*/) override { return false; }
    bool occ_visible(sPoly& /*P*/) override { return false; }

    void Calculate() override {}
    void Render() override {}
    void RenderMenu() override {}
    void BeforeWorldRender() override {}
    void AfterWorldRender() override {}

    void Screenshot(ScreenshotMode /*mode*/, pcstr /*name*/) override {}
    void SetPostProcessParams(const SPPInfo& /*ppi*/) override {}

    void setGamma(float /*fGamma*/) override {}
    void setBrightness(float /*fGamma*/) override {}
    void setContrast(float /*fGamma*/) override {}
    void updateGamma() override {}

    void OnDeviceDestroy(bool /*bKeepTextures*/) override {}
    void Destroy() override
{
    if (m_context)
    {
        SDL_GL_MakeCurrent(m_window, m_context);

        if (m_texture)
        {
            glDeleteTextures(1, &m_texture);
            m_texture = 0;
        }

        if (m_quad_vao)
        {
            glDeleteVertexArrays(1, &m_quad_vao);
            m_quad_vao = 0;
        }

        if (m_quad_vbo)
        {
            glDeleteBuffers(1, &m_quad_vbo);
            m_quad_vbo = 0;
        }

        if (m_quad_program)
        {
            glDeleteProgram(m_quad_program);
            m_quad_program = 0;
        }

        if (m_vao)
        {
            glDeleteVertexArrays(1, &m_vao);
            m_vao = 0;
        }

        if (m_vbo)
        {
            glDeleteBuffers(1, &m_vbo);
            m_vbo = 0;
        }

        if (m_program)
        {
            glDeleteProgram(m_program);
            m_program = 0;
        }

        SDL_GL_DeleteContext(m_context);
        m_context = nullptr;
    }

    m_window = nullptr;
}
    void Reset(SDL_Window* hWnd, u32& dwWidth, u32& dwHeight, float& fWidth_2, float& fHeight_2) override
{
    m_window = hWnd;
    Msg("[GLES-STUB] Reset entry window=%p", static_cast<void*>(hWnd));

    int w = 0;
    int h = 0;

    if (m_context)
    {
        SDL_GL_GetDrawableSize(m_window, &w, &h);
    }
    else if (m_window)
    {
        SDL_GetWindowSize(m_window, &w, &h);
    }

    if (w <= 0)
        w = 1;

    if (h <= 0)
        h = 1;

    dwWidth = static_cast<u32>(w);
    dwHeight = static_cast<u32>(h);
    fWidth_2 = static_cast<float>(w) * 0.5f;
    fHeight_2 = static_cast<float>(h) * 0.5f;

    if (m_context)
    {
        if (SDL_GL_MakeCurrent(m_window, m_context) != 0)
        {
            Msg("[GLES-STUB] Reset SDL_GL_MakeCurrent failed: %s", SDL_GetError());
        }

        glViewport(0, 0, w, h);
        glClearColor(0.08f, 0.18f, 0.35f, 1.0f);
    }

    Msg("[GLES-STUB] Reset size: %dx%d", w, h);
    Msg("[GLES-STUB] Reset exit");
}

    void ObtainRequiredWindowFlags(u32& windowFlags) override
    {
        windowFlags |= SDL_WINDOW_OPENGL;

        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    }

    void SetupStates() override
{
    Msg("[GLES-STUB] SetupStates entry");
    Msg("[GLES-STUB] SetupStates exit");
}

void OnDeviceCreate(pcstr shName) override
{
    Msg("[GLES-STUB] OnDeviceCreate entry shName=%s", shName ? shName : "(null)");
    Msg("[GLES-STUB] OnDeviceCreate exit");
}

    void Create(SDL_Window* hWnd, u32& dwWidth, u32& dwHeight, float& fWidth_2, float& fHeight_2) override
{
    m_window = hWnd;
    Msg("[GLES-STUB] Create entry window=%p", static_cast<void*>(hWnd));

    m_context = SDL_GL_CreateContext(m_window);
    if (!m_context)
    {
        Msg("[GLES-STUB] SDL_GL_CreateContext failed: %s", SDL_GetError());
        return;
    }

    if (SDL_GL_MakeCurrent(m_window, m_context) != 0)
    {
        Msg("[GLES-STUB] SDL_GL_MakeCurrent failed: %s", SDL_GetError());
    }

    // Para o stub, swap interval 0 é mais seguro: evita bloqueio por vsync
    // durante os primeiros frames de diagnóstico.
    if (SDL_GL_SetSwapInterval(1) != 0)
    {
        Msg("[GLES-STUB] SDL_GL_SetSwapInterval failed: %s", SDL_GetError());
    }

    Msg("[GLES-STUB] GL_VERSION: %s", SafeGLGetString(GL_VERSION));
    Msg("[GLES-STUB] GL_RENDERER: %s", SafeGLGetString(GL_RENDERER));
    Msg("[GLES-STUB] GL_VENDOR: %s", SafeGLGetString(GL_VENDOR));
    Msg("[GLES-STUB] GL_SHADING_LANGUAGE_VERSION: %s", SafeGLGetString(GL_SHADING_LANGUAGE_VERSION));
    Msg("[GLES-STUB] glGetError after context: %d", static_cast<int>(glGetError()));

    DumpGLCaps();

    int w = 0;
    int h = 0;
    SDL_GL_GetDrawableSize(m_window, &w, &h);

    if (w <= 0)
        w = 1;

    if (h <= 0)
        h = 1;

    // IMPORTANTE: devolver o tamanho real para o engine.
    // Esses parâmetros são referências de saída.
    dwWidth = static_cast<u32>(w);
    dwHeight = static_cast<u32>(h);
    fWidth_2 = static_cast<float>(w) * 0.5f;
    fHeight_2 = static_cast<float>(h) * 0.5f;

    glViewport(0, 0, w, h);
    glClearColor(0.08f, 0.18f, 0.35f, 1.0f);

    Msg("[GLES-STUB] drawable size: %dx%d", w, h);
    Msg("[GLES-STUB] engine size set: %ux%u", dwWidth, dwHeight);
    Msg("[GLES-STUB] engine half size set: %.2f x %.2f", fWidth_2, fHeight_2);

    // Primeiro clear/swap de diagnóstico.
    // Se isso funcionar, provamos que GL draw + swap estão vivos.
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    SDL_GL_SwapWindow(m_window);

    Msg("[GLES-STUB] initial clear/swap done, glGetError: %d", static_cast<int>(glGetError()));

    m_startTicks = SDL_GetTicks();

    static const char* vert_src =
        "#version 300 es\n"
        "layout(location = 0) in vec2 aPos;\n"
        "layout(location = 1) in vec3 aColor;\n"
        "out vec3 vColor;\n"
        "void main() {\n"
        "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
        "    vColor = aColor;\n"
        "}\n";

    static const char* frag_src =
        "#version 300 es\n"
        "precision mediump float;\n"
        "in vec3 vColor;\n"
        "out vec4 fragColor;\n"
        "void main() {\n"
        "    fragColor = vec4(vColor, 1.0);\n"
        "}\n";

    GLuint vs = CompileShader(GL_VERTEX_SHADER, vert_src);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, frag_src);

    if (vs && fs)
        m_program = LinkProgram(vs, fs);

    if (vs)
        glDeleteShader(vs);

    if (fs)
        glDeleteShader(fs);

    if (m_program)
    {
        static const GLfloat vertices[] =
        {
            -0.6f, -0.4f, 1.0f, 0.0f, 0.0f,
             0.6f, -0.4f, 0.0f, 1.0f, 0.0f,
             0.0f,  0.6f, 0.0f, 0.0f, 1.0f
        };

        glGenVertexArrays(1, &m_vao);
        glBindVertexArray(m_vao);

        glGenBuffers(1, &m_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(vertices)), vertices, GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * static_cast<GLint>(sizeof(GLfloat)), reinterpret_cast<const void*>(0));

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * static_cast<GLint>(sizeof(GLfloat)), reinterpret_cast<const void*>(2 * sizeof(GLfloat)));

        glBindVertexArray(0);

        Msg("[GLES-STUB] shader program linked, glGetError: %d", static_cast<int>(glGetError()));
        Msg("[GLES-STUB] VAO/VBO ready, glGetError: %d", static_cast<int>(glGetError()));
    }
    else
    {
        Msg("[GLES-STUB] shader/link FAILED, triangle disabled, glGetError: %d", static_cast<int>(glGetError()));
    }

    // Texture 16x16 RGBA8. Yellow marker in x<8 && y<8 is intentional to detect
    // flip/rotation. Data row 0 is the GL bottom row.
    static unsigned char tex[16 * 16 * 4];
    for (int y = 0; y < 16; ++y)
    {
        for (int x = 0; x < 16; ++x)
        {
            unsigned char r = 255;
            unsigned char g = 255;
            unsigned char b = 255;
            if (((x / 8) + (y / 8)) & 1)
            {
                g = 0;
                b = 255;
            }
            if (x < 8 && y < 8)
            {
                r = 255;
                g = 255;
                b = 0;
            }
            const int i = ((y * 16) + x) * 4;
            tex[i + 0] = r;
            tex[i + 1] = g;
            tex[i + 2] = b;
            tex[i + 3] = 255;
        }
    }

    DrGLError();
    glActiveTexture(GL_TEXTURE0);
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    Msg("[GLES-STUB] texture uploaded 16x16 RGBA8, glGetError: %d", static_cast<int>(glGetError()));

    static const char* quad_vert_src =
        "#version 300 es\n"
        "layout(location = 0) in vec2 aPos;\n"
        "layout(location = 1) in vec2 aUV;\n"
        "out vec2 vUV;\n"
        "void main() {\n"
        "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
        "    vUV = aUV;\n"
        "}\n";

    static const char* quad_frag_src =
        "#version 300 es\n"
        "precision mediump float;\n"
        "in vec2 vUV;\n"
        "uniform sampler2D uTex;\n"
        "out vec4 fragColor;\n"
        "void main() {\n"
        "    fragColor = texture(uTex, vUV);\n"
        "}\n";

    GLuint qvs = CompileShader(GL_VERTEX_SHADER, quad_vert_src);
    GLuint qfs = CompileShader(GL_FRAGMENT_SHADER, quad_frag_src);

    if (qvs && qfs)
        m_quad_program = LinkProgram(qvs, qfs);

    if (qvs)
        glDeleteShader(qvs);

    if (qfs)
        glDeleteShader(qfs);

    GLint uloc = -1;
    if (m_quad_program)
    {
        DrGLError();
        glUseProgram(m_quad_program);
        uloc = glGetUniformLocation(m_quad_program, "uTex");
        if (uloc >= 0)
            glUniform1i(uloc, 0);
        else
            Msg("[GLES-STUB] WARN uTex location=-1 (fallback unit0 via bind explicito)");
        glUseProgram(0);
        Msg("[GLES-STUB] quad program linked, uTex loc=%d, glGetError: %d", uloc, static_cast<int>(glGetError()));
    }
    else
    {
        Msg("[GLES-STUB] quad shader/link FAILED, texture disabled, glGetError: %d", static_cast<int>(glGetError()));
    }

    if (m_quad_program)
    {
        static const GLfloat qv[] =
        {
            -0.8f, -0.6f, 0.0f, 0.0f,
             0.8f, -0.6f, 2.0f, 0.0f,
            -0.8f,  0.6f, 0.0f, 2.0f,
             0.8f,  0.6f, 2.0f, 2.0f
        };

        DrGLError();
        glGenVertexArrays(1, &m_quad_vao);
        glBindVertexArray(m_quad_vao);
        glGenBuffers(1, &m_quad_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, m_quad_vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(qv)), qv, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * static_cast<GLint>(sizeof(GLfloat)), reinterpret_cast<const void*>(0));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * static_cast<GLint>(sizeof(GLfloat)), reinterpret_cast<const void*>(2 * sizeof(GLfloat)));
        glBindVertexArray(0);
        Msg("[GLES-STUB] quad VAO/VBO ready, glGetError: %d", static_cast<int>(glGetError()));
    }

    Msg("[GLES-STUB] Create exit");
}

    void overdrawBegin() override {}
    void overdrawEnd() override {}

    void DeferredLoad(bool /*E*/) override {}
    void ResourcesDeferredUpload() override {}
    void ResourcesDeferredUnload() override {}
    void ResourcesGetMemoryUsage(u32& m_base, u32& c_base, u32& m_lmaps, u32& c_lmaps) override
    {
        m_base = 0;
        c_base = 0;
        m_lmaps = 0;
        c_lmaps = 0;
    }
    void ResourcesDestroyNecessaryTextures() override {}
    void ResourcesStoreNecessaryTextures() override {}
    void ResourcesDumpMemoryUsage() override {}

    bool HWSupportsShaderYUV2RGB() override { return false; }

    DeviceState GetDeviceState() override { return DeviceState::Normal; }
    bool GetForceGPU_REF() override { return false; }
    u32 GetCacheStatPolys() override { return 0; }

    void OnCameraUpdated() override
{
    if (m_context)
    {
        if (SDL_GL_MakeCurrent(m_window, m_context) != 0)
        {
            Msg("[GLES-STUB] OnCameraUpdated SDL_GL_MakeCurrent failed: %s", SDL_GetError());
        }

        glClearColor(0.08f, 0.18f, 0.35f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (m_quad_program)
        {
            glUseProgram(m_quad_program);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_texture);
            glBindVertexArray(m_quad_vao);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            glBindVertexArray(0);
            glUseProgram(0);

            if (m_frameCount == 0)
            {
                Msg("[GLES-STUB] draw quad glGetError: %d", static_cast<int>(glGetError()));
            }
        }

        SDL_GL_SwapWindow(m_window);
    }

    ++m_frameCount;
    Msg("[GLES-STUB] frame=%d", m_frameCount);

    if (m_frameCount >= kFrameLimit && (SDL_GetTicks() - m_startTicks) >= kMinElapsedMs)
    {
        RequestQuit();
    }
}

    void Begin() override {}
    void Clear() override {}
    void End() override {}
    void ClearTarget() override {}

    void SetCacheXform(Fmatrix& /*mView*/, Fmatrix& /*mProject*/) override {}
    void OnAssetsChanged() override {}

    RenderContext GetCurrentContext() const override
    {
        if (!m_context)
            return NoContext;

        return SDL_GL_GetCurrentContext() == m_context ? PrimaryContext : NoContext;
    }

    void MakeContextCurrent(RenderContext context) override
    {
        if (!m_context)
            return;

        if (context == PrimaryContext)
            SDL_GL_MakeCurrent(m_window, m_context);
        else if (context == NoContext)
            SDL_GL_MakeCurrent(nullptr, nullptr);
    }
};

// Lazily constructed on first use, so nothing here touches the engine allocator
// during dlopen of libxr_3da.so.
CGlesStubRender& RenderStub()
{
    static CGlesStubRender r;
    return r;
}

CGlesStubRenderFactory& FactoryStub()
{
    static CGlesStubRenderFactory f;
    return f;
}

class CGlesStubRendererModule final : public RendererModule
{
public:
    // Must return a reference to persistent storage; EngineAPI.cpp keeps the reference.
    const xr_vector<std::pair<pcstr, int>>& ObtainSupportedModes() override
    {
        static xr_vector<std::pair<pcstr, int>> modes;
        if (modes.empty())
            modes.emplace_back(kRendererMode, 0);

        Msg("[GLES-STUB] ObtainSupportedModes -> %s", kRendererMode);
        return modes;
    }

    bool CheckGameRequirements() override { return true; }

    void SetupEnv(pcstr mode) override
    {
        Msg("[GLES-STUB] SetupEnv mode=%s", mode ? mode : "(null)");

        GEnv.Render = &RenderStub();
        GEnv.RenderFactory = &FactoryStub();

        Msg("[GLES-STUB] GEnv.Render=%p GEnv.RenderFactory=%p", static_cast<void*>(GEnv.Render),
            static_cast<void*>(GEnv.RenderFactory));
    }

    void ClearEnv() override
    {
        if (GEnv.Render == &RenderStub())
            GEnv.Render = nullptr;

        if (GEnv.RenderFactory == &FactoryStub())
            GEnv.RenderFactory = nullptr;
    }
};
} // namespace

RendererModule* GetRendererModule()
{
    static CGlesStubRendererModule module;
    return &module;
}
} // namespace xray::render::android_gles