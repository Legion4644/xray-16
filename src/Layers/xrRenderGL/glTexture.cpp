// Texture.cpp: implementation of the CTexture class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include <cstdlib>
#include <cstdio>
#include "xrCore/xrMemory.h"

#include <array>
#include <string>
#include <cstring>

#include <malloc.h>
#if defined(USE_MIMALLOC)
#include <mimalloc.h>
#endif

#include <gli/gli.hpp>
#if defined(__ANDROID__) && defined(XR_PLATFORM_GLES)
#include <gli/gl.hpp>
#endif


namespace xray::render::RENDER_NAMESPACE
{
void fix_texture_name(pstr fn)
{
    pstr _ext = strext(fn);
    if (_ext &&
        (0 == xr_stricmp(_ext, ".tga") ||
            0 == xr_stricmp(_ext, ".dds") ||
            0 == xr_stricmp(_ext, ".bmp") ||
            0 == xr_stricmp(_ext, ".ogm")))
        *_ext = 0;
}

int get_texture_load_lod(LPCSTR fn)
{
    CInifile::Sect& sect = pSettings->r_section("reduce_lod_texture_list");

    for (const auto& item : sect.Data)
    {
        if (strstr(fn, item.first.c_str()))
        {
            if (psTextureLOD < 1)
                return 0;
            if (psTextureLOD < 3)
                return 1;
            return 2;
        }
    }

    if (psTextureLOD < 2)
        return 0;
    if (psTextureLOD < 4)
        return 1;
    return 2;
}

#if defined(__ANDROID__)
static int find_mip_for_max_dim(const gli::texture2d& src, int tex_max)
{
    auto ext = src.extent();
    int lod = 0;
    while (lod < static_cast<int>(src.levels()) - 1 && (ext.x > tex_max || ext.y > tex_max))
    {
        ++lod;
        ext = src.extent(lod);
    }
    return lod;
}
#endif

u32 calc_texture_size(int lod, u32 mip_cnt, size_t orig_size)
{
    if (1 == mip_cnt)
        return orig_size;

    int _lod = lod;
    float res = float(orig_size);

    while (_lod > 0)
    {
        --_lod;
        res -= res / 1.333f;
    }
    return iFloor(res);
}


#if defined(__ANDROID__) && defined(XR_PLATFORM_GLES)
// MIP-CUT ASTC direct
static GLuint RGL_TextureLoadCompressedDirectAndroid(
    pcstr fn,
    const gli::texture& texture,
    u32& ret_msize,
    GLenum& ret_desc,
    u32& ret_width,
    u32& ret_height)
{
    static unsigned long s_log_count = 0;
    static unsigned long s_total_textures = 0;
    static unsigned long s_total_bytes = 0;

    if (texture.empty())
        return 0;

    if (texture.target() != gli::TARGET_2D)
        return 0;

    if (!gli::is_compressed(texture.format()))
        return 0;

    // Whitlist empírica do canary u0_a564 e do loader real u0_a565/u0_a566:
    // 157 = ASTC 4x4 UNORM_BLOCK
    // 158 = ASTC 4x4 SRGB_BLOCK
    const int fmt_id = static_cast<int>(texture.format());
    if (fmt_id != 157 && fmt_id != 158)
        return 0;

    if (texture.levels() == 0)
        return 0;

    const auto base_ext = texture.extent(0);
    if (base_ext.x == 0 || base_ext.y == 0)
        return 0;

    const int max_dim = static_cast<int>(psTextureMax);

    // Escolhe o primeiro level que cabe em psTextureMax.
    // Se max_dim <= 0, não corta.
    size_t start_level = 0;
    while (start_level + 1 < texture.levels())
    {
        const auto ext = texture.extent(start_level);
        if (max_dim <= 0 ||
            (static_cast<int>(ext.x) <= max_dim && static_cast<int>(ext.y) <= max_dim))
        {
            break;
        }
        ++start_level;
    }

    const auto first_ext = texture.extent(start_level);
    int cut_w = static_cast<int>(first_ext.x);
    int cut_h = static_cast<int>(first_ext.y);

    if (cut_w <= 0 || cut_h <= 0)
        return 0;

    // Se mesmo o último level não couber, fallback seguro para o caminho antigo.
    if (max_dim > 0 && (cut_w > max_dim || cut_h > max_dim))
    {
        start_level = texture.levels() - 1;
        const auto last_ext = texture.extent(start_level);
        cut_w = static_cast<int>(last_ext.x);
        cut_h = static_cast<int>(last_ext.y);

        if (cut_w <= 0 || cut_h <= 0 || cut_w > max_dim || cut_h > max_dim)
            return 0;
    }

    const size_t out_levels = texture.levels() - start_level;
    if (out_levels == 0)
        return 0;

    gli::gl GL(gli::gl::PROFILE_ES30);
    const auto fmt = GL.translate(texture.format(), texture.swizzles());

    if (fmt.Internal == 0)
        return 0;

    // Limpa erros GL antigos antes de medir o nosso upload.
    while (glGetError() != GL_NO_ERROR)
    {
    }

    struct RGL_GLESStateGuard
    {
        GLint active;
        GLint bind2D;

        RGL_GLESStateGuard() : active(GL_TEXTURE0), bind2D(0)
        {
            glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
            glActiveTexture(GL_TEXTURE0);
#ifdef GL_TEXTURE_BINDING_2D
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &bind2D);
#endif
        }

        ~RGL_GLESStateGuard()
        {
#ifdef GL_TEXTURE_BINDING_2D
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(bind2D));
#endif
            glActiveTexture(static_cast<GLenum>(active));
        }
    };

    RGL_GLESStateGuard guard;

    GLuint tex = 0;
    glGenTextures(1, &tex);
    if (!tex)
        return 0;

    glBindTexture(GL_TEXTURE_2D, tex);

    const GLenum min_filter = (out_levels > 1)
        ? GL_LINEAR_MIPMAP_LINEAR
        : GL_LINEAR;

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL,
                    static_cast<GLint>(out_levels - 1));

    size_t loaded_size = 0;

    for (size_t dest_level = 0; dest_level < out_levels; ++dest_level)
    {
        const size_t src_level = start_level + dest_level;
        const auto ext = texture.extent(src_level);
        const size_t level_size = texture.size(src_level);
        const void* level_data = texture.data(0, 0, src_level);

        if (ext.x == 0 || ext.y == 0 || level_size == 0 || level_data == nullptr)
        {
            glDeleteTextures(1, &tex);
            return 0;
        }

        // Caminho provado pelo canary e pelo loader real:
        // glCompressedTexImage2D, 8 argumentos, sem xoffset/yoffset.
        glCompressedTexImage2D(
            GL_TEXTURE_2D,
            static_cast<GLint>(dest_level),
            fmt.Internal,
            static_cast<GLsizei>(ext.x),
            static_cast<GLsizei>(ext.y),
            0,
            static_cast<GLsizei>(level_size),
            level_data);

        const GLenum err = glGetError();
        if (err != GL_NO_ERROR)
        {
            Msg("! TEXTURE-COMPRESSED-DIRECT MIP-CUT name=%s upload=FAIL internal=0x%X dest_level=%d src_level=%d err=0x%X",
                fn,
                static_cast<unsigned int>(fmt.Internal),
                static_cast<int>(dest_level),
                static_cast<int>(src_level),
                static_cast<unsigned int>(err));

            glDeleteTextures(1, &tex);
            return 0;
        }

        loaded_size += level_size;
    }

    glBindTexture(GL_TEXTURE_2D, 0);

    ret_msize = static_cast<u32>(loaded_size);
    ret_desc = static_cast<GLenum>(fmt.Internal);
    ret_width = static_cast<u32>(cut_w);
    ret_height = static_cast<u32>(cut_h);

    ++s_total_textures;
    s_total_bytes += static_cast<unsigned long>(loaded_size);

    if (s_log_count < 50)
    {
        ++s_log_count;
        Msg("* TEXTURE-COMPRESSED-DIRECT MIP-CUT [%lu/50] name=%s target=%d format=%d internal=0x%X base=%dx%d cut=%dx%d levels_total=%u out_levels=%u start_level=%u size_kb=%lu total_textures=%lu total_mb=%lu",
            s_log_count,
            fn,
            static_cast<int>(texture.target()),
            fmt_id,
            static_cast<unsigned int>(fmt.Internal),
            static_cast<unsigned int>(base_ext.x),
            static_cast<unsigned int>(base_ext.y),
            static_cast<unsigned int>(cut_w),
            static_cast<unsigned int>(cut_h),
            static_cast<unsigned int>(texture.levels()),
            static_cast<unsigned int>(out_levels),
            static_cast<unsigned int>(start_level),
            static_cast<unsigned long>(loaded_size / 1024),
            s_total_textures,
            static_cast<unsigned long>(s_total_bytes / (1024UL * 1024UL)));
    }

    return tex;
}
#endif


