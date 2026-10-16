#include "android_bootstrap.h"

#if defined(XR_PLATFORM_ANDROID)

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <android/log.h>
#include <dlfcn.h>
#include <unwind.h>
#include <jni.h>

#define XRAY_ANDROID_LOG_TAG "xray-cop"
#define XRAY_ANDROID_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, XRAY_ANDROID_LOG_TAG, __VA_ARGS__)
#define XRAY_ANDROID_LOGI(...) __android_log_print(ANDROID_LOG_INFO, XRAY_ANDROID_LOG_TAG, __VA_ARGS__)

namespace
{
std::string g_rootDir;
std::string g_logDir;

const char* const kFsgameContent =
    ";abbreviation           = recurs|notif|  root|                  add|        ext|            description\n"
    "$app_data_root$         = true|  false| $fs_root$|\n"
    "$arch_dir$              = false| false| $fs_root$\n"
    "$game_arch_mp$          = false| false| $fs_root$|            mp\n"
    "$arch_dir_levels$       = false| false| $fs_root$|            levels\n"
    "$arch_dir_resources$    = false| false| $fs_root$|            resources\n"
    "$arch_dir_localization$ = false| false| $fs_root$|            localization\n"
    "$arch_dir_patches$      = false| true|  $fs_root$|            patches\n"
    "$game_data$             = false| true|  $fs_root$|            gamedata\n"
    "$game_ai$               = true|  false| $game_data$|          ai\n"
    "$game_spawn$            = true|  false| $game_data$|          spawns\n"
    "$game_levels$           = true|  false| $game_data$|          levels\n"
    "$game_meshes$           = true|  true|  $game_data$|          meshes|       *.ogf;*.omf|   Game Object files\n"
    "$game_anims$            = true|  true|  $game_data$|          anims|        *.anm;*.anms|  Animation files\n"
    "$game_dm$               = true|  true|  $game_data$|          meshes|       *.dm|          Detail Model files\n"
    "$game_shaders$          = true|  true|  $game_data$|          shaders\n"
    "$game_sounds$           = true|  true|  $game_data$|          sounds\n"
    "$game_textures$         = true|  true|  $game_data$|          textures\n"
    "$game_config$           = true|  false| $game_data$|          configs\n"
    "$game_weathers$         = true|  false| $game_config$|        environment\\weathers\n"
    "$game_weather_effects$  = true|  false| $game_config$|        environment\\weather_effects\n"
    "$textures$              = true|  true|  $game_data$|          textures\n"
    "$level$                 = false| false| $game_levels$\n"
    "$game_scripts$          = true|  false| $game_data$|          scripts|      *.script|      Game script files\n"
    "$logs$                  = true|  false| $app_data_root$|      logs\n"
    "$screenshots$           = true|  false| $app_data_root$|      screenshots\n"
    "$game_saves$            = true|  false| $app_data_root$|      savedgames\n"
    "$downloads$             = false| false| $app_data_root$\n";

bool ensureDir(const std::string& path)
{
    if (path.empty())
        return false;

    struct stat st{};
    if (stat(path.c_str(), &st) == 0)
        return S_ISDIR(st.st_mode);

    std::string current = path[0] == '/' ? "/" : "";
    size_t pos = path[0] == '/' ? 1 : 0;
    while (pos <= path.size())
    {
        const size_t next = path.find('/', pos);
        const size_t end = (next == std::string::npos) ? path.size() : next;
        if (end > pos)
        {
            if (!current.empty() && current.back() != '/')
                current.push_back('/');
            current.append(path, pos, end - pos);
            if (mkdir(current.c_str(), 0775) != 0 && errno != EEXIST)
                return false;
        }
        if (next == std::string::npos)
            break;
        pos = next + 1;
    }

    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool dirWritable(const std::string& path)
{
    const std::string probe = path + "/.xray_write_test";
    const int fd = open(probe.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0664);
    if (fd < 0)
        return false;
    close(fd);
    unlink(probe.c_str());
    return true;
}

std::string resolveRoot()
{
    const char* const candidates[] = { "/sdcard/XRay", "/storage/emulated/0/XRay" };
    for (const char* candidate : candidates)
    {
        if (!ensureDir(candidate))
            continue;
        if (dirWritable(candidate))
            return candidate;
    }
    return {};
}

void writeDefaultFsgame(const std::string& root)
{
    const std::string fsgame = root + "/fsgame.ltx";

    struct stat st{};
    if (stat(fsgame.c_str(), &st) == 0)
        return;

    const int fd = open(fsgame.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0664);
    if (fd < 0)
    {
        XRAY_ANDROID_LOGE("failed to write fsgame.ltx (%s): %s", fsgame.c_str(), strerror(errno));
        return;
    }

    const size_t len = strlen(kFsgameContent);
    ssize_t written = 0;
    while (written < static_cast<ssize_t>(len))
    {
        const ssize_t result = write(fd, kFsgameContent + written, len - written);
        if (result <= 0)
            break;
        written += result;
    }
    close(fd);
    XRAY_ANDROID_LOGI("wrote default fsgame.ltx at %s", fsgame.c_str());
}

void redirectStdio(const std::string& logDir)
{
    const std::string earlyLog = logDir + "/engine_early.log";
    const int fd = open(earlyLog.c_str(), O_CREAT | O_WRONLY | O_APPEND, 0664);
    if (fd < 0)
    {
        XRAY_ANDROID_LOGE("failed to open early log (%s): %s", earlyLog.c_str(), strerror(errno));
        return;
    }

    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    if (fd > STDERR_FILENO)
        close(fd);

    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
}

const char* signalName(int sig)
{
    switch (sig)
    {
    case SIGSEGV: return "SIGSEGV (segmentation fault)";
    case SIGBUS:  return "SIGBUS (bus error)";
    case SIGABRT: return "SIGABRT (abort)";
    case SIGILL:  return "SIGILL (illegal instruction)";
    case SIGFPE:  return "SIGFPE (floating point error)";
    case SIGTRAP: return "SIGTRAP (trap)";
    case SIGSYS:  return "SIGSYS (bad system call)";
    default:      return "unknown signal";
    }
}

struct BacktraceState {
    void** current;
    void** end;
};

static _Unwind_Reason_Code UnwindCallback(struct _Unwind_Context* context, void* arg)
{
    BacktraceState* state = static_cast<BacktraceState*>(arg);
    uintptr_t pc = _Unwind_GetIP(context);
    if (pc)
    {
        if (state->current == state->end) return _URC_END_OF_STACK;
        *state->current++ = reinterpret_cast<void*>(pc);
    }
    return _URC_NO_REASON;
}

void writeCrashLog(int sig, siginfo_t* info)
{
    if (g_logDir.empty())
        return;

    const std::string crashLog = g_logDir + "/native_crash.log";
    const int fd = open(crashLog.c_str(), O_CREAT | O_WRONLY | O_APPEND, 0664);
    if (fd < 0)
        return;

    char buffer[256];
    const char* const header = "\n===== NATIVE CRASH =====\n";
    write(fd, header, strlen(header));

    int n = snprintf(buffer, sizeof(buffer), "Signal   : %s (%d)\n", signalName(sig), sig);
    write(fd, buffer, n);

    n = snprintf(buffer, sizeof(buffer), "Fault    : %p\n", info ? info->si_addr : nullptr);
    write(fd, buffer, n);

    const char* const traceHeader = "Backtrace:\n";
    write(fd, traceHeader, strlen(traceHeader));

    void* frames[64];
    BacktraceState state = {frames, frames + 64};
    _Unwind_Backtrace(UnwindCallback, &state);
    const int count = static_cast<int>(state.current - frames);

    for (int i = 0; i < count; ++i)
    {
        Dl_info dlInfo;
        if (dladdr(frames[i], &dlInfo) && dlInfo.dli_sname)
        {
            n = snprintf(buffer, sizeof(buffer), "  %p %s\n", frames[i], dlInfo.dli_sname);
        }
        else
        {
            n = snprintf(buffer, sizeof(buffer), "  %p\n", frames[i]);
        }
        write(fd, buffer, n);
    }

    const char* const footer = "===== END NATIVE CRASH =====\n";
    write(fd, footer, strlen(footer));
    close(fd);

    XRAY_ANDROID_LOGE("native crash: %s", signalName(sig));
}

void crashHandler(int sig, siginfo_t* info, void* /*ucontext*/)
{
    writeCrashLog(sig, info);

    signal(sig, SIG_DFL);
    raise(sig);
}

void installCrashHandlers()
{
    const int signals[] = { SIGSEGV, SIGBUS, SIGABRT, SIGILL, SIGFPE, SIGTRAP, SIGSYS };

    struct sigaction action{};
    action.sa_sigaction = crashHandler;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);

    for (const int sig : signals)
        sigaction(sig, &action, nullptr);
}
} // namespace

