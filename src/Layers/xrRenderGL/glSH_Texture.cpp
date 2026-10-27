#include "stdafx.h"
#pragma hdrstop

#include "../xrRender/ResourceManager.h"
#include "xrCore/xrMemory.h"

#ifdef XR_PLATFORM_WINDOWS // TODO
#include "xrEngine/tntQAVI.h"
#endif
#include "xrEngine/xrTheora_Surface.h"

#define PRIORITY_HIGH   12
#define PRIORITY_NORMAL 8
#define PRIORITY_LOW    4

namespace xray::render::RENDER_NAMESPACE
{
namespace
{
void ClearGLErrors()
{
    while (glGetError() != GL_NO_ERROR)
    {
    }
}
} // namespace

void resptrcode_texture::create(LPCSTR _name)
{
    _set(RImplementation.Resources->_CreateTexture(_name));
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
CTexture::CTexture()
{
    pSurface = 0;
    pBuffer = 0;
    pAVI = nullptr;
    pTheora = nullptr;
    desc = GL_TEXTURE_2D;
    desc_cache = 0;
    seqMSPF = 0;
    flags.MemoryUsage = 0;
    flags.bLoaded = false;
    flags.bUser = false;
    flags.seqCycles = FALSE;
    m_material = 1.0f;
    bind = fastdelegate::FastDelegate2<CBackend&,u32>(this, &CTexture::apply_load);
}

CTexture::~CTexture()
{
    Unload();
    // release external reference
    RImplementation.Resources->_DeleteTexture(this);
}

void CTexture::surface_set(GLenum target, GLuint surf)
{
    desc = target;
    pSurface = surf;
}

GLuint CTexture::surface_get() const
{
    return pSurface;
}

void CTexture::PostLoad()
{
    if (pTheora) bind = fastdelegate::FastDelegate2<CBackend&,u32>(this, &CTexture::apply_theora);
    else if (pAVI) bind = fastdelegate::FastDelegate2<CBackend&,u32>(this, &CTexture::apply_avi);
    else if (!seqDATA.empty()) bind = fastdelegate::FastDelegate2<CBackend&,u32>(this, &CTexture::apply_seq);
    else bind = fastdelegate::FastDelegate2<CBackend&,u32>(this, &CTexture::apply_normal);
}

void CTexture::apply_load(CBackend& cmd_list, u32 dwStage)
{
    CHK_GL(glActiveTexture(GL_TEXTURE0 + dwStage));
    if (!flags.bLoaded) Load();
    else PostLoad();
    bind(cmd_list, dwStage);
};

void CTexture::apply_theora(CBackend& cmd_list, u32 dwStage)
{
    if (!pTheora || !pSurface)
        return;

    if (!pTheora->Update(m_play_time != 0xFFFFFFFF ? m_play_time : Device.dwTimeContinual))
        return;

    const u32 w = pTheora->Width(true);
    const u32 h = pTheora->Height(true);
    if (!w || !h)
        return;

    static xr_vector<u32> frame;
    frame.resize(static_cast<size_t>(w) * static_cast<size_t>(h));

    int pos = 0;
    pTheora->DecompressFrame(frame.data(), 0, pos);

    const bool video_diag = strstr(Core.Params, "-video_diag") != nullptr;

#if defined(__ANDROID__)
    // Android GLES upload uses GL_RGBA. CPU path produces BGRA (color_rgba = ARGB in memory = BGRA bytes).
    // Convert BGRA to RGBA with exactly one R<->B swap.
    for (size_t idx = 0; idx < frame.size(); ++idx)
    {
        u32 px = frame[idx];
        px = (px & 0xff00ff00u) | ((px & 0x00ff0000u) >> 16) | ((px & 0x000000ffu) << 16);
        frame[idx] = px;
    }
    const GLenum format = GL_RGBA;
#else
    // Desktop GL uses GL_BGRA, matches CPU BGRA byte order.
    const GLenum format = GL_BGRA;
#endif

    glBindTexture(desc, pSurface);

    static u32 s_upload_count = 0;
    ++s_upload_count;

    const bool log_this_frame = (s_upload_count <= 5) || (s_upload_count % 30 == 0);

    while (glGetError() != GL_NO_ERROR) {}

    const GLenum err_before = glGetError();

    CHK_GL(glTexSubImage2D(
        desc, 0, 0, 0,
        static_cast<GLsizei>(w), static_cast<GLsizei>(h),
        format, GL_UNSIGNED_BYTE, frame.data()));

    const GLenum err_after = glGetError();

    const u32 tm_play = (m_play_time != 0xFFFFFFFF) ? m_play_time : Device.dwTimeContinual;

    TheoraTelemetry telemetry;
    pTheora->GetTelemetry(telemetry);

    if (log_this_frame)
    {
        Msg("[VIDEODIAG] Theora upload #%u: tm_play=%u, d_frame=%u, granulepos=%lld, glErr_before=0x%x, glErr_after=0x%x",
            s_upload_count, tm_play, telemetry.d_frame, telemetry.granulepos, err_before, err_after);
    }

    if (!frame.empty())
    {
        const size_t center_idx = (static_cast<size_t>(h) / 2) * static_cast<size_t>(w) + (static_cast<size_t>(w) / 2);
        const u32 cpu_center_px = (center_idx < frame.size()) ? frame[center_idx] : 0;
        const u32 cpu_px0 = frame[0];
        const u32 cpu_px1 = (frame.size() > 1) ? frame[1] : 0;
        const u32 cpu_px2 = (frame.size() > 2) ? frame[2] : 0;
        const u32 cpu_px3 = (frame.size() > 3) ? frame[3] : 0;

        if (telemetry.y_ptr)
        {
            u64 sumY = 0;
            u32 countY = 0;
            const ptrdiff_t strideY = telemetry.y_stride;
            for (u32 yy = 0; yy < telemetry.height; yy += 8)
            {
                const u8* row = telemetry.y_ptr + strideY * static_cast<ptrdiff_t>(yy);
                for (u32 xx = 0; xx < telemetry.width; xx += 8)
                {
                    sumY += row[xx];
                    ++countY;
                }
            }
            const u32 avgY = countY ? static_cast<u32>(sumY / countY) : 0;
            if (log_this_frame)
            {
                Msg("[VIDEODIAG]   CPU: avgY=%u (black=16, zero=0), y_stride=%td, uv_stride=%td, y_ptr=%p, u_ptr=%p, v_ptr=%p, px0=%08x px1=%08x px2=%08x px3=%08x, center=%08x",
                    avgY, telemetry.y_stride, telemetry.uv_stride, telemetry.y_ptr, telemetry.u_ptr, telemetry.v_ptr, cpu_px0, cpu_px1, cpu_px2, cpu_px3, cpu_center_px);
            }

            GLuint prev_draw_fbo = 0, prev_read_fbo = 0, prev_tex = 0;
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, reinterpret_cast<GLint*>(&prev_draw_fbo));
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, reinterpret_cast<GLint*>(&prev_read_fbo));
            glGetIntegerv(GL_TEXTURE_BINDING_2D, reinterpret_cast<GLint*>(&prev_tex));
            GLint prev_active_texture = 0;
            glGetIntegerv(GL_ACTIVE_TEXTURE, &prev_active_texture);

            GLuint fbo = 0;
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, desc, pSurface, 0);

            u8 readback[4] = {0};
            if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE)
            {
                glReadPixels(w / 2, h / 2, 1, 1, format, GL_UNSIGNED_BYTE, readback);
            }