#if defined(__ANDROID__) && defined(XR_PLATFORM_GLES)
// ASTC-CACHE-LOOKUP
static bool RGL_IsSep(char c)
{
    return c == '/' || c == 92; // 92 == backslash
}

static bool RGL_AstcCacheEnabled()
{
    const char* v = std::getenv("XRAY_ASTC_CACHE");
    return !(v && v[0] == '0');
}

static const char* RGL_FindGamedataRel(pcstr path)
{
    const char* search = path;

    while (search && *search)
    {
        const char* p = std::strstr(search, "gamedata");
        if (!p)
            break;

        const bool boundary = (p == path) || RGL_IsSep(*(p - 1));

        if (boundary)
        {
            p += 8; // strlen("gamedata")
            if (RGL_IsSep(*p))
                ++p;

            if (*p)
                return p;
        }

        search = p;
        if (*search)
            ++search;
    }

    return nullptr;
}

static bool RGL_BuildAstcCachePath(pcstr fn, string_path out, size_t out_size)
{
    if (!RGL_AstcCacheEnabled())
        return false;

    const char* rel = RGL_FindGamedataRel(fn);
    if (!rel || !rel[0])
        return false;

    const char* root = std::getenv("XRAY_ASTC_CACHE_ROOT");
    if (!root || !root[0])
        root = "/storage/emulated/0/XRay/cache/astc";

    const size_t root_len = std::strlen(root);
    const size_t rel_len = std::strlen(rel);

    const char* dds_ext = ".dds";
    const size_t dds_len = 4;

    const char* astc_ext = ".astc.dds";
    const size_t astc_len = 9;

    const bool has_dds =
        (rel_len >= dds_len && std::strcmp(rel + rel_len - dds_len, dds_ext) == 0);

    const size_t base_rel_len = has_dds ? (rel_len - dds_len) : rel_len;

    const bool root_has_slash = (root_len > 0 && root[root_len - 1] == '/');
    const size_t sep_len = root_has_slash ? 0 : 1;

    const size_t need = root_len + sep_len + base_rel_len + astc_len + 1;
    if (need > out_size)
        return false;

    std::memcpy(out, root, root_len);

    char* dst = out + root_len;
    if (sep_len)
    {
        *dst = '/';
        ++dst;
    }

    for (size_t i = 0; i < base_rel_len; ++i)
    {
        char c = rel[i];
        dst[i] = RGL_IsSep(c) ? '/' : c;
    }

    dst += base_rel_len;
    std::memcpy(dst, astc_ext, astc_len + 1);

    out[need - 1] = 0;
    return true;
}

