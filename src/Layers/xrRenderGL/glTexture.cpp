// Texture.cpp: implementation of the CTexture class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "xrCore/xrMemory.h"

#include <array>
#include <string>
#include <cstring>

#include <malloc.h>
#if defined(USE_MIMALLOC)
#include <mimalloc.h>
#endif

#include <gli/gli.hpp>

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
    IReader* S = FS.r_open(fn);
    R_ASSERT2_CURE(S, fn, { return 0; });
    size_t img_size = S->length();
#ifdef DEBUG
    Msg("* Loaded: %s[%d]b", fn, img_size);
#endif // DEBUG
    gli::texture texture = gli::load((char*)S->pointer(), img_size);
    R_ASSERT2(!texture.empty(), fn);

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
            
            // Android: safe cutoff for large textures - only load mips that reduce to <= 1024
            #ifdef __ANDROID__
            auto ext2 = src.extent();
            int lod = 0;
            while (lod < static_cast<int>(src.levels()) - 1 && (ext2.x > 1024 || ext2.y > 1024))
            {
                ++lod;
                ext2 = src.extent(lod);
            }
            if (lod > 0)
            {
                Msg("* MEM: mip-cut active=%d tex=%s %dx%d->lod%d", 1, fn, src.extent().x, src.extent().y, lod);
                // Create a new texture2d with only the target mip level
                gli::texture2d src_lod(src.format(), ext2, 1);
                std::memcpy(src_lod.data(), src.data(0, 0, lod), src.size(lod));
                gli::texture2d dst = gli::convert(src_lod, gli::FORMAT_RGBA8_UNORM_PACK8);
                if (!dst.empty())
                    converted = dst;
            }
            else
            #endif
            {
                gli::texture2d dst = gli::convert(src, gli::FORMAT_RGBA8_UNORM_PACK8);
                if (!dst.empty())
                    converted = dst;
            }
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
            auto ext2 = src.extent();
            int lod = 0;
            while (lod < static_cast<int>(src.levels()) - 1 && (ext2.x > 1024 || ext2.y > 1024))
            {
                ++lod;
                ext2 = src.extent(lod);
            }
            if (lod > 0)
            {
                Msg("* MEM: mip-cut active=%d tex=%s %dx%d->lod%d (uncompressed)", 1, fn, src.extent().x, src.extent().y, lod);
                // Create a new texture2d with only the target mip level
                gli::texture2d src_lod(src.format(), ext2, 1);
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