namespace xray_android
{
void EarlyInit()
{
    g_rootDir = resolveRoot();
    if (g_rootDir.empty())
    {
        const char* internal = getenv("XRAY_ANDROID_INTERNAL_DIR");
        g_rootDir = internal ? internal : "/data/local/tmp/XRay";
        ensureDir(g_rootDir);
        XRAY_ANDROID_LOGE("external storage not writable, using fallback %s", g_rootDir.c_str());
    }

    g_logDir = g_rootDir + "/logs";
    ensureDir(g_logDir);
    ensureDir(g_rootDir + "/savedgames");
    ensureDir(g_rootDir + "/screenshots");

    installCrashHandlers();

    redirectStdio(g_logDir);

    if (chdir(g_rootDir.c_str()) != 0)
        XRAY_ANDROID_LOGE("chdir(%s) failed: %s", g_rootDir.c_str(), strerror(errno));

    writeDefaultFsgame(g_rootDir);

    XRAY_ANDROID_LOGI("root=%s logs=%s", g_rootDir.c_str(), g_logDir.c_str());
    printf("[xray-android] root=%s\n", g_rootDir.c_str());
    printf("[xray-android] logs=%s\n", g_logDir.c_str());
    fflush(stdout);
}

const std::string& GetLogDir()
{
    return g_logDir;
}
} // namespace xray_android

extern "C" JNIEXPORT jstring JNICALL
Java_com_xray_cop_XRayActivity_nativeGetLogDir(JNIEnv* env, jclass /*clazz*/)
{
    return env->NewStringUTF(xray_android::GetLogDir().c_str());
}

#endif // XR_PLATFORM_ANDROID