static bool RGL_TryLoadAstcCacheAndroid(
    pcstr original_fn,
    pcstr cache_fn,
    u32& ret_msize,
    GLenum& ret_desc,
    u32& ret_width,
    u32& ret_height,
    GLuint& out_tex)
{
    out_tex = 0;

    std::FILE* f = std::fopen(cache_fn, "rb");
    if (!f)
        return false;

    if (std::fseek(f, 0, SEEK_END) != 0)
    {
        std::fclose(f);
        return false;
    }

    const long sz_long = std::ftell(f);
    if (sz_long <= 0)
    {
        std::fclose(f);
        return false;
    }

    if (std::fseek(f, 0, SEEK_SET) != 0)
    {
        std::fclose(f);
        return false;
    }

    const size_t sz = static_cast<size_t>(sz_long);
    xr_vector<u8> buf;
    buf.resize(sz);

    if (std::fread(buf.data(), 1, sz, f) != sz)
    {
        std::fclose(f);
        return false;
    }

    std::fclose(f);

    gli::texture texture = gli::load(reinterpret_cast<char*>(buf.data()), sz);
    if (texture.empty())
    {
        Msg("! ASTC-CACHE-FALLBACK original=%s cache=%s reason=empty",
            original_fn,
            cache_fn);
        return false;
    }

    GLuint tex = RGL_TextureLoadCompressedDirectAndroid(
        cache_fn,
        texture,
        ret_msize,
        ret_desc,
        ret_width,
        ret_height);

    if (tex == 0)
    {
        Msg("! ASTC-CACHE-FALLBACK original=%s cache=%s reason=direct_failed",
            original_fn,
            cache_fn);
        return false;
    }

    Msg("* ASTC-CACHE-HIT original=%s cache=%s size_kb=%lu",
        original_fn,
        cache_fn,
        static_cast<unsigned long>(sz / 1024));

    out_tex = tex;
    return true;
}
#endif


