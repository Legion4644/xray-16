// glHW.cpp: implementation of the OpenGL specialisation of CHW.
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#pragma hdrstop

#include "glHW.h"
#include "xrEngine/XR_IOConsole.h"

#include <vector>
#include <cstdio>

namespace xray::render::RENDER_NAMESPACE
{
CHW HW;

void CALLBACK OnDebugCallback(GLenum /*source*/, GLenum /*type*/, GLuint id, GLenum severity, GLsizei /*length*/,
    const GLchar* message, const void* /*userParam*/)
{
    if (severity != GL_DEBUG_SEVERITY_NOTIFICATION)
        Log(message, id);
}

static_assert(std::is_same_v<decltype(&OnDebugCallback), GLDEBUGPROC>);

void UpdateVSync()
{
#ifdef XR_PLATFORM_WEB
    return; // presentation is paced by the browser
#endif
    if (psDeviceFlags.test(rsVSync))
    {
        // Try adaptive vsync first
        if (SDL_GL_SetSwapInterval(-1) == -1)
            SDL_GL_SetSwapInterval(1);
    }
    else
    {
        SDL_GL_SetSwapInterval(0);
    }
}

CHW::CHW()
{
    if (!ThisInstanceIsGlobal())
        return;

    Device.seqAppActivate.Add(this);
    Device.seqAppDeactivate.Add(this);
}

CHW::~CHW()
{
    if (!ThisInstanceIsGlobal())
        return;

    Device.seqAppActivate.Remove(this);
    Device.seqAppDeactivate.Remove(this);
}

void CHW::OnAppActivate()
{
    if (m_window)
    {
        SDL_RestoreWindow(m_window);
    }
}

void CHW::OnAppDeactivate()
{
    if (m_window)
    {
        if (psDeviceMode.WindowStyle == rsFullscreen || psDeviceMode.WindowStyle == rsFullscreenBorderless)
            SDL_MinimizeWindow(m_window);
    }
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
#if defined(__ANDROID__) && defined(XR_PLATFORM_GLES)
#include <gli/gli.hpp>
#include <gli/gl.hpp>

namespace
{
bool g_gles_has_etc2 = false;
bool g_gles_has_astc_4x4 = false;
bool g_gles_has_astc_4x4_srgb = false;

static void DrGLErrorLocal() { while (glGetError() != GL_NO_ERROR) {} }

static GLuint CompileShaderLocal(GLenum type, const char* src)
{
    GLuint shader = glCreateShader(type);
    if (!shader)
    {
        Msg("[GLES-PROBE] glCreateShader failed, glGetError: %d", static_cast<int>(glGetError()));
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
        Msg("[GLES-PROBE] shader compile failed: %s", log);
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

static GLuint LinkProgramLocal(GLuint vs, GLuint fs)
{
    GLuint program = glCreateProgram();
    if (!program)
    {
        Msg("[GLES-PROBE] glCreateProgram failed, glGetError: %d", static_cast<int>(glGetError()));
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
        Msg("[GLES-PROBE] program link failed: %s", log);
        glDeleteProgram(program);
        return 0;
    }

    return program;
}

static int ProbeCompressedTextureFormatLocal(const char* name, GLenum internalformat,
    GLsizei width, GLsizei height, GLsizei imageSize)
{
    GLuint tex = 0;
    glGenTextures(1, &tex);
    if (!tex)
    {
        Msg("! PROBE_COMPRESSED name=%s fmt=0x%X upload=FAIL err=0x%X (gen failed)",
            name, static_cast<unsigned int>(internalformat), static_cast<unsigned int>(glGetError()));
        return -1;
    }

    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    std::vector<unsigned char> data(imageSize, 0);

    DrGLErrorLocal();
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, internalformat, width, height, 0, imageSize, data.data());
    GLenum err = glGetError();

    if (err != GL_NO_ERROR)
    {
        Msg("! PROBE_COMPRESSED name=%s fmt=0x%X upload=FAIL err=0x%X",
            name, static_cast<unsigned int>(internalformat), static_cast<unsigned int>(err));
        glDeleteTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, 0);
        return 0;
    }

    static const char* vs_src =
        "#version 300 es\n"
        "layout(location = 0) in vec2 aPos;\n"
        "layout(location = 1) in vec2 aUV;\n"
        "out vec2 vUV;\n"
        "void main() {\n"
        "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
        "    vUV = aUV;\n"
        "}\n";

    static const char* fs_src =
        "#version 300 es\n"
        "precision mediump float;\n"
        "in vec2 vUV;\n"
        "uniform sampler2D u_tex;\n"
        "out vec4 fragColor;\n"
        "void main() {\n"
        "    fragColor = texture(u_tex, vec2(0.5, 0.5));\n"
        "}\n";

    GLuint vs = CompileShaderLocal(GL_VERTEX_SHADER, vs_src);
    GLuint fs = CompileShaderLocal(GL_FRAGMENT_SHADER, fs_src);
    GLuint program = LinkProgramLocal(vs, fs);

    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);

    if (!program)
    {
        Msg("! PROBE_COMPRESSED name=%s fmt=0x%X upload=OK shader=FAIL",
            name, static_cast<unsigned int>(internalformat));
        glDeleteTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, 0);
        return 0;
    }

    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    GLuint colorTex = 0;
    glGenTextures(1, &colorTex);
    glBindTexture(GL_TEXTURE_2D, colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);

    GLint fbStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (fbStatus != GL_FRAMEBUFFER_COMPLETE)
    {
        Msg("! PROBE_COMPRESSED name=%s fmt=0x%X upload=OK draw=FAIL fbo=0x%X",
            name, static_cast<unsigned int>(internalformat), static_cast<unsigned int>(fbStatus));
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &colorTex);
        glDeleteProgram(program);
        glDeleteTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return 0;
    }

