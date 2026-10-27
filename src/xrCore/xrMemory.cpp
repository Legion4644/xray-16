#include "stdafx.h"

#include <SDL.h>

#if defined(XR_PLATFORM_WINDOWS)
#include <Psapi.h>
#elif defined(XR_PLATFORM_LINUX) || defined(XR_PLATFORM_ANDROID)
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <malloc.h>
#elif defined(XR_PLATFORM_BSD)
#include <sys/time.h>
#include <sys/resource.h>
#include <malloc.h>
#elif defined(XR_PLATFORM_HAIKU)
#include <OS.h>
#include <sys/time.h>
#include <sys/resource.h>
#endif

// On other platforms these options are controlled by CMake
#if defined(XR_PLATFORM_WINDOWS)
#   ifdef _DEBUG
#       define USE_PURE_ALLOC
#   else
#       define USE_MIMALLOC
#   endif
#endif

#if defined(USE_MIMALLOC)
    #include "mimalloc.h"

    static_assert(xrMemory::SMALL_SIZE_MAX <= MI_SMALL_SIZE_MAX, "Please, adjust SMALL_SIZE_ALLOC_MAX");

    #define xr_internal_malloc(size) mi_malloc(size)
    #define xr_internal_malloc_aligned(size, alignment) mi_malloc_aligned(size, alignment)
    #define xr_internal_malloc_nothrow(size) mi_malloc(size)
    #define xr_internal_malloc_nothrow_aligned(size, alignment) mi_malloc_aligned(size, alignment)
    #define xr_internal_small_alloc(size) mi_malloc_small(size)
    #define xr_internal_small_free(ptr) mi_free(ptr)

    #define xr_internal_realloc(ptr, size) mi_realloc(ptr, size)
    #define xr_internal_realloc_aligned(ptr, size, alignment) mi_realloc_aligned(ptr, size, alignment)

    #define xr_internal_free(ptr) mi_free(ptr)
    #define xr_internal_free_size(ptr, size) mi_free_size(ptr, size)
    #define xr_internal_free_aligned(ptr, alignment) mi_free_aligned(ptr, alignment)
    #define xr_internal_free_size_aligned(ptr, size, alignment) mi_free_size_aligned(ptr, size, alignment)
#elif defined(USE_XR_ALIGNED_MALLOC)
    #include "Memory/xrMemory_align.h"

    #define xr_internal_malloc(size) malloc(size)
    #define xr_internal_malloc_aligned(size, alignment) xr_aligned_malloc(size, alignment)
    #define xr_internal_malloc_nothrow(size) xr_malloc(size)
    #define xr_internal_malloc_nothrow_aligned(size, alignment) mi_malloc_aligned(size, alignment)
    #define xr_internal_small_alloc(size) xr_aligned_malloc(size)
    #define xr_internal_small_free(ptr) xr_aligned_free(ptr)

    #define xr_internal_realloc(ptr, size) xr_aligned_realloc(ptr, size)
    #define xr_internal_realloc_aligned(ptr, size, alignment) xr_aligned_realloc(ptr, size, alignment)

    #define xr_internal_free(ptr) xr_aligned_free(ptr)
    #define xr_internal_free_size(ptr, size) xr_aligned_free(ptr)
    #define xr_internal_free_aligned(ptr, alignment) xr_aligned_free(ptr)
    #define xr_internal_free_size_aligned(ptr, size, alignment) xr_aligned_free(ptr)
#elif defined(USE_PURE_ALLOC)
    // Additional bytes of memory to hide memory problems on Release
    // But for Debug we don't need this if we want to find these problems
    #ifdef NDEBUG
        constexpr size_t xr_reserved_tail = 8;
    #else
        constexpr size_t xr_reserved_tail = 0;
    #endif

    #define xr_internal_malloc(size) malloc(size + xr_reserved_tail)
    #define xr_internal_malloc_aligned(size, alignment) malloc(size + xr_reserved_tail)
    #define xr_internal_malloc_nothrow(size) malloc(size + xr_reserved_tail)
    #define xr_internal_malloc_nothrow_aligned(size, alignment) malloc(size + xr_reserved_tail)
    #define xr_internal_small_alloc(size) malloc(size + xr_reserved_tail)
    #define xr_internal_small_free(ptr) free(ptr)

    #define xr_internal_realloc(ptr, size) realloc(ptr, size + xr_reserved_tail)
    #define xr_internal_realloc_aligned(ptr, size, alignment) realloc(ptr, size + xr_reserved_tail)

    #define xr_internal_free(ptr) free(ptr)
    #define xr_internal_free_size(ptr, size) free(ptr)
    #define xr_internal_free_aligned(ptr, alignment) free(ptr)
    #define xr_internal_free_size_aligned(ptr, size, alignment) free(ptr)