GLuint CRender::texture_load(pcstr fRName, u32& ret_msize, GLenum& ret_desc, u32& ret_width, u32& ret_height)
{
    ret_msize = 0;
    R_ASSERT1_CURE(fRName && fRName[0], { return 0; });

    static int s_texture_load_count = 0;
    ++s_texture_load_count;
    if (s_texture_load_count % 50 == 0)
    {
        if (s_texture_load_count == 50)
            log_memory_phase("texture_50");
        else if (s_texture_load_count == 100)
            log_memory_phase("texture_100");
        else if (s_texture_load_count == 200)
            log_memory_phase("texture_200");
    }

    GLuint pTexture = 0;
    string_path fn;
    {
        // make file name
        string_path fname;
        xr_strcpy(fname, fRName);
        fix_texture_name(fname);

        // Call to FS.exist WRITES to fn !

        if (!FS.exist(fn, "$game_textures$", fname, ".dds") && strstr(fname, "_bump"))
        {
            Msg("! Fallback to default bump map: %s", fname);
            if (strstr(fname, "_bump#"))
                R_ASSERT1_CURE(FS.exist(fn, "$game_textures$", "ed\\ed_dummy_bump#", ".dds"), return 0);
            else
                R_ASSERT1_CURE(FS.exist(fn, "$game_textures$", "ed\\ed_dummy_bump", ".dds"), return 0);
        }
        else
        {
            bool exist = false;

            for (cpcstr folder : { "$level$", "$game_saves$", "$game_textures$" })
            {
                exist = FS.exist(fn, folder, fname, ".dds");
                if (exist)
                    break;
            }

            if (!exist)
            {
                Msg("! Can't find texture '%s'", fname);
                if (!FS.exist(fn, "$game_textures$", "ed\\ed_not_existing_texture", ".dds"))
                    return 0;
            }
        }
    }

    // Load and get header
#if defined(__ANDROID__) && defined(XR_PLATFORM_GLES)
    {
        string_path cache_fn;

        if (RGL_BuildAstcCachePath(fn, cache_fn, sizeof(cache_fn)))
        {
            static bool s_cache_attempt_logged = false;
            if (!s_cache_attempt_logged)
            {
                Msg("* ASTC-CACHE-ATTEMPT original=%s cache=%s", fn, cache_fn);
                s_cache_attempt_logged = true;
            }

            GLuint cache_tex = 0;
            u32 cache_msize = 0;
            GLenum cache_desc = 0;
            u32 cache_width = 0;
            u32 cache_height = 0;

            if (RGL_TryLoadAstcCacheAndroid(
                    fn,
                    cache_fn,
                    cache_msize,
                    cache_desc,
                    cache_width,
                    cache_height,
                    cache_tex))
            {
                ret_msize = cache_msize;
                ret_desc = cache_desc;
                ret_width = cache_width;
                ret_height = cache_height;
                return cache_tex;
            }
        }
    }
#endif

    IReader* S = FS.r_open(fn);
    R_ASSERT2_CURE(S, fn, { return 0; });
    size_t img_size = S->length();
#ifdef DEBUG
    Msg("* Loaded: %s[%d]b", fn, img_size);
#endif // DEBUG
    gli::texture texture = gli::load((char*)S->pointer(), img_size);
    R_ASSERT2(!texture.empty(), fn);

#if defined(__ANDROID__) && defined(XR_PLATFORM_GLES)
    {
        GLuint direct_tex = RGL_TextureLoadCompressedDirectAndroid(
            fn,
            texture,
            ret_msize,
            ret_desc,
            ret_width,
            ret_height);

        if (direct_tex != 0)
            return direct_tex;
    }
#endif

    // Track if we did DXT->RGBA8 conversion to subtract host buffer later
    bool dxt_converted = false;
    size_t dxt_expanded_size = 0;

#if defined(XR_PLATFORM_GLES)
    if (gli::is_compressed(texture.format()))
    {
        gli::texture converted;
        if (texture.target() == gli::TARGET_2D)
        {
            gli::texture2d src(texture);
            
            // Android: mip-cut before convert based on tex_max (dimension cap)
            #ifdef __ANDROID__
            static bool s_tex_max_logged = false;
            if (!s_tex_max_logged)
            {
                Msg("* MEM: tex_max=%d", psTextureMax);
                s_tex_max_logged = true;
            }
            static bool s_mip_cut_before_convert_logged = false;
            if (!s_mip_cut_before_convert_logged)
            {
                Msg("* MEM: mip-cut-before-convert active=1");
                s_mip_cut_before_convert_logged = true;
            }
            
            int lod = find_mip_for_max_dim(src, psTextureMax);
            if (lod > 0)
            {
                auto ext_before = src.extent();
                auto ext_after = src.extent(lod);
                Msg("* MEM: mip-cut tex=%s %dx%d -> %dx%d (tex_max=%d)", fn, ext_before.x, ext_before.y, ext_after.x, ext_after.y, psTextureMax);
                
                // Create a new texture2d with only the target mip level
                gli::texture2d src_lod(src.format(), ext_after, 1);
                std::memcpy(src_lod.data(), src.data(0, 0, lod), src.size(lod));
                gli::texture2d dst = gli::convert(src_lod, gli::FORMAT_RGBA8_UNORM_PACK8);
                if (!dst.empty())
                    converted = dst;
            }
            else
            {
                gli::texture2d dst = gli::convert(src, gli::FORMAT_RGBA8_UNORM_PACK8);
                if (!dst.empty())
                    converted = dst;
            }
            #else
            {
                gli::texture2d dst = gli::convert(src, gli::FORMAT_RGBA8_UNORM_PACK8);
                if (!dst.empty())
                    converted = dst;
            }
            #endif
        }
        if (!converted.empty())
        {
            dxt_converted = true;
            dxt_expanded_size = converted.size();
            
            // Track top 10 DXT->RGBA8 allocations
            static std::array<std::pair<std::string, size_t>, 10> s_top_dxt_allocations{};
            size_t alloc_size = dxt_expanded_size;
            std::string name(fn);
            for (auto& entry : s_top_dxt_allocations)
            {
                if (alloc_size > entry.second)
                {
                    entry = {name, alloc_size};
                    break;
                }
            }
            
            static bool s_logged_dxt_fallback = false;
            if (!s_logged_dxt_fallback)
            {
                Msg("! GLES: converting compressed texture to RGBA8: %s", fn);
                s_logged_dxt_fallback = true;
            }
            
            // Log top 10 periodically
            static int s_dxt_log_counter = 0;
            if (++s_dxt_log_counter % 50 == 0)
            {
                Msg("* [GLES-TOP10-DXT] Top 10 DXT->RGBA8 allocations:");
                int rank = 1;
                for (const auto& entry : s_top_dxt_allocations)
                {
                    if (entry.second > 0)
                        Msg("  %d. %s - %zu KB", rank++, entry.first.c_str(), entry.second / 1024);
                }
            }
            
            // Track DXT->RGBA8 expanded buffer in host_nonalloc (RISK A)
            // This is the actual RGBA8 buffer allocated by gli::convert, NOT just swapRedBlue
#ifdef __ANDROID__
            if (dxt_expanded_size > 0)
                ::Memory.track_mem(xrMemory::MEMCAT_HOST_NONALLOC, dxt_expanded_size / 1024);
            
            // One-shot log for first >1MB DXT->RGBA8 expanded buffer
            static bool s_host_nonalloc_hook_logged = false;
            if (!s_host_nonalloc_hook_logged && dxt_expanded_size > 1024 * 1024)
            {
                Msg("* MEM: host_nonalloc-hook fired tex=%s size_kb=%zu", fn, dxt_expanded_size / 1024);
                s_host_nonalloc_hook_logged = true;
            }
#endif
            
            texture = std::move(converted);
        }
        else
        {
            Msg("! GLES: compressed texture convert failed, using 1x1 RGBA8: %s", fn);
            texture = gli::texture(gli::TARGET_2D, gli::FORMAT_RGBA8_UNORM_PACK8, gli::extent3d(1, 1, 1), 1, 1, 1);
            const glm::u8vec4 pixel(255, 0, 255, 255);
            memcpy(texture.data(), &pixel, sizeof(pixel));
        }
    }
    else
    {
        // Android: apply mip-cut to large uncompressed textures as well (e.g. terrain_zaton.dds 2048x2048 RGBA8)
        #ifdef __ANDROID__
        if (texture.target() == gli::TARGET_2D)
        {
            gli::texture2d src(texture);
            int lod = find_mip_for_max_dim(src, psTextureMax);
            if (lod > 0)
            {
                auto ext_before = src.extent();
                auto ext_after = src.extent(lod);
                Msg("* MEM: mip-cut tex=%s %dx%d -> %dx%d (tex_max=%d)", fn, ext_before.x, ext_before.y, ext_after.x, ext_after.y, psTextureMax);
                // Create a new texture2d with only the target mip level
                gli::texture2d src_lod(src.format(), ext_after, 1);
                std::memcpy(src_lod.data(), src.data(0, 0, lod), src.size(lod));
                texture = std::move(src_lod);
            }
        }
        #endif
    }
#endif

    u32 mip_cnt = u32(-1); // XXX: write to it when reading with GLI!

#if defined(XR_PLATFORM_WEB) || defined(XR_PLATFORM_GLES)
    gli::gl GL(gli::gl::PROFILE_ES30);
#else
    gli::gl GL(gli::gl::PROFILE_GL33);
#endif

    gli::gl::format const format = GL.translate(texture.format(), texture.swizzles());
    GLenum target = GL.translate(texture.target());


    const texture_upload_unit uploadUnit(target);
    glGenTextures(1, &pTexture);
    glBindTexture(target, pTexture);

    glTexParameteri(target, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(texture.levels() - 1));

    xr_vector<u8> reordered;
#if defined(XR_PLATFORM_WEB) || defined(XR_PLATFORM_GLES)
    const bool swapRedBlue = !gli::is_compressed(texture.format()) && gli::block_size(texture.format()) == 4
        && format.Swizzles[gli::SWIZZLE_RED] == GL_BLUE;
    const auto texels = [&](size_t layer, size_t face, size_t level) -> const void*
    {
        const void* data = texture.data(layer, face, level);
        if (!swapRedBlue)
            return data;
        const size_t size = texture.size(level);
        reordered.assign(static_cast<const u8*>(data), static_cast<const u8*>(data) + size);
        for (size_t i = 0; i + 3 < size; i += 4)
            std::swap(reordered[i], reordered[i + 2]);
        return reordered.data();
    };
#else
    if (gli::gl::EXTERNAL_RED != format.External) // skip for proper greyscale-alpha font textures
        glTexParameteriv(target, GL_TEXTURE_SWIZZLE_RGBA, &format.Swizzles[gli::SWIZZLE_RED]);
    const auto texels = [&](size_t layer, size_t face, size_t level) { return texture.data(layer, face, level); };
#endif

    glm::tvec3<GLsizei> const tex_extent(texture.extent());

    GLenum err;
    switch (texture.target())
    {
    case gli::TARGET_2D:
    case gli::TARGET_CUBE:
#if defined(XR_PLATFORM_GLES)
        // clear stale GL errors from earlier paths (e.g. Theora video) so the check below reflects only this glTexStorage2D
        while (glGetError() != GL_NO_ERROR)
        {
        }
#endif
        glTexStorage2D(target, static_cast<GLint>(texture.levels()), format.Internal,
                       tex_extent.x, tex_extent.y);
        err = glGetError();
        if (err != GL_NO_ERROR)
        {
            VERIFY(err == GL_NO_ERROR);
            Msg("! OpenGL: 0x%x: Invalid 2D texture: '%s' target=0x%x internal=0x%x levels=%d w=%d h=%d",
                (unsigned int)err, fn, (unsigned int)target, (unsigned int)format.Internal,
                static_cast<int>(texture.levels()), static_cast<int>(tex_extent.x), static_cast<int>(tex_extent.y));
        }
        break;
    case gli::TARGET_3D:
    case gli::TARGET_CUBE_ARRAY:
        glTexStorage3D(target, static_cast<GLint>(texture.levels()), format.Internal,
                       tex_extent.x, tex_extent.y, tex_extent.z);
        err = glGetError();
        if (err != GL_NO_ERROR)
        {
            VERIFY(err == GL_NO_ERROR);
            Msg("! OpenGL: 0x%x: Invalid 3D texture: '%s'", err, fn);
        }
        break;
    default:
        NODEFAULT;
        break;
    }

    for (size_t layer = 0; layer < texture.layers(); ++layer)
    {
        for (size_t face = 0; face < texture.faces(); ++face)
        {
            for (size_t level = 0; level < texture.levels(); ++level)
            {
                glm::tvec3<GLsizei> const tex_level_extent(texture.extent(level));
                GLenum sub_target = gli::is_target_cube(texture.target())
                         ? static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face)
                         : target;

                switch (texture.target())
                {
                case gli::TARGET_2D:
                case gli::TARGET_CUBE:
                {
                    if (gli::is_compressed(texture.format()))
                    {
                        glCompressedTexSubImage2D(sub_target, static_cast<GLint>(level),
                                    0, 0, tex_level_extent.x, tex_level_extent.y,
                                    format.Internal, static_cast<GLsizei>(texture.size(level)),
                                    texture.data(layer, face, level));
                        err = glGetError();
                        if (err != GL_NO_ERROR)
                        {
                            VERIFY(err == GL_NO_ERROR);
                            Msg("! OpenGL: 0x%x: Invalid 2D compressed subtexture: '%s'", err, fn);
                        }
                    }
                    else
                    {
                        glTexSubImage2D(sub_target, static_cast<GLint>(level),
                                    0, 0, tex_level_extent.x, tex_level_extent.y,
                                    format.External, format.Type,
                                    texels(layer, face, level));
                        err = glGetError();
                        if (err != GL_NO_ERROR)
                        {
                            VERIFY(err == GL_NO_ERROR);
                            Msg("! OpenGL: 0x%x: Invalid 2D subtexture: '%s'", err, fn);
                        }

                    }
                    break;
                }
                case gli::TARGET_3D:
                case gli::TARGET_CUBE_ARRAY:
                {
                    if (gli::is_compressed(texture.format()))
                    {
                        glCompressedTexSubImage3D(target, static_cast<GLint>(level),
                                    0, 0, 0, tex_level_extent.x, tex_level_extent.y, tex_level_extent.z,
                                    format.Internal, static_cast<GLsizei>(texture.size(level)),
                                    texture.data(layer, face, level));
                        err = glGetError();
                        if (err != GL_NO_ERROR)
                        {
                            VERIFY(err == GL_NO_ERROR);
                            Msg("! OpenGL: 0x%x: Invalid compressed 3D subtexture: '%s'", err, fn);
                        }
                    }
                    else
                    {
                        glTexSubImage3D(target, static_cast<GLint>(level),
                                    0, 0, 0, tex_level_extent.x, tex_level_extent.y, tex_level_extent.z,
                                    format.External, format.Type,
                                    texels(layer, face, level));
                        err = glGetError();
                        if (err != GL_NO_ERROR)
                        {
                            VERIFY(err == GL_NO_ERROR);
                            Msg("! OpenGL: 0x%x: Invalid 3D subtexture: '%s'", err, fn);
                        }
                    }
                    break;
                }
                default:
                    NODEFAULT;
                    break;
                }
            }
        }
    }

    // Calculate total GPU texture size (all mip levels)
    size_t gpu_tex_size = 0;
    for (size_t level = 0; level < texture.levels(); ++level)
        gpu_tex_size += texture.size(level);

    // Track GPU texture memory (real upload path)
