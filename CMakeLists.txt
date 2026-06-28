# Copyright (c) 2026 DOMINGUEZ Vincent
# Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
# See LICENSE for details.

cmake_minimum_required(VERSION 3.24)

project(PixelArt98
    VERSION 0.1.0
    DESCRIPTION "A native pixel-art editor inspired by classic Paint workflows"
    LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(PIXELART_BUILD_BOTH "Build both SDL2 and GLFW launchers" ON)
set(PIXELART_BACKEND "SDL2" CACHE STRING "Default backend: SDL2 or GLFW")
set_property(CACHE PIXELART_BACKEND PROPERTY STRINGS SDL2 GLFW)
option(PIXELART_ENABLE_ANIMATION "Enable animation timeline features" ON)
option(PIXELART_ENABLE_MODEL "Enable cuboid model texturing features" ON)
option(PIXELART_BUILD_TESTS "Build unit tests" ON)
option(PIXELART_ENABLE_SANITIZERS "Enable ASan/UBSan for Debug builds with supported compilers" ON)
option(PIXELART_MEMORY_PROFILE_BUILD "Build with debug symbols, -O1, and frame pointers for allocation profiling" OFF)
option(PIXELART_USE_LINUX_PORTAL_FILE_DIALOGS "Use xdg-desktop-portal for native Linux file dialogs" ON)
option(PIXELART_ENABLE_ONNXRUNTIME "Enable ONNX Runtime-backed depth map extraction when ONNX Runtime is installed" ON)
option(PIXELART_ENABLE_OPENCV_DNN "Enable OpenCV DNN-backed depth map extraction when OpenCV is installed" OFF)
set(ONNXRUNTIME_ROOT "" CACHE PATH "ONNX Runtime SDK root directory")

if(PIXELART_ENABLE_ONNXRUNTIME)
    find_path(ONNXRUNTIME_INCLUDE_DIR
        NAMES onnxruntime_cxx_api.h
        HINTS ${ONNXRUNTIME_ROOT} ENV ONNXRUNTIME_ROOT
        PATH_SUFFIXES include include/onnxruntime/core/session)
    find_library(ONNXRUNTIME_LIBRARY
        NAMES onnxruntime
        HINTS ${ONNXRUNTIME_ROOT} ENV ONNXRUNTIME_ROOT
        PATH_SUFFIXES lib lib64)
endif()
if(PIXELART_ENABLE_OPENCV_DNN)
    find_package(OpenCV QUIET COMPONENTS core dnn imgproc)
endif()

include(cmake/Dependencies.cmake)

function(pixelart_enable_project_warnings target)
    target_compile_options(${target} PRIVATE
        $<$<COMPILE_LANG_AND_ID:C,GNU,Clang,AppleClang>:
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -pedantic-errors
            -Wconversion
            -Wsign-conversion
            -Wshadow
            -Wnull-dereference
            -Wdouble-promotion
            -Wformat=2
        >
        $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -pedantic-errors
            -Wconversion
            -Wsign-conversion
            -Wshadow
            -Wnon-virtual-dtor
            -Wold-style-cast
            -Woverloaded-virtual
            -Wnull-dereference
            -Wdouble-promotion
            -Wformat=2
        >
        $<$<COMPILE_LANG_AND_ID:C,MSVC>:
            /W4
            /WX
        >
        $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
        >)
endfunction()

function(pixelart_enable_debug_sanitizers target)
    if(NOT PIXELART_ENABLE_SANITIZERS)
        return()
    endif()
    target_compile_options(${target} PRIVATE
        $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU,Clang,AppleClang>>:
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        >)
    target_link_options(${target} PRIVATE
        $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU,Clang,AppleClang>>:
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        >)
endfunction()

function(pixelart_enable_memory_profile_flags target)
    if(NOT PIXELART_MEMORY_PROFILE_BUILD)
        return()
    endif()
    target_compile_options(${target} PRIVATE
        $<$<COMPILE_LANG_AND_ID:C,GNU,Clang,AppleClang>:
            -g
            -O1
            -fno-omit-frame-pointer
        >
        $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:
            -g
            -O1
            -fno-omit-frame-pointer
        >)
    target_link_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:
            -g
            -fno-omit-frame-pointer
        >)
endfunction()

function(pixelart_configure_project_target target)
    pixelart_enable_project_warnings(${target})
    pixelart_enable_debug_sanitizers(${target})
    pixelart_enable_memory_profile_flags(${target})
endfunction()

add_library(pixelart_core
    src/core/Document.cpp
    src/core/Filters.cpp
    src/core/MemoryTrace.cpp
    src/core/Model.cpp
    src/core/Tools.cpp
    src/io/ProjectIO.cpp
    src/third_party/stb_impl.cpp)
target_include_directories(pixelart_core PUBLIC src)
target_link_libraries(pixelart_core
    PUBLIC nlohmann_json::nlohmann_json
    PRIVATE miniz_lib gifenc_lib stb_headers)