#else
    #error Please, define explicitly which allocator you want to use
#endif

namespace
{
    std::atomic<size_t> g_mem_counters[xrMemory::MEMCAT_COUNT]{};
    std::atomic<size_t> g_mem_boot_baseline[xrMemory::MEMCAT_COUNT]{};
    bool g_boot_snapshotted = false;
    std::string g_uninstr_list;
    std::atomic<long long> g_alloc_bytes{0};
    std::atomic<bool> g_ui_hook_fired{false};
}

xrMemory Memory;
// Also used in src\xrCore\xrDebug.cpp to prevent use of g_pStringContainer before it initialized
bool shared_str_initialized = false;

void xrMemory::_initialize()
{
    ZoneScoped;
    g_pStringContainer = xr_new<str_container>();
    shared_str_initialized = true;
    g_pSharedMemoryContainer = xr_new<smem_container>();
}

void xrMemory::_destroy()
{
    ZoneScoped;
    xr_delete(g_pSharedMemoryContainer);
    xr_delete(g_pStringContainer);
}

XRCORE_API void vminfo(size_t* _free, size_t* reserved, size_t* committed)
{
#if defined(XR_PLATFORM_WINDOWS)
    MEMORY_BASIC_INFORMATION memory_info;
    memory_info.BaseAddress = nullptr;
    *_free = *reserved = *committed = 0;
    while (VirtualQuery(memory_info.BaseAddress, &memory_info, sizeof(memory_info))) //-V575
    {
        switch (memory_info.State)
        {
        case MEM_FREE: *_free += memory_info.RegionSize; break;
        case MEM_RESERVE: *reserved += memory_info.RegionSize; break;
        case MEM_COMMIT: *committed += memory_info.RegionSize; break;
        }
        memory_info.BaseAddress = (char*)memory_info.BaseAddress + memory_info.RegionSize;
    }
#elif defined(XR_PLATFORM_LINUX)
    struct sysinfo si;
    sysinfo(&si);
    *_free = si.freeram * si.mem_unit;
    *reserved = si.bufferram * si.mem_unit;
    *committed = (si.totalram - si.freeram + si.totalswap - si.freeswap) * si.mem_unit;
#elif defined(XR_PLATFORM_HAIKU)
    *_free = *reserved = *committed = 0;
    system_info info;
    if (get_system_info(&info) == B_OK)
    {
        *_free = B_PAGE_SIZE * (uint64)(info.max_pages - info.used_pages);
        *reserved = B_PAGE_SIZE * (uint64)info.cached_pages;
        *committed = B_PAGE_SIZE * (uint64)info.used_pages;
    }
#endif
}

XRCORE_API void log_vminfo()
{
    size_t w_free, w_reserved, w_committed;
    vminfo(&w_free, &w_reserved, &w_committed);
    Msg("* [ %s ]: free[%zu K], reserved[%zu K], committed[%zu K]", SDL_GetPlatform(), w_free / 1024, w_reserved / 1024, w_committed / 1024);
}

XRCORE_API bool memmap_test_and_set_ui_hook_fired()
{
    return g_ui_hook_fired.exchange(true, std::memory_order_acq_rel);
}

XRCORE_API void log_memory_phase(pcstr phase)
{
    // Read VmRSS and VmSize from /proc/self/status
    size_t vm_rss = 0;
    size_t vm_size = 0;
    
    FILE* f = fopen("/proc/self/status", "r");
    if (f)
    {
        char line[256];
        while (fgets(line, sizeof(line), f))
        {
            if (strncmp(line, "VmRSS:", 6) == 0)
            {
                sscanf(line + 6, "%zu", &vm_rss);
            }
            else if (strncmp(line, "VmSize:", 7) == 0)
            {
                sscanf(line + 7, "%zu", &vm_size);
            }
        }
        fclose(f);
    }

    // Get free/committed memory
    size_t w_free, w_reserved, w_committed;
    vminfo(&w_free, &w_reserved, &w_committed);

    Msg("* [MEM:%s] VmRSS[%zu KB] VmSize[%zu KB] free[%zu KB] committed[%zu KB]", 
        phase, vm_rss, vm_size, w_free / 1024, w_committed / 1024);

    Memory.log_memmap(phase);
}