    glViewport(0, 0, 4, 4);

    glUseProgram(program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    GLint uloc = glGetUniformLocation(program, "u_tex");
    if (uloc >= 0)
        glUniform1i(uloc, 0);

    static const GLfloat quad[] =
    {
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
         1.0f,  1.0f, 1.0f, 1.0f
    };

    GLuint vao = 0, vbo = 0;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), reinterpret_cast<const void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), reinterpret_cast<const void*>(2 * sizeof(GLfloat)));

    DrGLErrorLocal();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    GLenum drawErr = glGetError();

    if (drawErr != GL_NO_ERROR)
    {
        Msg("! PROBE_COMPRESSED name=%s fmt=0x%X upload=OK draw=FAIL err=0x%X",
            name, static_cast<unsigned int>(internalformat), static_cast<unsigned int>(drawErr));
        glDeleteBuffers(1, &vbo);
        glDeleteVertexArrays(1, &vao);
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &colorTex);
        glDeleteProgram(program);
        glDeleteTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glUseProgram(0);
        return 0;
    }

    unsigned char pixel[4] = { 0, 0, 0, 0 };
    glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    GLenum readErr = glGetError();

    Msg("* PROBE_COMPRESSED name=%s fmt=0x%X upload=OK draw=OK read=OK pixel=rgba(%d,%d,%d,%d) err=0x%X",
        name, static_cast<unsigned int>(internalformat),
        pixel[0], pixel[1], pixel[2], pixel[3],
        static_cast<unsigned int>(readErr));

    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &colorTex);
    glDeleteProgram(program);
    glDeleteTextures(1, &tex);

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glUseProgram(0);
    glActiveTexture(GL_TEXTURE0);

    return 1;
}

static void AndroidRunCompressedTextureProbeOnce()
{
    static bool s_done = false;
    if (s_done)
        return;
    s_done = true;

    FILE* f = fopen("/sdcard/XRay/gl_caps_probe.txt", "w");
    bool file_ok = (f != nullptr);
    if (!file_ok)
    {
        Msg("! PROBE_COMPRESSED: fopen gl_caps_probe.txt failed, only Msg used");
    }

    auto probe_and_log = [&](const char* name, GLenum fmt, GLsizei w, GLsizei h, GLsizei imgSize, int& out) -> int
    {
        int result = ProbeCompressedTextureFormatLocal(name, fmt, w, h, imgSize);
        out = (result == 1) ? 1 : (result == -1 ? -1 : 0);
        if (file_ok)
        {
            fprintf(f, "PROBE_COMPRESSED name=%s fmt=0x%X result=%d\n",
                name, static_cast<unsigned int>(fmt), out);
        }
        return result;
    };

    int dxt1_rgb = 0, dxt1_rgba = 0, dxt3_rgba = 0, dxt5_rgba = 0;
    int etc2_rgb = 0, etc2_rgba = 0, astc_4x4 = 0, astc_4x4_srgb = 0;

    probe_and_log("DXT1_RGB", 0x83F0, 4, 4, 8, dxt1_rgb);
    probe_and_log("DXT1_RGBA", 0x83F1, 4, 4, 8, dxt1_rgba);
    probe_and_log("DXT3_RGBA", 0x83F2, 4, 4, 16, dxt3_rgba);
    probe_and_log("DXT5_RGBA", 0x83F3, 4, 4, 16, dxt5_rgba);
    probe_and_log("ETC2_RGB8", 0x9274, 4, 4, 8, etc2_rgb);
    probe_and_log("ETC2_RGBA8", 0x9278, 4, 4, 16, etc2_rgba);
    probe_and_log("ASTC_4x4_LDR", 0x93B0, 4, 4, 16, astc_4x4);
    probe_and_log("ASTC_4x4_SRGB", 0x93D0, 4, 4, 16, astc_4x4_srgb);

    if (file_ok)
    {
        fprintf(f, "PROBE_COMPRESSED_SUMMARY: dxt1_rgb=%d dxt1_rgba=%d dxt3_rgba=%d dxt5_rgba=%d etc2_rgb=%d etc2_rgba=%d astc4=%d astc4_srgb=%d\n",
            dxt1_rgb, dxt1_rgba, dxt3_rgba, dxt5_rgba, etc2_rgb, etc2_rgba, astc_4x4, astc_4x4_srgb);
        fprintf(f, "GL-COMPRESSED-REAL-PROBE: done\n");
        fclose(f);
    }

    g_gles_has_etc2 = (etc2_rgb == 1 && etc2_rgba == 1);
    g_gles_has_astc_4x4 = (astc_4x4 == 1);
    g_gles_has_astc_4x4_srgb = (astc_4x4_srgb == 1);

    Msg("* GLES_CAPS: etc2=%d astc_4x4=%d astc_4x4_srgb=%d",
        g_gles_has_etc2, g_gles_has_astc_4x4, g_gles_has_astc_4x4_srgb);

    Msg("* GL-COMPRESSED-REAL-PROBE: done");
}

