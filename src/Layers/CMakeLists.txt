add_subdirectory(xrAPI)

option(XR_ANDROID_GLRENDER "Experimental Android GL renderer compile-only probe" OFF)

if (WIN32)
    add_subdirectory(xrRenderPC_R4)
endif()

if (NOT ANDROID OR XR_ANDROID_GLRENDER)
    add_subdirectory(xrRenderPC_GL)
endif()