size_t xrMemory::mem_usage()
{
#if defined(XR_PLATFORM_WINDOWS)
    PROCESS_MEMORY_COUNTERS pmc = {};
    if (HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, GetCurrentProcessId()))
    {
        GetProcessMemoryInfo(h, &pmc, sizeof(pmc));
        CloseHandle(h);
    }
    return pmc.PagefileUsage;
#elif defined(XR_PLATFORM_LINUX) || defined(XR_PLATFORM_BSD) || defined(XR_PLATFORM_APPLE)
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return (size_t)ru.ru_maxrss;
#elif defined(XR_PLATFORM_HAIKU)
    system_info info;
    get_system_info(&info);
    return B_PAGE_SIZE * (uint64)info.used_pages;
#else
    return 0;
#endif
}

void xrMemory::mem_compact()
{
#if defined(XR_PLATFORM_WINDOWS)
    RegFlushKey(HKEY_CLASSES_ROOT);
    RegFlushKey(HKEY_CURRENT_USER);
#endif

    /*
    Следующая команда, в целом, не нужна.
    Современные аллокаторы достаточно грамотно и когда нужно возвращают память операционной системе.
    Эта строчка нужна, скорее всего, в определённых ситуациях, вроде использования файлов отображаемых в память,
    которые требуют большие свободные области памяти.
    */
    //HeapCompact(GetProcessHeap(), 0);
    if (g_pStringContainer)
        g_pStringContainer->clean();
    if (g_pSharedMemoryContainer)
        g_pSharedMemoryContainer->clean();

#if defined(XR_PLATFORM_WINDOWS)
    if (strstr(Core.Params, "-swap_on_compact"))
        SetProcessWorkingSetSize(GetCurrentProcess(), size_t(-1), size_t(-1));
#endif
}

void* xrMemory::mem_alloc(size_t size)
{
    const auto result = xr_internal_malloc(size);
    if (result)
        g_alloc_bytes.fetch_add(static_cast<long long>(size / 1024), std::memory_order_relaxed);
    return result;
}

void* xrMemory::mem_alloc(size_t size, size_t alignment)
{
    const auto result = xr_internal_malloc_aligned(size, alignment);
    if (result)
        g_alloc_bytes.fetch_add(static_cast<long long>(size / 1024), std::memory_order_relaxed);
    return result;
}

void* xrMemory::mem_alloc(size_t size, const std::nothrow_t&) noexcept
{
    const auto result = xr_internal_malloc_nothrow(size);
    if (result)
        g_alloc_bytes.fetch_add(static_cast<long long>(size / 1024), std::memory_order_relaxed);
    return result;
}

void* xrMemory::mem_alloc(size_t size, size_t alignment, const std::nothrow_t&) noexcept
{
    const auto result = xr_internal_malloc_nothrow_aligned(size, alignment);
    if (result)
        g_alloc_bytes.fetch_add(static_cast<long long>(size / 1024), std::memory_order_relaxed);
    return result;
}

void* xrMemory::small_alloc(size_t size) noexcept
{
    const auto result = xr_internal_small_alloc(size);
    if (result)
        g_alloc_bytes.fetch_add(static_cast<long long>(size / 1024), std::memory_order_relaxed);
    return result;
}

void xrMemory::small_free(void* ptr) noexcept
{
    if (ptr)
    {
#if defined(USE_MIMALLOC)
        size_t usable = mi_malloc_usable_size(ptr);
        g_alloc_bytes.fetch_sub(static_cast<long long>(usable / 1024), std::memory_order_relaxed);
#elif defined(XR_PLATFORM_LINUX) || defined(XR_PLATFORM_ANDROID) || defined(XR_PLATFORM_BSD)
        size_t usable = malloc_usable_size(ptr);
        g_alloc_bytes.fetch_sub(static_cast<long long>(usable / 1024), std::memory_order_relaxed);
#endif
    }
    xr_internal_small_free(ptr);
}