static void AndroidRunCompressedFileCanaryOnce()
{
    static bool s_done = false;
    if (s_done)
        return;
    s_done = true;

    FILE* f = fopen("/sdcard/XRay/gl_caps_canary.txt", "w");
    bool file_ok = (f != nullptr);
    if (!file_ok)
    {
        Msg("! CANARY_COMPRESSED: fopen gl_caps_canary.txt failed, only Msg used");
    }

    struct CanaryResult
    {
        int etc2_rgb_ktx = -1;
        int etc2_srgb_ktx = -1;
        int astc4_srgb_ktx = -1;
        int etc2_rgb_dds = -1;
        int etc2_srgb_dds = -1;
        int astc4_srgb_dds = -1;
        int astc8_dds = -1;
    } results;

    const struct TestFile
    {
        const char* path;
        const char* name;
        int CanaryResult::* result_field;
    } test_files[] = {
        { "/sdcard/XRay/tests/kueken7_rgb_etc2_unorm.ktx", "kueken7_rgb_etc2_unorm.ktx", &CanaryResult::etc2_rgb_ktx },
        { "/sdcard/XRay/tests/kueken7_rgb_etc2_srgb.ktx", "kueken7_rgb_etc2_srgb.ktx", &CanaryResult::etc2_srgb_ktx },
        { "/sdcard/XRay/tests/kueken7_rgba_astc4x4_srgb.ktx", "kueken7_rgba_astc4x4_srgb.ktx", &CanaryResult::astc4_srgb_ktx },
        { "/sdcard/XRay/tests/kueken7_rgb_etc2_unorm.dds", "kueken7_rgb_etc2_unorm.dds", &CanaryResult::etc2_rgb_dds },
        { "/sdcard/XRay/tests/kueken7_rgb_etc2_srgb.dds", "kueken7_rgb_etc2_srgb.dds", &CanaryResult::etc2_srgb_dds },
        { "/sdcard/XRay/tests/kueken7_rgba_astc4x4_srgb.dds", "kueken7_rgba_astc4x4_srgb.dds", &CanaryResult::astc4_srgb_dds },
        { "/sdcard/XRay/tests/kueken7_rgba_astc8x8_unorm.dds", "kueken7_rgba_astc8x8_unorm.dds", &CanaryResult::astc8_dds },
    };

    for (const auto& tf : test_files)
    {
        FILE* test_f = fopen(tf.path, "rb");
        if (!test_f)
        {
            Msg("CANARY_FILE name=%s status=MISSING", tf.name);
            if (file_ok)
                fprintf(f, "CANARY_FILE name=%s status=MISSING\n", tf.name);
            continue;
        }
        fclose(test_f);

        gli::texture tex = gli::load(tf.path);

        Msg("CANARY_FILE name=%s empty=%d target=%d format=%d compressed=%d block=%d extent=%dx%d levels=%u faces=%u layers=%u size=%zu",
            tf.name,
            static_cast<int>(tex.empty()),
            static_cast<int>(tex.target()),
            static_cast<int>(tex.format()),
            static_cast<int>(gli::is_compressed(tex.format())),
            static_cast<int>(gli::block_size(tex.format())),
            static_cast<int>(tex.extent(0).x),
            static_cast<int>(tex.extent(0).y),
            static_cast<unsigned int>(tex.levels()),
            static_cast<unsigned int>(tex.faces()),
            static_cast<unsigned int>(tex.layers()),
            tex.size());

        if (file_ok)
        {
            fprintf(f, "CANARY_FILE name=%s empty=%d target=%d format=%d compressed=%d block=%d extent=%dx%d levels=%u faces=%u layers=%u size=%zu\n",
                tf.name,
                static_cast<int>(tex.empty()),
                static_cast<int>(tex.target()),
                static_cast<int>(tex.format()),
                static_cast<int>(gli::is_compressed(tex.format())),
                static_cast<int>(gli::block_size(tex.format())),
                static_cast<int>(tex.extent(0).x),
                static_cast<int>(tex.extent(0).y),
                static_cast<unsigned int>(tex.levels()),
                static_cast<unsigned int>(tex.faces()),
                static_cast<unsigned int>(tex.layers()),
                tex.size());
        }

        if (tex.empty())
        {
            Msg("CANARY_FILE name=%s status=EMPTY", tf.name);
            if (file_ok)
                fprintf(f, "CANARY_FILE name=%s status=EMPTY\n", tf.name);
            continue;
        }

        if (tex.format() == gli::FORMAT_UNDEFINED)
        {
            Msg("CANARY_FILE name=%s status=INVALID_FORMAT reason=FORMAT_UNDEFINED format=%d compressed=%d extent=%dx%d levels=%u faces=%u layers=%u size=%zu",
                tf.name,
                static_cast<int>(tex.format()),
                static_cast<int>(gli::is_compressed(tex.format())),
                static_cast<int>(tex.extent(0).x),
                static_cast<int>(tex.extent(0).y),
                static_cast<unsigned int>(tex.levels()),
                static_cast<unsigned int>(tex.faces()),
                static_cast<unsigned int>(tex.layers()),
                tex.size());
            if (file_ok)
                fprintf(f, "CANARY_FILE name=%s status=INVALID_FORMAT reason=FORMAT_UNDEFINED format=%d compressed=%d extent=%dx%d levels=%u faces=%u layers=%u size=%zu\n",
                    tf.name,
                    static_cast<int>(tex.format()),
                    static_cast<int>(gli::is_compressed(tex.format())),
                    static_cast<int>(tex.extent(0).x),
                    static_cast<int>(tex.extent(0).y),
                    static_cast<unsigned int>(tex.levels()),
                    static_cast<unsigned int>(tex.faces()),
                    static_cast<unsigned int>(tex.layers()),
                    tex.size());
            continue;
        }

        if (!gli::is_compressed(tex.format()))
        {
            Msg("CANARY_FILE name=%s status=NOT_COMPRESSED format=%d compressed=%d extent=%dx%d levels=%u faces=%u layers=%u size=%zu",
                tf.name,
                static_cast<int>(tex.format()),
                static_cast<int>(gli::is_compressed(tex.format())),
                static_cast<int>(tex.extent(0).x),
                static_cast<int>(tex.extent(0).y),
                static_cast<unsigned int>(tex.levels()),
                static_cast<unsigned int>(tex.faces()),
                static_cast<unsigned int>(tex.layers()),
                tex.size());
            if (file_ok)
                fprintf(f, "CANARY_FILE name=%s status=NOT_COMPRESSED format=%d compressed=%d extent=%dx%d levels=%u faces=%u layers=%u size=%zu\n",
                    tf.name,
                    static_cast<int>(tex.format()),
                    static_cast<int>(gli::is_compressed(tex.format())),
                    static_cast<int>(tex.extent(0).x),
                    static_cast<int>(tex.extent(0).y),
                    static_cast<unsigned int>(tex.levels()),
                    static_cast<unsigned int>(tex.faces()),
                    static_cast<unsigned int>(tex.layers()),
                    tex.size());
            continue;
        }

        if (tex.target() != gli::TARGET_2D)
        {
            Msg("CANARY_FILE name=%s status=SKIP_NON_2D target=%d format=%d compressed=%d",
                tf.name,
                static_cast<int>(tex.target()),
                static_cast<int>(tex.format()),
                static_cast<int>(gli::is_compressed(tex.format())));
            if (file_ok)
                fprintf(f, "CANARY_FILE name=%s status=SKIP_NON_2D target=%d format=%d compressed=%d\n",
                    tf.name,
                    static_cast<int>(tex.target()),
                    static_cast<int>(tex.format()),
                    static_cast<int>(gli::is_compressed(tex.format())));
            continue;
        }

        if (tex.levels() == 0)
        {
            Msg("CANARY_FILE name=%s status=INVALID_LEVELS levels=%u",
                tf.name, static_cast<unsigned int>(tex.levels()));
            if (file_ok)
                fprintf(f, "CANARY_FILE name=%s status=INVALID_LEVELS levels=%u\n",
                    tf.name, static_cast<unsigned int>(tex.levels()));
            continue;
        }

        auto extent0 = tex.extent(0);
        if (extent0.x <= 0 || extent0.y <= 0)
        {
            Msg("CANARY_FILE name=%s status=INVALID_EXTENT extent=%dx%d",
                tf.name, static_cast<int>(extent0.x), static_cast<int>(extent0.y));
            if (file_ok)
                fprintf(f, "CANARY_FILE name=%s status=INVALID_EXTENT extent=%dx%d\n",
                    tf.name, static_cast<int>(extent0.x), static_cast<int>(extent0.y));
            continue;
        }

        size_t size0 = tex.size(0);
        if (size0 == 0)
        {
            Msg("CANARY_FILE name=%s status=INVALID_SIZE size0=%zu",
                tf.name, size0);
            if (file_ok)
                fprintf(f, "CANARY_FILE name=%s status=INVALID_SIZE size0=%zu\n",
                    tf.name, size0);
            continue;
        }

        const void* data0 = tex.data(0, 0, 0);
        if (data0 == nullptr)
        {
            Msg("CANARY_FILE name=%s status=NULL_DATA size0=%zu",
                tf.name, size0);
            if (file_ok)
                fprintf(f, "CANARY_FILE name=%s status=NULL_DATA size0=%zu\n",
                    tf.name, size0);
            continue;
        }

        gli::gl GL(gli::gl::PROFILE_ES30);
        gli::gl::format const fmt = GL.translate(tex.format(), tex.swizzles());

        Msg("CANARY_TRANSLATE name=%s format=%d internal=0x%X external=0x%X type=0x%X",
            tf.name,
            static_cast<int>(tex.format()),
            static_cast<unsigned int>(fmt.Internal),
            static_cast<unsigned int>(fmt.External),
            static_cast<unsigned int>(fmt.Type));

        if (file_ok)
        {
            fprintf(f, "CANARY_TRANSLATE name=%s format=%d internal=0x%X external=0x%X type=0x%X\n",
                tf.name,
                static_cast<int>(tex.format()),
                static_cast<unsigned int>(fmt.Internal),
                static_cast<unsigned int>(fmt.External),
                static_cast<unsigned int>(fmt.Type));
        }

        if (fmt.Internal == 0)
        {
            Msg("! CANARY_TRANSLATE name=%s status=INVALID_INTERNAL format=%d",
                tf.name, static_cast<int>(tex.format()));
            if (file_ok)
                fprintf(f, "CANARY_TRANSLATE name=%s status=INVALID_INTERNAL format=%d\n",
                    tf.name, static_cast<int>(tex.format()));
            continue;
        }

        GLuint texGL = 0;
        glGenTextures(1, &texGL);
        if (!texGL)
        {
            Msg("! CANARY_UPLOAD name=%s internal=0x%X gen=FAIL err=0x%X",
                tf.name, static_cast<unsigned int>(fmt.Internal), static_cast<unsigned int>(glGetError()));
            if (file_ok)
                fprintf(f, "CANARY_UPLOAD name=%s internal=0x%X gen=FAIL\n", tf.name, static_cast<unsigned int>(fmt.Internal));
            continue;
        }

        glBindTexture(GL_TEXTURE_2D, texGL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        DrGLErrorLocal();
        glCompressedTexImage2D(
            GL_TEXTURE_2D,
            0,
            fmt.Internal,
            static_cast<GLsizei>(extent0.x),
            static_cast<GLsizei>(extent0.y),
            0,
            static_cast<GLsizei>(size0),
            data0);
        GLenum err = glGetError();

        bool upload_ok = (err == GL_NO_ERROR);

        if (upload_ok)
        {
            Msg("CANARY_UPLOAD name=%s internal=0x%X w=%d h=%d size=%zu upload=OK err=0x0",
                tf.name, static_cast<unsigned int>(fmt.Internal),
                static_cast<int>(extent0.x), static_cast<int>(extent0.y), size0);
            if (file_ok)
                fprintf(f, "CANARY_UPLOAD name=%s internal=0x%X w=%d h=%d size=%zu upload=OK err=0x0\n",
                    tf.name, static_cast<unsigned int>(fmt.Internal),
                    static_cast<int>(extent0.x), static_cast<int>(extent0.y), size0);
        }
        else
        {
            Msg("! CANARY_UPLOAD name=%s internal=0x%X w=%d h=%d size=%zu upload=FAIL err=0x%X",
                tf.name, static_cast<unsigned int>(fmt.Internal),
                static_cast<int>(extent0.x), static_cast<int>(extent0.y), size0,
                static_cast<unsigned int>(err));
            if (file_ok)
                fprintf(f, "CANARY_UPLOAD name=%s internal=0x%X w=%d h=%d size=%zu upload=FAIL err=0x%X\n",
                    tf.name, static_cast<unsigned int>(fmt.Internal),
                    static_cast<int>(extent0.x), static_cast<int>(extent0.y), size0,
                    static_cast<unsigned int>(err));

            glDeleteTextures(1, &texGL);
            glBindTexture(GL_TEXTURE_2D, 0);
            continue;
        }

        static const char* vs_src =
            "#version 300 es\n"
            "layout(location = 0) in vec2 aPos;\n"
            "layout(location = 1) in vec2 aUV;\n"
            "out vec2 vUV;\n"
            "void main() {\n"
            "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
            "    vUV = aUV;\n"
            "}\n";

        static const char* fs_src =
            "#version 300 es\n"
            "precision mediump float;\n"
            "in vec2 vUV;\n"
            "uniform sampler2D u_tex;\n"
            "out vec4 fragColor;\n"
            "void main() {\n"
            "    fragColor = texture(u_tex, vec2(0.5, 0.5));\n"
            "}\n";

        GLuint vs = CompileShaderLocal(GL_VERTEX_SHADER, vs_src);
        GLuint fs = CompileShaderLocal(GL_FRAGMENT_SHADER, fs_src);
        GLuint program = LinkProgramLocal(vs, fs);

        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);

        if (!program)
        {
            Msg("! CANARY_DRAW name=%s shader=FAIL", tf.name);
            if (file_ok)
                fprintf(f, "CANARY_DRAW name=%s status=SHADER_FAIL\n", tf.name);

            glDeleteTextures(1, &texGL);
            glBindTexture(GL_TEXTURE_2D, 0);
            continue;
        }

        int fbo_w = static_cast<int>(extent0.x);
        int fbo_h = static_cast<int>(extent0.y);
        if (fbo_w > 64) fbo_w = 64;
        if (fbo_h > 64) fbo_h = 64;

        GLuint fbo = 0;
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);

        GLuint colorTex = 0;
        glGenTextures(1, &colorTex);
        glBindTexture(GL_TEXTURE_2D, colorTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, fbo_w, fbo_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);

        GLint fbStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (fbStatus != GL_FRAMEBUFFER_COMPLETE)
        {
            Msg("! CANARY_DRAW name=%s fbo=FAIL status=0x%X",
                tf.name, static_cast<unsigned int>(fbStatus));
            if (file_ok)
                fprintf(f, "CANARY_DRAW name=%s status=FBO_FAIL fbstatus=0x%X\n",
                    tf.name, static_cast<unsigned int>(fbStatus));

            glDeleteFramebuffers(1, &fbo);
            glDeleteTextures(1, &colorTex);
            glDeleteProgram(program);
            glDeleteTextures(1, &texGL);
            glBindTexture(GL_TEXTURE_2D, 0);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            continue;
        }

        glViewport(0, 0, fbo_w, fbo_h);

        glUseProgram(program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texGL);
        GLint uloc = glGetUniformLocation(program, "u_tex");
        if (uloc >= 0)
            glUniform1i(uloc, 0);

        static const GLfloat quad[] =
        {
            -1.0f, -1.0f, 0.0f, 0.0f,
             1.0f, -1.0f, 1.0f, 0.0f,
            -1.0f,  1.0f, 0.0f, 1.0f,
             1.0f,  1.0f, 1.0f, 1.0f
        };

        GLuint vao = 0, vbo = 0;
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), reinterpret_cast<const void*>(0));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), reinterpret_cast<const void*>(2 * sizeof(GLfloat)));

        DrGLErrorLocal();
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        GLenum drawErr = glGetError();

        bool draw_ok = (drawErr == GL_NO_ERROR);
        int pixel_r = 0, pixel_g = 0, pixel_b = 0, pixel_a = 0;
        GLenum readErr = GL_NO_ERROR;

        if (draw_ok)
        {
            unsigned char pixel[4] = { 0, 0, 0, 0 };
            glReadPixels(fbo_w / 2, fbo_h / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
            readErr = glGetError();
            pixel_r = pixel[0];
            pixel_g = pixel[1];
            pixel_b = pixel[2];
            pixel_a = pixel[3];
        }

        bool read_ok = draw_ok && (readErr == GL_NO_ERROR);

        if (draw_ok && read_ok)
        {
            Msg("CANARY_DRAW name=%s draw=OK read=OK pixel=rgba(%d,%d,%d,%d) err=0x%X",
                tf.name, pixel_r, pixel_g, pixel_b, pixel_a, static_cast<unsigned int>(readErr));
            if (file_ok)
                fprintf(f, "CANARY_DRAW name=%s draw=OK read=OK pixel=rgba(%d,%d,%d,%d) err=0x%X\n",
                    tf.name, pixel_r, pixel_g, pixel_b, pixel_a, static_cast<unsigned int>(readErr));
            results.*tf.result_field = 1;
        }
        else if (!draw_ok)
        {
            Msg("! CANARY_DRAW name=%s draw=FAIL err=0x%X",
                tf.name, static_cast<unsigned int>(drawErr));
            if (file_ok)
                fprintf(f, "CANARY_DRAW name=%s status=DRAW_FAIL err=0x%X\n",
                    tf.name, static_cast<unsigned int>(drawErr));
            results.*tf.result_field = 0;
        }
        else
        {
            Msg("! CANARY_DRAW name=%s read=FAIL err=0x%X",
                tf.name, static_cast<unsigned int>(readErr));
            if (file_ok)
                fprintf(f, "CANARY_DRAW name=%s status=READ_FAIL err=0x%X\n",
                    tf.name, static_cast<unsigned int>(readErr));
            results.*tf.result_field = 0;
        }

        glDeleteBuffers(1, &vbo);
        glDeleteVertexArrays(1, &vao);
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &colorTex);
        glDeleteProgram(program);
        glDeleteTextures(1, &texGL);

        glBindTexture(GL_TEXTURE_2D, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glUseProgram(0);
        glActiveTexture(GL_TEXTURE0);
    }

    bool any_valid = (results.etc2_rgb_ktx != -1 || results.etc2_srgb_ktx != -1 || results.astc4_srgb_ktx != -1 ||
                      results.etc2_rgb_dds != -1 || results.etc2_srgb_dds != -1 || results.astc4_srgb_dds != -1 || results.astc8_dds != -1);

    if (file_ok)
    {
        fprintf(f, "CANARY_SUMMARY: etc2_rgb_ktx=%d etc2_srgb_ktx=%d astc4_srgb_ktx=%d etc2_rgb_dds=%d etc2_srgb_dds=%d astc4_srgb_dds=%d astc8_dds=%d\n",
            results.etc2_rgb_ktx, results.etc2_srgb_ktx, results.astc4_srgb_ktx,
            results.etc2_rgb_dds, results.etc2_srgb_dds, results.astc4_srgb_dds, results.astc8_dds);
        fprintf(f, "GL-CANARY-COMPRESSED-FILE: done\n");
        fclose(f);
    }

    Msg("* CANARY_SUMMARY: etc2_rgb_ktx=%d etc2_srgb_ktx=%d astc4_srgb_ktx=%d etc2_rgb_dds=%d etc2_srgb_dds=%d astc4_srgb_dds=%d astc8_dds=%d",
        results.etc2_rgb_ktx, results.etc2_srgb_ktx, results.astc4_srgb_ktx,
        results.etc2_rgb_dds, results.etc2_srgb_dds, results.astc4_srgb_dds, results.astc8_dds);
    Msg("* GL-CANARY-COMPRESSED-FILE: done");

    if (!any_valid)
    {
        Msg("! CANARY_SUMMARY: no valid compressed file pipeline tested");
    }
}
} // namespace
#endif