#ifdef __ANDROID__
    if (gpu_tex_size > 0)
        ::Memory.track_mem(xrMemory::MEMCAT_GPU_TEX_LIVE, gpu_tex_size / 1024);
#endif

    // Track host_nonalloc for reordered buffer (swapRedBlue path)
#ifdef __ANDROID__
    size_t reordered_size = reordered.capacity();
    if (reordered_size > 0)
        ::Memory.track_mem(xrMemory::MEMCAT_HOST_NONALLOC, reordered_size / 1024);
#endif

    // One-shot log for first texture >1MB to prove hook fires on device
#ifdef __ANDROID__
    static bool s_gpu_tex_hook_logged = false;
    if (!s_gpu_tex_hook_logged && gpu_tex_size > 1024 * 1024)
    {
        Msg("* MEM: gpu_tex-hook fired load tex=%s size_kb=%zu", fn, gpu_tex_size / 1024);
        s_gpu_tex_hook_logged = true;
    }
#endif

    // One-shot log for gpu_tex-path=dxt-rgba8 (RISK B - confirm mutual exclusivity)
#ifdef __ANDROID__
    static bool s_gpu_tex_path_dxt_logged = false;
    if (!s_gpu_tex_path_dxt_logged && gpu_tex_size > 1024 * 1024)
    {
        Msg("* MEM: gpu_tex-path=dxt-rgba8 tex=%s size_kb=%zu", fn, gpu_tex_size / 1024);
        s_gpu_tex_path_dxt_logged = true;
    }
