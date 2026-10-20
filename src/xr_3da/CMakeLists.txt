if (ANDROID)
    add_library(xr_3da SHARED)
else()
    add_executable(xr_3da)
endif()

target_sources(xr_3da PRIVATE
    entry_point.cpp
    stdafx.h
)

if (ANDROID)
    target_sources(xr_3da PRIVATE
        android_bootstrap.cpp
        android_bootstrap.h
        android_gles_renderer.cpp
        android_gles_renderer.h
    )
endif()

if (NOT ANDROID)
    target_sources(xr_3da PRIVATE
        resource.h
        resource.rc

        splash_oxr.bmp
        stalker_cop.ico
        stalker_cs.ico
        stalker_soc.ico
    )
endif()

set_property(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    PROPERTY
    VS_STARTUP_PROJECT xr_3da
)

target_include_directories(xr_3da
    PRIVATE
    "${XRAY_SOURCE_DIR}/src"
)

target_link_libraries(xr_3da
    PRIVATE
    xrCore
    xrAPI
    xrEngine
    $<$<NOT:$<BOOL:${ANDROID}>>:xrRender_GL>
    xrGame
    $<$<BOOL:${ANDROID}>:SDL2::SDL2>
    $<$<BOOL:${ANDROID}>:log>
    $<$<BOOL:${ANDROID}>:android>
    $<$<BOOL:${ANDROID}>:EGL>
    $<$<BOOL:${ANDROID}>:GLESv3>
    $<$<BOOL:${ANDROID}>:OpenSLES>
    $<$<BOOL:${ANDROID}>:z>
)

if (XR_ANDROID_GLRENDER AND TARGET xrRender_GL)
  add_dependencies(xr_3da xrRender_GL)
endif()

if (NOT ANDROID)
    set_target_properties(xr_3da PROPERTIES
        PREFIX ""
    )
endif()

if (ANDROID)
    install(TARGETS xr_3da LIBRARY
        DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    )
else()
    install(TARGETS xr_3da RUNTIME
        DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )
endif()