            glBindFramebuffer(GL_READ_FRAMEBUFFER, prev_read_fbo);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prev_draw_fbo);
            glBindTexture(GL_TEXTURE_2D, prev_tex);
            glActiveTexture(static_cast<GLenum>(prev_active_texture));
            glDeleteFramebuffers(1, &fbo);

            // Pack readback to match CPU format (ARGB in memory = 0xAARRGGBB)
            // Android: format=GL_RGBA, readback bytes = R,G,B,A -> pack as A<<24 | R<<16 | G<<8 | B
            // Desktop: format=GL_BGRA, readback bytes = B,G,R,A -> pack as A<<24 | R<<16 | G<<8 | B
            u32 gpu_px;
#if defined(__ANDROID__)
            gpu_px = (readback[3] << 24) | (readback[0] << 16) | (readback[1] << 8) | readback[2];
#else
            gpu_px = (readback[3] << 24) | (readback[2] << 16) | (readback[1] << 8) | readback[0];
#endif

            if (log_this_frame)
            {
                Msg("[VIDEODIAG]   GPU readback center: raw_bytes=%02x%02x%02x%02x, packed_cpu_fmt=%08x vs CPU center=%08x",
                    readback[0], readback[1], readback[2], readback[3], gpu_px, cpu_center_px);
            }
        }
        else if (log_this_frame)
        {
            Msg("[VIDEODIAG]   CPU: px0=%08x px1=%08x px2=%08x px3=%08x, center=%08x",
                cpu_px0, cpu_px1, cpu_px2, cpu_px3, cpu_center_px);
        }
    }

    static bool s_logged_first_frame = false;
    if (!s_logged_first_frame && !frame.empty())
    {
        Msg("* GL: Theora first frame uploaded: %ux%u px=%08x",
            static_cast<unsigned int>(w),
            static_cast<unsigned int>(h),
            static_cast<unsigned int>(frame[0]));
        s_logged_first_frame = true;
    }
}