target_compile_definitions(pixelart_core PUBLIC
    PIXELART_ENABLE_ANIMATION=$<BOOL:${PIXELART_ENABLE_ANIMATION}>
    PIXELART_ENABLE_MODEL=$<BOOL:${PIXELART_ENABLE_MODEL}>)
pixelart_configure_project_target(pixelart_core)

add_library(pixelart_app
    src/platform/NativeFileDialogProvider.cpp
    src/depth/DepthMapExtractor.cpp
    src/render/GpuEffectRenderer.cpp
    src/render/GLCanvasTexture.cpp
    src/render/GLTiledCanvasTexture.cpp
    src/render/Renderer3D.cpp
    src/render/MeshUvOverlayRenderer.cpp
    src/ui/AppSettings.cpp
    src/ui/EmbeddedAssets.cpp
    src/ui/EmbeddedTransformIcons.c
    src/ui/SplashScreen.cpp
    src/ui/StartupSplash.cpp
    src/ui/EditorApp.cpp)
if(APPLE)
    target_sources(pixelart_app PRIVATE src/render/MpsEffectRenderer.mm)
    set_source_files_properties(src/render/MpsEffectRenderer.mm PROPERTIES
        LANGUAGE CXX
        COMPILE_FLAGS "-x objective-c++ -fobjc-arc")
else()
    target_sources(pixelart_app PRIVATE src/render/MpsEffectRenderer.cpp)