void CHW::CreateDevice(SDL_Window* hWnd)
{
    ZoneScoped;

    m_window = hWnd;

    R_ASSERT(m_window);

    // Choose the closest pixel format
    SDL_DisplayMode mode;
    SDL_GetWindowDisplayMode(m_window, &mode);
    mode.format = SDL_PIXELFORMAT_RGBA8888;
    // Apply the pixel format to the device context
    SDL_SetWindowDisplayMode(m_window, &mode);

    Caps.fTarget = D3DFMT_A8R8G8B8;
    Caps.fDepth = D3DFMT_D24S8;

    // Create the context
#ifdef XR_PLATFORM_WEB
    EmscriptenWebGLContextAttributes attributes;
    emscripten_webgl_init_context_attributes(&attributes);
    attributes.majorVersion = 2;
    attributes.minorVersion = 0;
    attributes.alpha = false;
    attributes.depth = false;
    attributes.stencil = false;
    attributes.antialias = false;
    attributes.powerPreference = EM_WEBGL_POWER_PREFERENCE_HIGH_PERFORMANCE;

    m_webgl = emscripten_webgl_create_context("#canvas", &attributes);
    if (m_webgl <= 0)
    {
        Log("! WebGL2: could not create drawing context:", m_webgl);
        return;
    }

    if (MakeContextCurrent(IRender::PrimaryContext) != 0)
    {
        Log("! WebGL2: could not make context current");
        return;
    }

    int version;
    {
        ZoneScopedN("gladLoadGLES2");
        version = gladLoadGLES2(reinterpret_cast<GLADloadfunc>(emscripten_webgl_get_proc_address));
    }
    BaseVertexDrawSupported =
        emscripten_webgl_enable_extension(m_webgl, "WEBGL_draw_instanced_base_vertex_base_instance");
    Msg("* WebGL base vertex draws: %s", BaseVertexDrawSupported ? "extension" : "attribute re-pointing");
#else
    m_context = SDL_GL_CreateContext(m_window);
    if (m_context == nullptr)
    {
        Log("! OpenGL: could not create drawing context:", SDL_GetError());
        return;
    }

    if (MakeContextCurrent(IRender::PrimaryContext) != 0)
    {
        Log("! OpenGL: could not make context current:", SDL_GetError());
        return;
    }

    int version;
    {
#ifdef XR_PLATFORM_GLES
        ZoneScopedN("gladLoadGLES2");
        version = gladLoadGLES2(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress));
#else
        ZoneScopedN("gladLoadGL");
        version = gladLoadGL(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress));