void* xrMemory::mem_realloc(void* ptr, size_t size)
{
    size_t old_size = 0;
    if (ptr)
    {
#if defined(USE_MIMALLOC)
        old_size = mi_malloc_usable_size(ptr);
#elif defined(XR_PLATFORM_LINUX) || defined(XR_PLATFORM_ANDROID) || defined(XR_PLATFORM_BSD)
        old_size = malloc_usable_size(ptr);
#endif
    }
    const auto result = xr_internal_realloc(ptr, size);
    if (result)
    {
        long long delta = static_cast<long long>(size / 1024) - static_cast<long long>(old_size / 1024);
        g_alloc_bytes.fetch_add(delta, std::memory_order_relaxed);
    }
    return result;
}

void* xrMemory::mem_realloc(void* ptr, size_t size, size_t alignment)
{
    size_t old_size = 0;
    if (ptr)
    {
#if defined(USE_MIMALLOC)
        old_size = mi_malloc_usable_size(ptr);
#elif defined(XR_PLATFORM_LINUX) || defined(XR_PLATFORM_ANDROID) || defined(XR_PLATFORM_BSD)
        old_size = malloc_usable_size(ptr);
#endif
    }
    const auto result = xr_internal_realloc_aligned(ptr, size, alignment);
    if (result)
    {
        long long delta = static_cast<long long>(size / 1024) - static_cast<long long>(old_size / 1024);
        g_alloc_bytes.fetch_add(delta, std::memory_order_relaxed);
    }
    return result;
}

void xrMemory::mem_free(void* ptr)
{
    if (ptr)
    {
#if defined(USE_MIMALLOC)
        size_t usable = mi_malloc_usable_size(ptr);
        g_alloc_bytes.fetch_sub(static_cast<long long>(usable / 1024), std::memory_order_relaxed);
#elif defined(XR_PLATFORM_LINUX) || defined(XR_PLATFORM_ANDROID) || defined(XR_PLATFORM_BSD)
        size_t usable = malloc_usable_size(ptr);
        g_alloc_bytes.fetch_sub(static_cast<long long>(usable / 1024), std::memory_order_relaxed);
#endif
    }
    xr_internal_free(ptr);
}

void xrMemory::mem_free(void* ptr, size_t alignment)
{
    if (ptr)
    {
#if defined(USE_MIMALLOC)
        size_t usable = mi_malloc_usable_size(ptr);
        g_alloc_bytes.fetch_sub(static_cast<long long>(usable / 1024), std::memory_order_relaxed);
#elif defined(XR_PLATFORM_LINUX) || defined(XR_PLATFORM_ANDROID) || defined(XR_PLATFORM_BSD)
        size_t usable = malloc_usable_size(ptr);
        g_alloc_bytes.fetch_sub(static_cast<long long>(usable / 1024), std::memory_order_relaxed);
#endif
    }
    xr_internal_free_aligned(ptr, alignment);
}

// xr_strdup
XRCORE_API pstr xr_strdup(pcstr string)
{
#ifdef USE_MIMALLOC
    return mi_strdup(string);
#else
    VERIFY(string);
    size_t len = xr_strlen(string) + 1;
    auto memory = static_cast<char*>(xr_malloc(len));
    CopyMemory(memory, string, len);
    return memory;
#endif
}

[[nodiscard]] void* operator new(size_t size)
{
    return Memory.mem_alloc(size);
}

[[nodiscard]] void* operator new[](size_t size)
{
    return Memory.mem_alloc(size);
}

[[nodiscard]] void* operator new(size_t size, const std::nothrow_t&) noexcept
{
    return Memory.mem_alloc(size);
}

[[nodiscard]] void* operator new[](size_t size, const std::nothrow_t&) noexcept
{
    return Memory.mem_alloc(size);
}

[[nodiscard]] void* operator new(size_t size, std::align_val_t alignment)
{
    return Memory.mem_alloc(size, static_cast<size_t>(alignment));
}

[[nodiscard]] void* operator new[](size_t size, std::align_val_t alignment)
{
    return Memory.mem_alloc(size, static_cast<size_t>(alignment));
}

[[nodiscard]] void* operator new(size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    return Memory.mem_alloc(size, static_cast<size_t>(alignment));
}

[[nodiscard]] void* operator new[](size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    return Memory.mem_alloc(size, static_cast<size_t>(alignment));
}

void operator delete(void* ptr) noexcept
{
    Memory.mem_free(ptr);
}

void operator delete[](void* ptr) noexcept
{
    Memory.mem_free(ptr);
}

void operator delete(void* ptr, std::align_val_t alignment) noexcept
{
    Memory.mem_free(ptr, static_cast<size_t>(alignment));
}

void operator delete[](void* ptr, std::align_val_t alignment) noexcept
{
    Memory.mem_free(ptr, static_cast<size_t>(alignment));
}

