include_guard()

include(FetchContent)

set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
if (POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

set(_XRAY_ANDROID_SAVED_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
set(_XRAY_ANDROID_SAVED_UNITY_BUILD ${CMAKE_UNITY_BUILD})
set(BUILD_SHARED_LIBS OFF)
set(BUILD_TESTING OFF)
set(CMAKE_UNITY_BUILD OFF)

# ---------------------------------------------------------------------------
# SDL2
# ---------------------------------------------------------------------------
# SDL2 must be a shared library: SDLActivity.java loads "SDL2" at startup and
# its native JNI entry points (nativeRunMain) live there.
set(SDL_SHARED ON)
set(SDL_STATIC OFF)
set(SDL_TEST OFF)
set(SDL_TESTS OFF)
set(SDL2_DISABLE_INSTALL ON)
set(SDL2_DISABLE_SDL2MAIN ON)

FetchContent_Declare(SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-2.30.11
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(SDL2)

if (NOT TARGET SDL2::SDL2)
    if (TARGET SDL2-static)
        add_library(SDL2::SDL2 ALIAS SDL2-static)
    elseif (TARGET SDL2::SDL2-static)
        add_library(SDL2::SDL2 ALIAS SDL2-static)
    elseif (TARGET SDL2)
        add_library(SDL2::SDL2 ALIAS SDL2)
    endif()
endif()

# ---------------------------------------------------------------------------
# OpenAL Soft
# ---------------------------------------------------------------------------
set(ALSOFT_UTILS OFF)
set(ALSOFT_EXAMPLES OFF)
set(ALSOFT_TESTS OFF)
set(ALSOFT_INSTALL OFF)
set(ALSOFT_INSTALL_CONFIG OFF)
set(ALSOFT_INSTALL_HRTF_DATA OFF)
set(ALSOFT_INSTALL_AMBDEC_PRESETS OFF)
set(ALSOFT_NO_CONFIG_UTIL ON)
set(LIBTYPE STATIC)

FetchContent_Declare(OpenALSoft
    GIT_REPOSITORY https://github.com/kcat/openal-soft.git
    GIT_TAG 1.23.1
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(OpenALSoft)

if (NOT TARGET OpenAL::OpenAL)
    if (TARGET OpenAL)
        add_library(OpenAL::OpenAL ALIAS OpenAL)
    endif()
endif()

# ---------------------------------------------------------------------------
# Ogg (OVERRIDE_FIND_PACKAGE so vorbis's find_package(Ogg) reuses this)
# ---------------------------------------------------------------------------
set(INSTALL_DOCS OFF)
set(INSTALL_PKG_CONFIG_MODULE OFF)
set(INSTALL_CMAKE_PACKAGE_MODULE OFF)

FetchContent_Declare(Ogg
    GIT_REPOSITORY https://github.com/xiph/ogg.git
    GIT_TAG v1.3.5
    GIT_SHALLOW TRUE
    OVERRIDE_FIND_PACKAGE
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(Ogg)

if (NOT TARGET Ogg::ogg AND TARGET ogg)
    add_library(Ogg::ogg ALIAS ogg)
endif()
if (NOT TARGET Ogg::Ogg AND TARGET ogg)
    add_library(Ogg::Ogg ALIAS ogg)
endif()

# ---------------------------------------------------------------------------
# Vorbis
# ---------------------------------------------------------------------------
FetchContent_Declare(Vorbis
    GIT_REPOSITORY https://github.com/xiph/vorbis.git
    GIT_TAG v1.3.7
    GIT_SHALLOW TRUE
    OVERRIDE_FIND_PACKAGE
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(Vorbis)

if (NOT TARGET Vorbis::Vorbis AND TARGET vorbis)
    add_library(Vorbis::Vorbis ALIAS vorbis)
endif()
if (NOT TARGET Vorbis::VorbisFile AND TARGET vorbisfile)
    add_library(Vorbis::VorbisFile ALIAS vorbisfile)
endif()
if (NOT TARGET Vorbis::VorbisEnc AND TARGET vorbisenc)
    add_library(Vorbis::VorbisEnc ALIAS vorbisenc)
endif()

# ---------------------------------------------------------------------------
# Theora v1.1.1 has no CMakeLists; compile the portable decoder sources.
# ---------------------------------------------------------------------------
FetchContent_Declare(theora
    GIT_REPOSITORY https://github.com/xiph/theora.git
    GIT_TAG v1.1.1
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL
)
FetchContent_GetProperties(theora)
if (NOT theora_POPULATED)
    FetchContent_Populate(theora)
endif()

if (NOT TARGET theora)
    add_library(theora STATIC
        "${theora_SOURCE_DIR}/lib/apiwrapper.c"
        "${theora_SOURCE_DIR}/lib/bitpack.c"
        "${theora_SOURCE_DIR}/lib/cpu.c"
        "${theora_SOURCE_DIR}/lib/decapiwrapper.c"
        "${theora_SOURCE_DIR}/lib/decinfo.c"
        "${theora_SOURCE_DIR}/lib/decode.c"
        "${theora_SOURCE_DIR}/lib/dequant.c"
        "${theora_SOURCE_DIR}/lib/fragment.c"
        "${theora_SOURCE_DIR}/lib/huffdec.c"
        "${theora_SOURCE_DIR}/lib/idct.c"
        "${theora_SOURCE_DIR}/lib/info.c"
        "${theora_SOURCE_DIR}/lib/internal.c"
        "${theora_SOURCE_DIR}/lib/mathops.c"
        "${theora_SOURCE_DIR}/lib/quant.c"
        "${theora_SOURCE_DIR}/lib/state.c"
    )
    target_include_directories(theora
        PUBLIC
        "${theora_SOURCE_DIR}/include"
        PRIVATE
        "${theora_SOURCE_DIR}/lib"
    )
    target_link_libraries(theora PUBLIC ogg)
    set_target_properties(theora PROPERTIES
        POSITION_INDEPENDENT_CODE ON
    )
endif()

if (NOT TARGET Theora::Theora)
    add_library(Theora::Theora ALIAS theora)
endif()

# ---------------------------------------------------------------------------
# LZO 2.10
# ---------------------------------------------------------------------------
set(ENABLE_STATIC ON)
set(ENABLE_SHARED OFF)
FetchContent_Declare(lzo
    URL https://www.oberhumer.com/opensource/lzo/download/lzo-2.10.tar.gz
    URL_HASH SHA256=c0f892943208266f9b6543b3ae308fab6284c5c90e627931446fb49b4221a072
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(lzo)

if (NOT TARGET LZO::LZO)
    if (TARGET lzo_static_lib)
        target_include_directories(lzo_static_lib PUBLIC "${lzo_SOURCE_DIR}/include")
        add_library(LZO::LZO ALIAS lzo_static_lib)
    elseif (TARGET lzo2)
        target_include_directories(lzo2 PUBLIC "${lzo_SOURCE_DIR}/include")
        add_library(LZO::LZO ALIAS lzo2)
    elseif (TARGET lzo)
        target_include_directories(lzo PUBLIC "${lzo_SOURCE_DIR}/include")
        add_library(LZO::LZO ALIAS lzo)
    else()
        message(FATAL_ERROR "LZO was fetched but no library target was created")
    endif()
endif()

set(BUILD_SHARED_LIBS ${_XRAY_ANDROID_SAVED_BUILD_SHARED_LIBS})
set(CMAKE_UNITY_BUILD ${_XRAY_ANDROID_SAVED_UNITY_BUILD})

message(STATUS "Android dependencies: SDL2, OpenAL, Ogg, Vorbis, Theora, LZO")