#endif
    }
#endif
    if (version == 0)
    {
        Log("! OpenGL: could not initialize GLAD.");
        if (const auto err = SDL_GetError())
            Log("SDL Error:", err);
        return;
    }

    if (ThisInstanceIsGlobal())
    {
        UpdateVSync();

#ifdef DEBUG
        if (glDebugMessageCallback)
        {
            CHK_GL(glEnable(GL_DEBUG_OUTPUT));
            CHK_GL(glDebugMessageCallback((GLDEBUGPROC)OnDebugCallback, nullptr));
        }
#endif // DEBUG
    }

    int iMaxVTFUnits, iMaxCTIUnits;
    glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, &iMaxVTFUnits);
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &iMaxCTIUnits);

    AdapterName = reinterpret_cast<pcstr>(glGetString(GL_RENDERER));
    OpenGLVersionString = reinterpret_cast<pcstr>(glGetString(GL_VERSION));
    ShadingVersion = reinterpret_cast<pcstr>(glGetString(GL_SHADING_LANGUAGE_VERSION));

    Msg("* GPU vendor: [%s] device: [%s]", glGetString(GL_VENDOR), AdapterName);
    Msg("* GPU OpenGL version: %s", OpenGLVersionString);
    Msg("* GPU OpenGL shading language version: %s", ShadingVersion);
    Msg("* GPU OpenGL VTF units: [%d] CTI units: [%d]", iMaxVTFUnits, iMaxCTIUnits);