;

void CTexture::apply_avi(CBackend& cmd_list, u32 dwStage) const
{
    CHK_GL(glActiveTexture(GL_TEXTURE0 + dwStage));
    CHK_GL(glBindTexture(desc, pSurface));

#ifdef XR_PLATFORM_WINDOWS // TODO
    if (pAVI->NeedUpdate())
    {
        // AVI
        u8* ptr{};
        pAVI->GetFrame(&ptr);
        CHK_GL(glTexSubImage2D(desc, 0, 0, 0, m_width, m_height,
            GL_RGBA, GL_UNSIGNED_BYTE, ptr));
    }
#endif
};

void CTexture::apply_seq(CBackend& cmd_list, u32 dwStage)
{
    // SEQ
    u32 frame = Device.dwTimeContinual / seqMSPF; //Device.dwTimeGlobal
    u32 frame_data = seqDATA.size();
    if (flags.seqCycles)
    {
        u32 frame_id = frame % (frame_data * 2);
        if (frame_id >= frame_data) frame_id = frame_data - 1 - frame_id % frame_data;
        pSurface = seqDATA[frame_id];
    }
    else
    {
        u32 frame_id = frame % frame_data;
        pSurface = seqDATA[frame_id];
    }

    CHK_GL(glActiveTexture(GL_TEXTURE0 + dwStage));
    CHK_GL(glBindTexture(desc, pSurface));
};

void CTexture::apply_normal(CBackend& cmd_list, u32 dwStage) const
{
    CHK_GL(glActiveTexture(GL_TEXTURE0 + dwStage));
    CHK_GL(glBindTexture(desc, pSurface));
};

void CTexture::Preload()
{
    m_bumpmap = RImplementation.Resources->m_textures_description.GetBumpName(cName);
    m_material = RImplementation.Resources->m_textures_description.GetMaterial(cName);
}

void CTexture::Load()
{
    flags.bLoaded = true;
    desc_cache = 0;
    if (pSurface) return;

    flags.bUser = false;
    flags.MemoryUsage = 0;
    if (nullptr == cName.c_str())
        return;
    if (0 == xr_stricmp(cName.c_str(), "$null")) return;
    // we need to check only the beginning of the string,
    // so let's use strncmp instead of strstr.
    if (0 == strncmp(cName.c_str(), "$user$", sizeof("$user$") - 1))
    {
        flags.bUser = true;
        return;
    }

    ZoneScoped;

    Preload();

    // Check for OGM
    string_path fn;
    if (FS.exist(fn, "$game_textures$", cName.c_str(), ".ogm"))
    {
        pTheora = xr_new<CTheoraSurface>();
        m_play_time = 0xFFFFFFFF;

        if (!pTheora->Load(fn))
        {
            Msg("! GL: failed to open video stream: %s", fn);
            xr_delete(pTheora);
            pTheora = nullptr;
            pSurface = 0;
            return;
        }

        flags.MemoryUsage = pTheora->Width(true) * pTheora->Height(true) * 4;
        pTheora->Play(TRUE, Device.dwTimeContinual);

        GLuint pTexture = 0;
#ifdef XR_PLATFORM_GLES
        // GLES UI has no YUV2RGB shader and no pow2 padding requirement;
        // use the real video size so the frame maps 1:1 without letterbox.
        u32 _w = pTheora->Width(true);
        u32 _h = pTheora->Height(true);
#else
        u32 _w = pTheora->Width(false);
        u32 _h = pTheora->Height(false);
#endif

        ClearGLErrors();

        // Direct CPU upload path; avoids PBO/BGRA pitfalls on Android GLES.
        pBuffer = 0;

        const texture_upload_unit uploadUnit(GL_TEXTURE_2D);
        glGenTextures(1, &pTexture);
        glBindTexture(GL_TEXTURE_2D, pTexture);
        CHK_GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0));
        CHK_GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0));
        CHK_GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
        CHK_GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        CHK_GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
        CHK_GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));

#ifdef XR_PLATFORM_GLES
        CHK_GL(glTexImage2D(
            GL_TEXTURE_2D, 0, GL_RGBA8,
            static_cast<GLsizei>(_w), static_cast<GLsizei>(_h),
            0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr));
#else
        CHK_GL(glTexImage2D(
            GL_TEXTURE_2D, 0, GL_RGBA8,
            static_cast<GLsizei>(_w), static_cast<GLsizei>(_h),
            0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr));
