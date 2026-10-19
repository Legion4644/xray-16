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
#include <utility>

namespace xray::render::android_gles
{
namespace
{
constexpr pcstr kRendererMode = "renderer_gles";
constexpr int kFrameLimit = 3;

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
    void Destroy() override { m_window = nullptr; }
    void Reset(SDL_Window* hWnd, u32& /*dwWidth*/, u32& /*dwHeight*/, float& /*fWidth_2*/, float& /*fHeight_2*/) override
    {
        m_window = hWnd;
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

    void SetupStates() override {}
    void OnDeviceCreate(pcstr /*shName*/) override {}

    void Create(SDL_Window* hWnd, u32& /*dwWidth*/, u32& /*dwHeight*/, float& /*fWidth_2*/, float& /*fHeight_2*/) override
    {
        m_window = hWnd;
        Msg("[GLES-STUB] Create window=%p (no GL context yet)", static_cast<void*>(hWnd));
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
        ++m_frameCount;
        Msg("[GLES-STUB] frame=%d", m_frameCount);

        if (m_frameCount >= kFrameLimit)
            RequestQuit();
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