#endif

    // Free host pixel buffer immediately after upload to avoid CPU memory bloat
    size_t host_buf_size = texture.size();
    auto texture_target = texture.target(); // save before clear
    
    // Clear reordered buffer early if it was used (swapRedBlue path)
    reordered.clear();
    reordered.shrink_to_fit();
    
    // Subtract host_nonalloc for reordered buffer after free
#ifdef __ANDROID__
    if (reordered_size > 0)
        ::Memory.track_mem(xrMemory::MEMCAT_HOST_NONALLOC, -(long long)(reordered_size / 1024));
#endif
    
    // Subtract host_nonalloc for DXT->RGBA8 expanded buffer after free (RISK A)
#ifdef __ANDROID__
    if (dxt_converted && dxt_expanded_size > 0)
        ::Memory.track_mem(xrMemory::MEMCAT_HOST_NONALLOC, -(long long)(dxt_expanded_size / 1024));
#endif
    
    texture.clear();

// Log host buffer size at texture #50 and #100 to verify release
    static int s_host_log_counter = 0;
    if (++s_host_log_counter == 50 || s_host_log_counter == 100)
    {
        Msg("* MEM: host buffer freed size=%zu KB (texture #%d)", host_buf_size / 1024, s_host_log_counter);
    }

    FS.r_close(S);

    xr_strlwr(fn);
    ret_desc = target;
    ret_width = tex_extent.x;
    ret_height = tex_extent.y;
    // Return actual GPU texture size in KB for accurate memory tracking
    ret_msize = static_cast<u32>(gpu_tex_size / 1024);

    return pTexture;
}
} // namespace xray::render::RENDER_NAMESPACE