#endif

        pSurface = pTexture;
        desc = GL_TEXTURE_2D;
        size_set(_w, _h);

        // Initialize video texture to opaque black to avoid uninitialized green/garbage frames.
        if (_w && _h)
        {
            static xr_vector<u32> black;
            black.assign(static_cast<size_t>(_w) * static_cast<size_t>(_h), 0xFF000000u);

            glBindTexture(GL_TEXTURE_2D, pTexture);

#ifdef XR_PLATFORM_GLES
            CHK_GL(glTexSubImage2D(
                GL_TEXTURE_2D, 0, 0, 0,
                static_cast<GLsizei>(_w), static_cast<GLsizei>(_h),
                GL_RGBA, GL_UNSIGNED_BYTE, black.data()));
#else
            CHK_GL(glTexSubImage2D(
                GL_TEXTURE_2D, 0, 0, 0,
                static_cast<GLsizei>(_w), static_cast<GLsizei>(_h),
                GL_BGRA, GL_UNSIGNED_BYTE, black.data()));
#endif
        }

        const GLenum err = glGetError();
        if (err != GL_NO_ERROR)
        {
            Msg("! GL: invalid video stream: 0x%x", static_cast<unsigned int>(err));
            xr_delete(pTheora);
            pTheora = nullptr;
            pSurface = 0;
            return;
        }

        static bool s_logged_video_enabled = false;
        if (!s_logged_video_enabled)
        {
            Msg("* GL: Theora video texture enabled: %s", fn);
            s_logged_video_enabled = true;
        }

#ifdef __ANDROID__
        // Track GPU texture memory for video textures (mutually exclusive path, not via texture_load)
        if (flags.MemoryUsage > 0)
            ::Memory.track_mem(xrMemory::MEMCAT_GPU_TEX_LIVE, flags.MemoryUsage / 1024);
        
        // One-shot log for gpu_tex-path=theora (RISK B - confirm mutual exclusivity)
        static bool s_gpu_tex_path_theora_logged = false;
        if (!s_gpu_tex_path_theora_logged && flags.MemoryUsage > 1024 * 1024)
        {
            Msg("* MEM: gpu_tex-path=theora tex=%s size_kb=%zu", cName.c_str(), flags.MemoryUsage / 1024);
            s_gpu_tex_path_theora_logged = true;
        }
#endif

    }
    else if (FS.exist(fn, "$game_textures$", cName.c_str(), ".avi"))
    {
#ifdef XR_PLATFORM_WINDOWS // TODO
        // AVI
        pAVI = xr_new<CAviPlayerCustom>();

        if (!pAVI->Load(fn))
        {
            xr_delete(pAVI);
            FATAL("Can't open video stream");
        }
        else
        {
            flags.MemoryUsage = pAVI->m_dwWidth * pAVI->m_dwHeight * 4;

                // Create pixel buffer object
                glGenBuffers(1, &pBuffer);
                glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pBuffer);
                CHK_GL(glBufferData(GL_PIXEL_UNPACK_BUFFER, flags.MemoryUsage, nullptr, GL_STREAM_DRAW));

                // Now create texture to copy PBO into
                GLuint pTexture = 0;
                const texture_upload_unit uploadUnit(GL_TEXTURE_2D);
                glGenTextures(1, &pTexture);
                glBindTexture(GL_TEXTURE_2D, pTexture);
                CHK_GL(glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, pAVI->m_dwWidth, pAVI->m_dwHeight));

                pSurface = pTexture;
                desc = GL_TEXTURE_2D;
                size_set(pAVI->m_dwWidth, pAVI->m_dwHeight);
                if (glGetError() != GL_NO_ERROR)
                {
                    FATAL("Invalid video stream");
                    xr_delete(pAVI);
                    pSurface = 0;
                }

#ifdef __ANDROID__
                // Track GPU texture memory for AVI video textures (mutually exclusive path, not via texture_load)
                if (flags.MemoryUsage > 0)
                    ::Memory.track_mem(xrMemory::MEMCAT_GPU_TEX_LIVE, flags.MemoryUsage / 1024);
                
                // One-shot log for gpu_tex-path=avi (RISK B - confirm mutual exclusivity)
                static bool s_gpu_tex_path_avi_logged = false;
                if (!s_gpu_tex_path_avi_logged && flags.MemoryUsage > 1024 * 1024)
                {
                    Msg("* MEM: gpu_tex-path=avi tex=%s size_kb=%zu", cName.c_str(), flags.MemoryUsage / 1024);
                    s_gpu_tex_path_avi_logged = true;
                }