void operator delete(void* ptr, size_t) noexcept
{
    Memory.mem_free(ptr);
}

void operator delete[](void* ptr, size_t) noexcept
{
    Memory.mem_free(ptr);
}

void operator delete(void* ptr, size_t, std::align_val_t alignment) noexcept
{
    Memory.mem_free(ptr, static_cast<size_t>(alignment));
}

void operator delete[](void* ptr, size_t, std::align_val_t alignment) noexcept
{
    Memory.mem_free(ptr, static_cast<size_t>(alignment));
}

XRCORE_API void* xr_malloc(size_t size)
{
    return Memory.mem_alloc(size);
}

XRCORE_API void* xr_realloc(void* ptr, size_t size)
{
    return Memory.mem_realloc(ptr, size);
}

void xrMemory::track_mem(MemCategory cat, size_t delta_kb)
{
    if (cat < MEMCAT_COUNT)
    {
        g_mem_counters[cat].fetch_add(delta_kb, std::memory_order_relaxed);
    }
}

size_t xrMemory::get_mem(MemCategory cat) const
{
    if (cat < MEMCAT_COUNT)
    {
        if (cat == MEMCAT_HEAP_LIVE)
        {
            long long val = g_alloc_bytes.load(std::memory_order_relaxed);
            return (val > 0) ? static_cast<size_t>(val) : 0;
        }
        return g_mem_counters[cat].load(std::memory_order_relaxed);
    }
    return 0;
}

size_t xrMemory::get_mem_delta(MemCategory cat) const
{
    if (cat < MEMCAT_COUNT)
    {
        size_t current = get_mem(cat);
        size_t baseline = g_mem_boot_baseline[cat].load(std::memory_order_relaxed);
        return (current > baseline) ? (current - baseline) : 0;
    }
    return 0;
}

void xrMemory::snapshot_boot_baseline()
{
    if (!g_boot_snapshotted)
    {
        for (u32 i = 0; i < MEMCAT_COUNT; ++i)
        {
            if (i == MEMCAT_HEAP_LIVE)
            {
                long long val = g_alloc_bytes.load(std::memory_order_relaxed);
                g_mem_boot_baseline[i].store((val > 0) ? static_cast<size_t>(val) : 0, std::memory_order_relaxed);
            }
            else
            {
                g_mem_boot_baseline[i].store(g_mem_counters[i].load(std::memory_order_relaxed), std::memory_order_relaxed);
            }
        }
        g_boot_snapshotted = true;
    }
}

const std::string& xrMemory::get_uninstr_list() const
{
    return g_uninstr_list;
}

void xrMemory::add_uninstr(pcstr name)
{
    if (!g_uninstr_list.empty())
        g_uninstr_list += ",";
    g_uninstr_list += name;
}