#if defined(__ANDROID__) && defined(XR_PLATFORM_GLES)
    AndroidRunCompressedTextureProbeOnce();
    AndroidRunCompressedFileCanaryOnce();
#endif

#ifdef XR_PLATFORM_WEB
    TextureUploadUnit = u32(iMaxCTIUnits) - 1;
    R_ASSERT2(TextureUploadUnit >= CTexture::rstVertex + CTexture::mtMaxVertexShaderTextures,
        "too few texture units for a dedicated upload unit");
#endif

    ComputeShadersSupported = false; // XXX: Implement compute shaders support

    if (glGenFramebuffers && glBindFramebuffer)
        UpdateViews();
}

void CHW::DestroyDevice()
{
    CHK_GL(glDeleteFramebuffers(1, &pFB));
    pFB = 0;

#ifdef XR_PLATFORM_WEB
    emscripten_webgl_destroy_context(m_webgl);
    m_webgl = 0;
    return;
#endif

    const auto context = SDL_GL_GetCurrentContext();
    if (context == m_context)
        SDL_GL_MakeCurrent(nullptr, nullptr);

    SDL_GL_DeleteContext(m_context);
    m_context = nullptr;
}

//////////////////////////////////////////////////////////////////////
// Resetting device
//////////////////////////////////////////////////////////////////////
void CHW::Reset()
{
    ZoneScoped;

    CHK_GL(glDeleteFramebuffers(1, &pFB));
    pFB = 0;
    UpdateViews();

    UpdateVSync();
}

