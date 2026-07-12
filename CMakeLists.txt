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

option(PIXELART_ENABLE_ANIMATION "Enable animation timeline features" ON)
option(PIXELART_ENABLE_MODEL "Enable cuboid model texturing features" ON)
option(PIXELART_BUILD_TESTS "Build unit tests" ON)
option(PIXELART_ENABLE_SANITIZERS "Enable ASan/UBSan for Debug builds with supported compilers" ON)
option(PIXELART_MEMORY_PROFILE_BUILD "Build with debug symbols, -O1, and frame pointers for allocation profiling" OFF)
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
    target_compile_definitions(${target} PRIVATE
        $<$<COMPILE_LANG_AND_ID:C,MSVC>:_CRT_SECURE_NO_WARNINGS>
        $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:_CRT_SECURE_NO_WARNINGS>)
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
    src/depth/DepthMapExtractor.cpp
    src/render/GpuEffectRenderer.cpp
    src/render/GLCanvasTexture.cpp
    src/render/GLTiledCanvasTexture.cpp
    src/render/Renderer3D.cpp
    src/render/MeshUvOverlayRenderer.cpp
    src/ui/AppSettings.cpp
    src/ui/EditorController.cpp)
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
target_link_libraries(pixelart_app PUBLIC pixelart_core glad_gl_core_33 PRIVATE stb_headers)
if(APPLE)
    target_link_libraries(pixelart_app PUBLIC Qt6::OpenGL "-framework Foundation" "-framework Metal" "-framework MetalPerformanceShaders")
else()
    target_link_libraries(pixelart_app PUBLIC OpenGL::GL)
endif()
target_compile_definitions(pixelart_app PUBLIC
    GL_SILENCE_DEPRECATION
    NOMINMAX
    WIN32_LEAN_AND_MEAN)
pixelart_configure_project_target(pixelart_app)

qt_standard_project_setup()
set(PIXELART_TOOL_ICONS
    ${CMAKE_SOURCE_DIR}/res/icons/brush.png ${CMAKE_SOURCE_DIR}/res/icons/bucket.png
    ${CMAKE_SOURCE_DIR}/res/icons/clone_stamp.png ${CMAKE_SOURCE_DIR}/res/icons/ellipse.png
    ${CMAKE_SOURCE_DIR}/res/icons/eraser.png ${CMAKE_SOURCE_DIR}/res/icons/eyedropper.png
    ${CMAKE_SOURCE_DIR}/res/icons/gradient.png ${CMAKE_SOURCE_DIR}/res/icons/lasso.png
    ${CMAKE_SOURCE_DIR}/res/icons/line.png ${CMAKE_SOURCE_DIR}/res/icons/magic_wand.png
    ${CMAKE_SOURCE_DIR}/res/icons/new_layer.png ${CMAKE_SOURCE_DIR}/res/icons/duplicate_layer.png
    ${CMAKE_SOURCE_DIR}/res/icons/remove_layer.png ${CMAKE_SOURCE_DIR}/res/icons/up.png
    ${CMAKE_SOURCE_DIR}/res/icons/reveal_mask.png ${CMAKE_SOURCE_DIR}/res/icons/hide_mask.png
    ${CMAKE_SOURCE_DIR}/res/icons/mask_from_selection.png ${CMAKE_SOURCE_DIR}/res/icons/clear_from_selection.png
    ${CMAKE_SOURCE_DIR}/res/icons/move_pixels.png ${CMAKE_SOURCE_DIR}/res/icons/pencil.png
    ${CMAKE_SOURCE_DIR}/res/icons/rectangle.png ${CMAKE_SOURCE_DIR}/res/icons/rectangle_select.png
    ${CMAKE_SOURCE_DIR}/res/icons/text.png)
qt_add_executable(PixelArt98
    src/platform/main_qt.cpp
    src/ui/EmbeddedAssets.cpp
    src/ui/QtCanvasWidget.cpp
    src/ui/QtModelPreviewWidget.cpp
    src/ui/QtMainWindow.cpp)
qt_add_resources(PixelArt98 tool_icons
    PREFIX "/"
    BASE "${CMAKE_SOURCE_DIR}/res"
    FILES ${PIXELART_TOOL_ICONS})
if(APPLE)
    target_sources(PixelArt98 PRIVATE ${PIXELART_TOOL_ICONS})
    set_source_files_properties(${PIXELART_TOOL_ICONS} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources/icons")
endif()
target_link_libraries(PixelArt98 PRIVATE
    pixelart_app
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets)
target_compile_definitions(PixelArt98 PRIVATE GL_SILENCE_DEPRECATION)
set_target_properties(PixelArt98 PROPERTIES
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE)
pixelart_configure_project_target(PixelArt98)
add_custom_target(pixelart DEPENDS PixelArt98)

install(TARGETS PixelArt98
    BUNDLE DESTINATION .
    RUNTIME DESTINATION .)

qt_generate_deploy_app_script(
    TARGET PixelArt98
    OUTPUT_SCRIPT pixelart_deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR)
install(SCRIPT ${pixelart_deploy_script})

install(DIRECTORY res/skyboxes DESTINATION res)
install(FILES
    ${CMAKE_SOURCE_DIR}/README.md
    ${CMAKE_SOURCE_DIR}/FEATURES.md
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

    add_executable(pixelart_editor_controller_tests tests/editor_controller_tests.cpp)
    target_link_libraries(pixelart_editor_controller_tests PRIVATE pixelart_app)
    pixelart_configure_project_target(pixelart_editor_controller_tests)
    add_test(NAME pixelart_editor_controller_tests COMMAND pixelart_editor_controller_tests)

    add_executable(pixelart_shader_compile_tests tests/shader_compile_tests.cpp)
    if(APPLE)
        target_sources(pixelart_shader_compile_tests PRIVATE tests/metal_shader_compile_tests.mm)
        set_source_files_properties(tests/metal_shader_compile_tests.mm PROPERTIES
            LANGUAGE CXX
            COMPILE_FLAGS "-x objective-c++ -fobjc-arc")
    endif()
    target_link_libraries(pixelart_shader_compile_tests PRIVATE pixelart_app Qt6::Gui)
    target_compile_definitions(pixelart_shader_compile_tests PRIVATE GL_SILENCE_DEPRECATION)
    pixelart_configure_project_target(pixelart_shader_compile_tests)
    add_test(NAME pixelart_shader_compile_tests COMMAND pixelart_shader_compile_tests)
    set_tests_properties(pixelart_shader_compile_tests PROPERTIES TIMEOUT 15)

    add_executable(pixelart_opengl_smoke tests/opengl_smoke.cpp)
    target_link_libraries(pixelart_opengl_smoke PRIVATE pixelart_app Qt6::Gui)
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
    target_link_libraries(pixelart_gpu_backend_benchmark PRIVATE pixelart_app Qt6::Gui)
    target_compile_definitions(pixelart_gpu_backend_benchmark PRIVATE GL_SILENCE_DEPRECATION)
    pixelart_configure_project_target(pixelart_gpu_backend_benchmark)
endif()
