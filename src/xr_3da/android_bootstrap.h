#pragma once

// Android-specific bootstrap for the standalone engine host.
//
// This is only compiled on Android (see CMakeLists.txt) and is responsible for
// everything that must happen before the engine creates its own filesystem and
// log: resolving the user's data directory, dropping a default fsgame.ltx,
// redirecting the process stdout/stderr to a file and installing native crash
// handlers that dump a readable log we can inspect directly on the device.

#if defined(XR_PLATFORM_ANDROID)

#include <string>

namespace xray_android
{
// Resolves the external data root (e.g. /sdcard/XRay), creates the directory
// layout, writes a default fsgame.ltx if missing, redirects stdout/stderr to a
// log file and installs signal handlers. Safe to call once, early in main().
void EarlyInit();

// Directory where logs are written. Empty until EarlyInit() ran.
const std::string& GetLogDir();

} // namespace xray_android

#endif // XR_PLATFORM_ANDROID