void CHW::SetPrimaryAttributes(u32& windowFlags)
{
#ifdef XR_PLATFORM_WEB
    return; // the WebGL2 context is created on the canvas directly, not through SDL
#endif
    windowFlags |= SDL_WINDOW_OPENGL;

#ifdef XR_PLATFORM_GLES
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif

    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    if (!strstr(Core.Params, "-no_gl_context"))
    {
#ifdef XR_PLATFORM_GLES
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#endif
    }
}

IRender::RenderContext CHW::GetCurrentContext() const
{
#ifdef XR_PLATFORM_WEB
    if (emscripten_webgl_get_current_context() == m_webgl)
        return IRender::PrimaryContext;
    return IRender::NoContext;
#else
    const auto context = SDL_GL_GetCurrentContext();
    if (context == m_context)
        return IRender::PrimaryContext;
    return IRender::NoContext;
#endif
}

int CHW::MakeContextCurrent(IRender::RenderContext context) const
{
    switch (context)
    {
#ifdef XR_PLATFORM_WEB
    case IRender::NoContext:
        return emscripten_webgl_make_context_current(0);

    case IRender::PrimaryContext:
        return emscripten_webgl_make_context_current(m_webgl);
#else
    case IRender::NoContext:
        return SDL_GL_MakeCurrent(nullptr, nullptr);

    case IRender::PrimaryContext:
        return SDL_GL_MakeCurrent(m_window, m_context);
#endif

    default:
        NODEFAULT;
    }
    return -1;
}