void xrMemory::log_memmap(pcstr phase)
{
#ifdef __ANDROID__
    static bool s_uninstr_init = false;
    if (!s_uninstr_init && xr_strcmp(phase, "boot") == 0)
    {
        add_uninstr("lua_internal_heap");
        add_uninstr("physics_ODE_internals");
        add_uninstr("video_memory_non_tex");
        add_uninstr("render_thread_stacks");
        add_uninstr("system_malloc_overhead");
        add_uninstr("ui_texture_pool");
        add_uninstr("ui_fonts");
        s_uninstr_init = true;
    }

    size_t vm_rss = 0;
    FILE* f = fopen("/proc/self/status", "r");
    if (f)
    {
        char line[256];
        while (fgets(line, sizeof(line), f))
        {
            if (strncmp(line, "VmRSS:", 6) == 0)
            {
                sscanf(line + 6, "%zu", &vm_rss);
                break;
            }
        }
        fclose(f);
    }

    // Primary categories for residual (mutually exclusive, live/resident)
    size_t heap_live_abs = get_mem(MEMCAT_HEAP_LIVE);
    size_t heap_live_delta = get_mem_delta(MEMCAT_HEAP_LIVE);
    size_t gpu_tex_live_abs = get_mem(MEMCAT_GPU_TEX_LIVE);
    size_t gpu_tex_live_delta = get_mem_delta(MEMCAT_GPU_TEX_LIVE);
    size_t gpu_rt_live_abs = get_mem(MEMCAT_GPU_RT_LIVE);
    size_t gpu_rt_live_delta = get_mem_delta(MEMCAT_GPU_RT_LIVE);

    // Informational categories (excluded from residual)
    size_t level_info_abs = get_mem(MEMCAT_LEVEL_INFO);
    size_t level_info_delta = get_mem_delta(MEMCAT_LEVEL_INFO);
    size_t shaders_info_abs = get_mem(MEMCAT_SHADERS_INFO);
    size_t shaders_info_delta = get_mem_delta(MEMCAT_SHADERS_INFO);
    size_t ui_info_abs = get_mem(MEMCAT_UI_INFO);
    size_t ui_info_delta = get_mem_delta(MEMCAT_UI_INFO);
    size_t script_info_abs = get_mem(MEMCAT_SCRIPT_INFO);
    size_t script_info_delta = get_mem_delta(MEMCAT_SCRIPT_INFO);
    size_t sound_info_abs = get_mem(MEMCAT_SOUND_INFO);
    size_t sound_info_delta = get_mem_delta(MEMCAT_SOUND_INFO);
    size_t phys_info_abs = get_mem(MEMCAT_PHYS_INFO);
    size_t phys_info_delta = get_mem_delta(MEMCAT_PHYS_INFO);

    // Residual formula: only primary exclusive live categories
    size_t sum_primary = heap_live_abs + gpu_tex_live_abs + gpu_rt_live_abs;
    size_t residual = (vm_rss > sum_primary) ? (vm_rss - sum_primary) : 0;

    // Main log line
    Msg("* [MEMMAP:%s] RSS=%zu heap_live=%zu/+%zu gpu_tex_live=%zu/+%zu gpu_rt_live=%zu/+%zu residual=%zu "
        "level_info=%zu/+%zu shaders_info=%zu/+%zu ui_info=%zu/+%zu script_info=%zu/+%zu sound_info=%zu/+%zu phys_info=%zu/+%zu "
        "uninstr=%s",
        phase, vm_rss,
        heap_live_abs, heap_live_delta,
        gpu_tex_live_abs, gpu_tex_live_delta,
        gpu_rt_live_abs, gpu_rt_live_delta,
        residual,
        level_info_abs, level_info_delta,
        shaders_info_abs, shaders_info_delta,
        ui_info_abs, ui_info_delta,
        script_info_abs, script_info_delta,
        sound_info_abs, sound_info_delta,
        phys_info_abs, phys_info_delta,
        g_uninstr_list.empty() ? "none" : g_uninstr_list.c_str());

    // Audit line - explains the formula and exclusions
    if (xr_strcmp(phase, "boot") == 0 || xr_strcmp(phase, "t_report") == 0)
    {
        int heap_live_ok = (heap_live_abs > 0) ? 1 : 0;
        int gpu_tex_live_ok = (gpu_tex_live_abs > 0) ? 1 : 0;
        int gpu_rt_live_ok = (gpu_rt_live_abs > 0) ? 1 : 0;
        Msg("* [MEMMAP-AUDIT] formula=heap_live+gpu_tex_live+gpu_rt_live; "
            "heap_live=%d gpu_tex_live=%d gpu_rt_live=%d; "
            "excluded_from_residual=level,shaders,ui,script,sound,phys; "
            "overlap_known=level_heap,shaders_heap,ui_texgpu+heap,script_heap,sound_heap,phys_heap; "
            "proxies_excluded=phys_hardcoded,script_static",
            heap_live_ok, gpu_tex_live_ok, gpu_rt_live_ok);
    }

    // Sanity check at boot
    if (xr_strcmp(phase, "boot") == 0)
    {
        int same_order = 0;
        if (heap_live_abs > 0 && vm_rss > 0)
        {
            double ratio = static_cast<double>(heap_live_abs) / static_cast<double>(vm_rss);
            same_order = (ratio >= 0.1 && ratio <= 10.0) ? 1 : 0;
        }
        int ui_instrumented = (ui_info_abs > 0) ? 1 : 0;
        int ui_hook_fired = g_ui_hook_fired.load(std::memory_order_relaxed) ? 1 : 0;
        int hom_instrumented = (level_info_abs > 0) ? 1 : 0;
        Msg("* [MEMMAP-CHECK] rss_boot_kb=%zu heap_live_kb=%zu same_order=%d ui_instrumented=%d ui_hook_fired=%d hom_instrumented=%d",
            vm_rss, heap_live_abs, same_order, ui_instrumented, ui_hook_fired, hom_instrumented);
    }
#else
    (void)phase;
#endif
}
