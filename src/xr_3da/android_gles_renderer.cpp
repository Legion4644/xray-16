#include "stdafx.h"

#include "android_gles_renderer.h"

#include "xrEngine/Render.h"
#include "xrEngine/EngineAPI.h"
#include "Include/xrRender/RenderFactory.h"
#include "Include/xrRender/ImGuiRender.h"
#include "Include/xrRender/FontRender.h"
#include "Include/xrRender/StatGraphRender.h"
#include "Include/xrRender/UISequenceVideoItem.h"
#include "Include/xrRender/UIShader.h"
#include "Include/xrRender/WallMarkArray.h"
#include "Include/xrRender/EnvironmentRender.h"
#include "Include/xrRender/RainRender.h"
#include "Include/xrRender/LensFlareRender.h"
#include "Include/xrRender/ThunderboltRender.h"
#include "Include/xrRender/ThunderboltDescRender.h"
#ifdef DEBUG
#include "Include/xrRender/ObjectSpaceRender.h"
#endif
#include "Include/xrAPI/xrAPI.h"

#include <SDL.h>
#include <GLES3/gl3.h>

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

class CGlesStubRenderFactory final : public IRenderFactory
{
public:
    IUISequenceVideoItem* CreateUISequenceVideoItem() override { return nullptr; }
    void DestroyUISequenceVideoItem(IUISequenceVideoItem* pObject) override { xr_delete(pObject); }

    IUIShader* CreateUIShader() override { return nullptr; }
    void DestroyUIShader(IUIShader* pObject) override { xr_delete(pObject); }

    IStatGraphRender* CreateStatGraphRender() override { return xr_new<CGlesStubStatGraphRender>(); }
    void DestroyStatGraphRender(IStatGraphRender* pObject) override { xr_delete(pObject); }

#ifdef DEBUG
    IObjectSpaceRender* CreateObjectSpaceRender() override { return nullptr; }
    void DestroyObjectSpaceRender(IObjectSpaceRender* pObject) override { xr_delete(pObject); }
#endif

    IWallMarkArray* CreateWallMarkArray() override { return nullptr; }
    void DestroyWallMarkArray(IWallMarkArray* pObject) override { xr_delete(pObject); }

    IEnvironmentRender* CreateEnvironmentRender() override { return nullptr; }
    void DestroyEnvironmentRender(IEnvironmentRender* pObject) override { xr_delete(pObject); }

    IEnvDescriptorRender* CreateEnvDescriptorRender() override { return nullptr; }
    void DestroyEnvDescriptorRender(IEnvDescriptorRender* pObject) override { xr_delete(pObject); }

    IRainRender* CreateRainRender() override { return nullptr; }
    void DestroyRainRender(IRainRender* pObject) override { xr_delete(pObject); }

    ILensFlareRender* CreateLensFlareRender() override { return nullptr; }
    void DestroyLensFlareRender(ILensFlareRender* pObject) override { xr_delete(pObject); }

    IImGuiRender* CreateImGuiRender() override { return xr_new<CGlesStubImGuiRender>(); }
    void DestroyImGuiRender(IImGuiRender* pObject) override { xr_delete(pObject); }

    IThunderboltRender* CreateThunderboltRender() override { return nullptr; }
    void DestroyThunderboltRender(IThunderboltRender* pObject) override { xr_delete(pObject); }

    IThunderboltDescRender* CreateThunderboltDescRender() override { return nullptr; }
    void DestroyThunderboltDescRender(IThunderboltDescRender* pObject) override { xr_delete(pObject); }

    IFlareRender* CreateFlareRender() override { return nullptr; }
    void DestroyFlareRender(IFlareRender* pObject) override { xr_delete(pObject); }

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

class CGlesStubRendererModule final : public RendererModule
{
    xr_vector<std::pair<pcstr, int>> m_modes{ { kRendererMode, 0 } };

public:
    const xr_vector<std::pair<pcstr, int>>& ObtainSupportedModes() override
    {
        Msg("[GLES-STUB] ObtainSupportedModes -> %s", kRendererMode);
        return m_modes;
    }

    bool CheckGameRequirements() override { return true; }

    void SetupEnv(pcstr mode) override
    {
        Msg("[GLES-STUB] SetupEnv mode=%s", mode ? mode : "(null)");

        GEnv.Render = &s_render;
        GEnv.RenderFactory = &s_factory;

        Msg("[GLES-STUB] GEnv.Render=%p GEnv.RenderFactory=%p", static_cast<void*>(GEnv.Render),
            static_cast<void*>(GEnv.RenderFactory));
    }

    void ClearEnv() override
    {
        if (GEnv.Render == &s_render)
            GEnv.Render = nullptr;

        if (GEnv.RenderFactory == &s_factory)
            GEnv.RenderFactory = nullptr;
    }

    static CGlesStubRender s_render;
    static CGlesStubRenderFactory s_factory;
};

CGlesStubRender CGlesStubRendererModule::s_render;
CGlesStubRenderFactory CGlesStubRendererModule::s_factory;

CGlesStubRendererModule g_module;
} // namespace

RendererModule* GetRendererModule()
{
    return &g_module;
}
} // namespace xray::render::android_gles