void CHW::UpdateViews()
{
    // Create the default framebuffer
    glGenFramebuffers(1, &pFB);
    CHK_GL(glBindFramebuffer(GL_FRAMEBUFFER, pFB));

    BackBufferCount = 1;
}

void CHW::BeginScene() { }
void CHW::EndScene() { }

void CHW::Present()
{
    int drawableW = 0;
    int drawableH = 0;
    SDL_GL_GetDrawableSize(m_window, &drawableW, &drawableH);

    if (drawableW <= 0 || drawableH <= 0)
    {
        drawableW = static_cast<int>(Device.dwWidth);
        drawableH = static_cast<int>(Device.dwHeight);
    }

    GLint oldViewport[4] = { 0, 0, 0, 0 };
    glGetIntegerv(GL_VIEWPORT, oldViewport);
    const GLboolean oldScissor = glIsEnabled(GL_SCISSOR_TEST);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, pFB);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

    glViewport(0, 0, static_cast<GLsizei>(drawableW), static_cast<GLsizei>(drawableH));
    glDisable(GL_SCISSOR_TEST);

    glBlitFramebuffer(
        0, 0, static_cast<GLsizei>(Device.dwWidth), static_cast<GLsizei>(Device.dwHeight),
        0, 0, static_cast<GLsizei>(drawableW), static_cast<GLsizei>(drawableH),
        GL_COLOR_BUFFER_BIT, GL_NEAREST);

#ifndef XR_PLATFORM_WEB
    SDL_GL_SwapWindow(m_window);
#endif

    glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);

    if (oldScissor)
        glEnable(GL_SCISSOR_TEST);
    else
        glDisable(GL_SCISSOR_TEST);

    if (BackBufferCount == 0)
        BackBufferCount = 1;

    CurrentBackBuffer = (CurrentBackBuffer + 1) % BackBufferCount;

    static bool s_logged_present_once = false;
    if (!s_logged_present_once)
    {
        GLfloat clearColor[4] = {0};
        glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor);
        GLint viewport[4] = {0};
        glGetIntegerv(GL_VIEWPORT, viewport);
        int dw = 0, dh = 0;
        SDL_GL_GetDrawableSize(m_window, &dw, &dh);
        Msg("[VIDEODIAG] CHW::Present: clearColor=(%.3f,%.3f,%.3f,%.3f) viewport=(%d,%d,%d,%d) drawable=(%d,%d)",
            clearColor[0], clearColor[1], clearColor[2], clearColor[3],
            viewport[0], viewport[1], viewport[2], viewport[3], dw, dh);
        s_logged_present_once = true;
    }
}

DeviceState CHW::GetDeviceState() const
{
    //  TODO: OGL: Implement GetDeviceState
    return DeviceState::Normal;
}

std::pair<u32, u32> CHW::GetSurfaceSize()
{
    return
    {
        psDeviceMode.Width,
        psDeviceMode.Height
    };
}

bool CHW::ThisInstanceIsGlobal() const
{
    return this == &HW;
}

void CHW::BeginPixEvent(pcstr name) const
{
    if (glPushDebugGroup)
        glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, name);
}

void CHW::EndPixEvent() const
{
    if (glPushDebugGroup)
        glPopDebugGroup();
}
} // namespace xray::render::RENDER_NAMESPACE

#ifdef XR_PLATFORM_WEB
namespace xray::render::RENDER_NAMESPACE
{
texture_upload_unit::texture_upload_unit(GLenum target) : target(target)
{
    CHK_GL(glActiveTexture(GL_TEXTURE0 + HW.TextureUploadUnit));
}

texture_upload_unit::~texture_upload_unit()
{
    CHK_GL(glBindTexture(target, 0));
}
} // namespace xray::render::RENDER_NAMESPACE
#endif
