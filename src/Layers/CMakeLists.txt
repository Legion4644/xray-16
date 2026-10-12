add_subdirectory(xrAPI)

if (WIN32)
    add_subdirectory(xrRenderPC_R4)
endif()

if (NOT ANDROID)
    add_subdirectory(xrRenderPC_GL)
endif()