endif()
target_include_directories(pixelart_app PUBLIC src)
if(ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIBRARY)
    target_include_directories(pixelart_app SYSTEM PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
    target_link_libraries(pixelart_app PRIVATE ${ONNXRUNTIME_LIBRARY})
    target_compile_definitions(pixelart_app PUBLIC PIXELART_HAS_ONNXRUNTIME=1)
else()
    target_compile_definitions(pixelart_app PUBLIC PIXELART_HAS_ONNXRUNTIME=0)
    if(PIXELART_ENABLE_ONNXRUNTIME)
        message(STATUS "ONNX Runtime not found; depth extraction will try other model backends or stay unavailable.")
    endif()
endif()
if(OpenCV_FOUND)
    target_include_directories(pixelart_app SYSTEM PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(pixelart_app PRIVATE ${OpenCV_LIBS})
    target_compile_definitions(pixelart_app PUBLIC PIXELART_HAS_OPENCV_DNN=1)
else()
    target_compile_definitions(pixelart_app PUBLIC PIXELART_HAS_OPENCV_DNN=0)
    if(PIXELART_ENABLE_OPENCV_DNN)
        message(STATUS "OpenCV DNN not found; depth extraction will skip the OpenCV model backend.")
    endif()
endif()
target_link_libraries(pixelart_app PUBLIC pixelart_core imgui_backend glad_gl_core_33 nfd::nfd OpenGL::GL PRIVATE stb_headers)
if(APPLE)
    target_link_libraries(pixelart_app PUBLIC "-framework Foundation" "-framework Metal" "-framework MetalPerformanceShaders")
endif()
target_compile_definitions(pixelart_app PUBLIC
    GL_SILENCE_DEPRECATION
    NOMINMAX
    WIN32_LEAN_AND_MEAN)
pixelart_configure_project_target(pixelart_app)

set(PIXELART_BUILD_SDL OFF)
set(PIXELART_BUILD_GLFW OFF)
if(PIXELART_BUILD_BOTH)
    set(PIXELART_BUILD_SDL ON)
    set(PIXELART_BUILD_GLFW ON)
elseif(PIXELART_BACKEND STREQUAL "SDL2")
    set(PIXELART_BUILD_SDL ON)
elseif(PIXELART_BACKEND STREQUAL "GLFW")
    set(PIXELART_BUILD_GLFW ON)
else()
    message(FATAL_ERROR "PIXELART_BACKEND must be SDL2 or GLFW")
endif()

if(PIXELART_BUILD_SDL)
    add_executable(pixelart_sdl2 src/platform/main_sdl.cpp)
    target_link_libraries(pixelart_sdl2 PRIVATE pixelart_app)
    target_include_directories(pixelart_sdl2 SYSTEM PRIVATE
        ${sdl2_SOURCE_DIR}/include
        ${sdl2_BINARY_DIR}/include
        ${sdl2_BINARY_DIR}/include/SDL2
        ${nfd_SOURCE_DIR}/src/include)
    target_compile_definitions(pixelart_sdl2 PRIVATE GL_SILENCE_DEPRECATION)
    pixelart_configure_project_target(pixelart_sdl2)
    install(TARGETS pixelart_sdl2 RUNTIME DESTINATION .)
endif()

if(PIXELART_BUILD_GLFW)
    add_executable(pixelart_glfw src/platform/main_glfw.cpp)
    target_link_libraries(pixelart_glfw PRIVATE pixelart_app)
    target_include_directories(pixelart_glfw SYSTEM PRIVATE
        ${glfw_SOURCE_DIR}/include
        ${nfd_SOURCE_DIR}/src/include)
    target_compile_definitions(pixelart_glfw PRIVATE GL_SILENCE_DEPRECATION)
    pixelart_configure_project_target(pixelart_glfw)
    install(TARGETS pixelart_glfw RUNTIME DESTINATION .)
endif()

if(PIXELART_BACKEND STREQUAL "GLFW" AND TARGET pixelart_glfw)
    add_custom_target(pixelart DEPENDS pixelart_glfw)
elseif(TARGET pixelart_sdl2)
    add_custom_target(pixelart DEPENDS pixelart_sdl2)
endif()

install(DIRECTORY res/skyboxes DESTINATION res)
install(FILES
    ${CMAKE_SOURCE_DIR}/README.md
    ${CMAKE_SOURCE_DIR}/LICENSE
    ${CMAKE_SOURCE_DIR}/THIRD-PARTY-NOTICES.md
    DESTINATION .)

if(PIXELART_BUILD_TESTS)
    enable_testing()
    add_executable(pixelart_tests tests/core_tests.cpp)
    target_link_libraries(pixelart_tests PRIVATE pixelart_app)
    pixelart_configure_project_target(pixelart_tests)
    add_test(NAME pixelart_core_tests COMMAND pixelart_tests)

    add_executable(pixelart_io_tests tests/io_roundtrip_tests.cpp)
    target_link_libraries(pixelart_io_tests PRIVATE pixelart_core miniz_lib stb_headers)
    pixelart_configure_project_target(pixelart_io_tests)
    add_test(NAME pixelart_io_tests COMMAND pixelart_io_tests)

    add_executable(pixelart_huge_image_memory_tests tests/huge_image_memory_tests.cpp)
    target_link_libraries(pixelart_huge_image_memory_tests PRIVATE pixelart_app)
    pixelart_configure_project_target(pixelart_huge_image_memory_tests)
    add_test(NAME pixelart_huge_image_memory_tests COMMAND pixelart_huge_image_memory_tests)

    add_executable(pixelart_shader_compile_tests tests/shader_compile_tests.cpp)
    if(APPLE)
        target_sources(pixelart_shader_compile_tests PRIVATE tests/metal_shader_compile_tests.mm)
        set_source_files_properties(tests/metal_shader_compile_tests.mm PROPERTIES
            LANGUAGE CXX
            COMPILE_FLAGS "-x objective-c++ -fobjc-arc")
    endif()
    target_link_libraries(pixelart_shader_compile_tests PRIVATE pixelart_app)
    target_include_directories(pixelart_shader_compile_tests SYSTEM PRIVATE
        ${glfw_SOURCE_DIR}/include)
    target_compile_definitions(pixelart_shader_compile_tests PRIVATE GL_SILENCE_DEPRECATION)
    pixelart_configure_project_target(pixelart_shader_compile_tests)
    add_test(NAME pixelart_shader_compile_tests COMMAND pixelart_shader_compile_tests)
    set_tests_properties(pixelart_shader_compile_tests PROPERTIES TIMEOUT 15)

    add_executable(pixelart_opengl_smoke tests/opengl_smoke.cpp)
    target_link_libraries(pixelart_opengl_smoke PRIVATE pixelart_app)
    target_include_directories(pixelart_opengl_smoke SYSTEM PRIVATE
        ${glfw_SOURCE_DIR}/include)
    target_compile_definitions(pixelart_opengl_smoke PRIVATE GL_SILENCE_DEPRECATION)
    pixelart_configure_project_target(pixelart_opengl_smoke)
    add_test(NAME pixelart_opengl_smoke COMMAND pixelart_opengl_smoke)
    set_tests_properties(pixelart_opengl_smoke PROPERTIES TIMEOUT 15)

    if(APPLE)
        add_executable(pixelart_mps_smoke tests/mps_smoke.cpp)
        target_link_libraries(pixelart_mps_smoke PRIVATE pixelart_app)
        pixelart_configure_project_target(pixelart_mps_smoke)
        add_test(NAME pixelart_mps_smoke COMMAND pixelart_mps_smoke)
        set_tests_properties(pixelart_mps_smoke PROPERTIES TIMEOUT 15)
    endif()

    add_executable(pixelart_gpu_backend_benchmark tests/gpu_backend_benchmark.cpp)
    target_link_libraries(pixelart_gpu_backend_benchmark PRIVATE pixelart_app)
    target_include_directories(pixelart_gpu_backend_benchmark SYSTEM PRIVATE
        ${glfw_SOURCE_DIR}/include)
    target_compile_definitions(pixelart_gpu_backend_benchmark PRIVATE GL_SILENCE_DEPRECATION)
    pixelart_configure_project_target(pixelart_gpu_backend_benchmark)
endif()