#endif
            }
#endif
    }
    else if (FS.exist(fn, "$game_textures$", cName.c_str(), ".seq"))
    {
        // Sequence
        string256 buffer;
        IReader* _fs = FS.r_open(fn);

        flags.seqCycles = FALSE;
        _fs->r_string(buffer, sizeof buffer);
        if (0 == xr_stricmp(buffer, "cycled"))
        {
            flags.seqCycles = TRUE;
            _fs->r_string(buffer, sizeof buffer);
        }
        u32 fps = atoi(buffer);
        seqMSPF = 1000 / fps;

        while (!_fs->eof())
        {
            _fs->r_string(buffer, sizeof buffer);
            _Trim(buffer);
            if (buffer[0])
            {
                // Load another texture
                u32 mem = 0;
                pSurface = RImplementation.texture_load(buffer, mem, desc, m_loaded_width, m_loaded_height);
                if (pSurface)
                {
                    // pSurface->SetPriority	(PRIORITY_LOW);
                    seqDATA.push_back(pSurface);
                    flags.MemoryUsage += mem;
                }
            }
        }
        pSurface = 0;
        FS.r_close(_fs);
    }
    else
    {
        // Normal texture
        u32 mem = 0;
        pSurface = RImplementation.texture_load(cName.c_str(), mem, desc, m_loaded_width, m_loaded_height);

        // Calc memory usage and preload into vid-mem
        if (pSurface)
        {
            // pSurface->SetPriority	(PRIORITY_NORMAL);
            flags.MemoryUsage = mem;
        }
    }

    PostLoad();
}

void CTexture::Unload()
{
    ZoneScoped;
#ifdef DEBUG
    string_path				msg_buff;
    sprintf_s(msg_buff, sizeof(msg_buff), "* Unloading texture [%s] pSurface ID=%d", cName.c_str(), pSurface);
#endif // DEBUG

    //.	if (flags.bLoaded)		Msg		("* Unloaded: %s",cName.c_str());

    flags.bLoaded = FALSE;
#ifdef __ANDROID__
    if (flags.MemoryUsage > 0)
    {
        ::Memory.track_mem(xrMemory::MEMCAT_GPU_TEX_LIVE, -(long long)(flags.MemoryUsage / 1024));
        
        // One-shot log for gpu_tex-hook unload (RISK C - confirm subtraction happens)
        static bool s_gpu_tex_unload_logged = false;
        if (!s_gpu_tex_unload_logged && flags.MemoryUsage > 1024 * 1024)
        {
            Msg("* MEM: gpu_tex-hook fired unload tex=%s size_kb=%zu", cName.c_str(), flags.MemoryUsage / 1024);
            s_gpu_tex_unload_logged = true;
        }
    }
#endif
    if (!seqDATA.empty())
    {
        CHK_GL(glDeleteTextures(seqDATA.size(), seqDATA.data()));
        seqDATA.clear();
        pSurface = 0;
    }

    CHK_GL(glDeleteTextures(1, &pSurface));
    CHK_GL(glDeleteBuffers(1, &pBuffer));

#ifdef XR_PLATFORM_WINDOWS
    xr_delete(pAVI);
#endif
    xr_delete(pTheora);

    bind = fastdelegate::FastDelegate2<CBackend&,u32>(this, &CTexture::apply_load);
}

void CTexture::desc_update()
{
    desc_cache = pSurface;
    if (pSurface && (GL_TEXTURE_2D == desc || GL_TEXTURE_2D_MULTISAMPLE == desc))
    {
#ifdef XR_PLATFORM_WEB
        m_width = m_loaded_width;
        m_height = m_loaded_height;
#else
        glBindTexture(desc, pSurface);
        CHK_GL(glGetTexLevelParameteriv(desc, 0, GL_TEXTURE_WIDTH, &m_width));
        CHK_GL(glGetTexLevelParameteriv(desc, 0, GL_TEXTURE_HEIGHT, &m_height));
#endif
    }
}

void CTexture::video_Play(BOOL looped, u32 _time)
{
    if (pTheora) pTheora->Play(looped, _time != 0xFFFFFFFF ? (m_play_time = _time) : Device.dwTimeContinual);
}

void CTexture::video_Pause(BOOL state) const
{
    if (pTheora) pTheora->Pause(state);
}

void CTexture::video_Stop() const
{
    if (pTheora) pTheora->Stop();
}

BOOL CTexture::video_IsPlaying() const
{
    return pTheora ? pTheora->IsPlaying() : FALSE;
}
} // namespace xray::render::RENDER_NAMESPACE
