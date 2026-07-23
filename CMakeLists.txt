# Copyright (c) 2026 DOMINGUEZ Vincent
# Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
# See LICENSE for details.

cmake_minimum_required(VERSION 3.24)

project(PixelArt98
    VERSION 0.1.0
    DESCRIPTION "A native pixel-art editor inspired by classic Paint workflows"
    LANGUAGES C CXX)

set(PIXELART_VERSION_OVERRIDE "" CACHE STRING
    "Version embedded in PixelArt98 instead of PROJECT_VERSION")
set(PIXELART_BUILD_COMMIT "" CACHE STRING
    "Git commit embedded in PixelArt98 when Git metadata is unavailable")

if(PIXELART_VERSION_OVERRIDE)
    set(PIXELART_BUILD_VERSION "${PIXELART_VERSION_OVERRIDE}")
else()
    set(PIXELART_BUILD_VERSION "${PROJECT_VERSION}")
endif()
set(PIXELART_GIT_COMMIT "${PIXELART_BUILD_COMMIT}")
set(PIXELART_GIT_SHORT_COMMIT "${PIXELART_BUILD_COMMIT}")
set(PIXELART_GIT_DESCRIPTION "${PIXELART_BUILD_VERSION}")
set(PIXELART_GIT_COMMIT_TIMESTAMP 0)
set(PIXELART_IS_RELEASE false)
find_package(Git QUIET)
if(Git_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        RESULT_VARIABLE PIXELART_GIT_RESULT
        OUTPUT_VARIABLE PIXELART_GIT_COMMIT_FROM_REPOSITORY
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(PIXELART_GIT_RESULT EQUAL 0)
        set(PIXELART_GIT_COMMIT "${PIXELART_GIT_COMMIT_FROM_REPOSITORY}")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            OUTPUT_VARIABLE PIXELART_GIT_SHORT_COMMIT
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" describe --tags --always --dirty
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            OUTPUT_VARIABLE PIXELART_GIT_DESCRIPTION
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" show -s --format=%ct HEAD
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            OUTPUT_VARIABLE PIXELART_GIT_COMMIT_TIMESTAMP
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" describe --tags --exact-match
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            RESULT_VARIABLE PIXELART_GIT_TAG_RESULT
            OUTPUT_VARIABLE PIXELART_GIT_TAG
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if(PIXELART_GIT_TAG_RESULT EQUAL 0 AND
           PIXELART_GIT_TAG MATCHES "^v?([0-9]+\\.[0-9]+\\.[0-9]+)$")
            set(PIXELART_BUILD_VERSION "${CMAKE_MATCH_1}")
            set(PIXELART_IS_RELEASE true)
        endif()

        execute_process(
            COMMAND "${GIT_EXECUTABLE}" rev-parse --absolute-git-dir
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            OUTPUT_VARIABLE PIXELART_GIT_DIRECTORY
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if(PIXELART_GIT_DIRECTORY)
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                "${PIXELART_GIT_DIRECTORY}/HEAD")
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" symbolic-ref -q HEAD
                WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                RESULT_VARIABLE PIXELART_GIT_REF_RESULT
                OUTPUT_VARIABLE PIXELART_GIT_REF
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET)
            if(PIXELART_GIT_REF_RESULT EQUAL 0)
                set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                    "${PIXELART_GIT_DIRECTORY}/${PIXELART_GIT_REF}")
            endif()
        endif()
    endif()
endif()

if(NOT PIXELART_GIT_COMMIT)
    set(PIXELART_GIT_COMMIT "unknown")
endif()
if(NOT PIXELART_GIT_SHORT_COMMIT)
    set(PIXELART_GIT_SHORT_COMMIT "unknown")
endif()

configure_file(
    cmake/PixelArtVersion.hpp.in
    generated/PixelArtVersion.hpp
    @ONLY)
add_library(pixelart_version INTERFACE)
target_include_directories(pixelart_version INTERFACE
    "${CMAKE_CURRENT_BINARY_DIR}/generated")

message(STATUS
    "PixelArt98 version: ${PIXELART_BUILD_VERSION} (${PIXELART_GIT_SHORT_COMMIT})")

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
    src/core/DocumentTransforms.cpp
    src/core/Filters.cpp
    src/core/GraphEffect.cpp
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
    src/ui/AnimationExport.cpp
    src/ui/EditorController.cpp
    src/ui/TextRasterizer.cpp)
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
target_link_libraries(pixelart_app
    PUBLIC pixelart_core glad_gl_core_33 Qt6::Core Qt6::Gui
    PRIVATE stb_headers)
if(NOT APPLE)
    target_link_libraries(pixelart_app PUBLIC OpenGL::GL)
endif()
if(APPLE)
    target_link_libraries(pixelart_app PUBLIC "-framework Foundation" "-framework Metal" "-framework MetalPerformanceShaders")
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
    ${CMAKE_SOURCE_DIR}/res/icons/text.png
    ${CMAKE_SOURCE_DIR}/res/icons/new_frame_animation.png
    ${CMAKE_SOURCE_DIR}/res/icons/play_pause_animation.png
    ${CMAKE_SOURCE_DIR}/res/icons/stop_animation.png)
set(PIXELART_APP_LOGO ${CMAKE_SOURCE_DIR}/res/logo.png)
qt_add_executable(PixelArt98
    src/platform/main_qt.cpp
    src/ui/EmbeddedAssets.cpp
    src/ui/AdjustmentWidgets.cpp
    src/ui/GraphEffectWidget.cpp
    src/ui/QtCanvasWidget.cpp
    src/ui/QtModelPreviewWidget.cpp
    src/ui/QtMainWindow.cpp)
qt_add_resources(PixelArt98 tool_icons
    PREFIX "/"
    BASE "${CMAKE_SOURCE_DIR}/res"
    FILES ${PIXELART_TOOL_ICONS})
qt_add_resources(PixelArt98 app_branding
    PREFIX "/"
    BASE "${CMAKE_SOURCE_DIR}/res"
    FILES ${PIXELART_APP_LOGO})
if(APPLE)
    set(PIXELART_APP_ICON_MACOS ${CMAKE_SOURCE_DIR}/res/PixelArt98.icns)
    target_sources(PixelArt98 PRIVATE ${PIXELART_TOOL_ICONS} ${PIXELART_APP_ICON_MACOS})
    set_source_files_properties(${PIXELART_TOOL_ICONS} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources/icons")
    set_source_files_properties(${PIXELART_APP_ICON_MACOS} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
elseif(WIN32)
    target_sources(PixelArt98 PRIVATE ${CMAKE_SOURCE_DIR}/res/PixelArt98.rc)
endif()
target_link_libraries(PixelArt98 PRIVATE
    pixelart_app
    pixelart_version
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Widgets
    Qt6::OpenGLWidgets)
target_compile_definitions(PixelArt98 PRIVATE GL_SILENCE_DEPRECATION)
set_target_properties(PixelArt98 PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_ICON_FILE PixelArt98.icns
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

if(UNIX AND NOT APPLE)
    install(FILES ${PIXELART_APP_LOGO}
        DESTINATION share/pixmaps
        RENAME pixelart98.png)
    install(FILES ${CMAKE_SOURCE_DIR}/res/pixelart98.desktop
        DESTINATION share/applications)
endif()

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

    add_executable(pixelart_canvas_viewport_tests tests/canvas_viewport_tests.cpp)
    target_link_libraries(pixelart_canvas_viewport_tests PRIVATE pixelart_app)
    pixelart_configure_project_target(pixelart_canvas_viewport_tests)
    add_test(NAME pixelart_canvas_viewport_tests COMMAND pixelart_canvas_viewport_tests)

    add_executable(pixelart_filter_coverage_tests tests/filter_coverage_tests.cpp)
    target_link_libraries(pixelart_filter_coverage_tests PRIVATE pixelart_app)
    pixelart_configure_project_target(pixelart_filter_coverage_tests)
    add_test(NAME pixelart_filter_coverage_tests COMMAND pixelart_filter_coverage_tests)

    add_executable(pixelart_tools_coverage_tests tests/tools_coverage_tests.cpp)
    target_link_libraries(pixelart_tools_coverage_tests PRIVATE pixelart_core)
    pixelart_configure_project_target(pixelart_tools_coverage_tests)
    add_test(NAME pixelart_tools_coverage_tests COMMAND pixelart_tools_coverage_tests)

    add_executable(pixelart_document_coverage_tests tests/document_coverage_tests.cpp)
    target_link_libraries(pixelart_document_coverage_tests PRIVATE pixelart_core)
    pixelart_configure_project_target(pixelart_document_coverage_tests)
    add_test(NAME pixelart_document_coverage_tests COMMAND pixelart_document_coverage_tests)

    add_executable(pixelart_graph_effect_tests tests/graph_effect_tests.cpp)
    target_link_libraries(pixelart_graph_effect_tests PRIVATE pixelart_core)
    pixelart_configure_project_target(pixelart_graph_effect_tests)
    add_test(NAME pixelart_graph_effect_tests COMMAND pixelart_graph_effect_tests)

    qt_add_executable(pixelart_animation_export_tests tests/animation_export_tests.cpp)
    target_link_libraries(pixelart_animation_export_tests PRIVATE pixelart_app Qt6::Core Qt6::Gui)
    pixelart_configure_project_target(pixelart_animation_export_tests)
    add_test(NAME pixelart_animation_export_tests COMMAND pixelart_animation_export_tests)
    set_tests_properties(pixelart_animation_export_tests PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    qt_add_executable(pixelart_graph_effect_ui_tests
        tests/graph_effect_ui_tests.cpp
        src/ui/AdjustmentWidgets.cpp
        src/ui/GraphEffectWidget.cpp)
    target_link_libraries(pixelart_graph_effect_ui_tests PRIVATE
        pixelart_core Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
    pixelart_configure_project_target(pixelart_graph_effect_ui_tests)
    add_test(NAME pixelart_graph_effect_ui_tests COMMAND pixelart_graph_effect_ui_tests)
    set_tests_properties(pixelart_graph_effect_ui_tests PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    qt_add_executable(pixelart_qt_ui_tests
        tests/qt_ui_tests.cpp
        src/ui/AdjustmentWidgets.cpp
        src/ui/GraphEffectWidget.cpp
        src/ui/QtCanvasWidget.cpp
        src/ui/QtMainWindow.cpp
        src/ui/QtModelPreviewWidget.cpp)
    qt_add_resources(pixelart_qt_ui_tests ui_test_tool_icons
        PREFIX "/"
        BASE "${CMAKE_SOURCE_DIR}/res"
        FILES ${PIXELART_TOOL_ICONS})
    target_link_libraries(pixelart_qt_ui_tests PRIVATE
        pixelart_app pixelart_version Qt6::Core Qt6::Gui Qt6::Network
        Qt6::Widgets Qt6::OpenGLWidgets Qt6::Test)
    target_compile_definitions(pixelart_qt_ui_tests PRIVATE GL_SILENCE_DEPRECATION)
    pixelart_configure_project_target(pixelart_qt_ui_tests)
    add_test(NAME pixelart_qt_ui_tests COMMAND pixelart_qt_ui_tests)
    set_tests_properties(pixelart_qt_ui_tests PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

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

        qt_add_executable(pixelart_qt_mps_effects_e2e
            tests/qt_mps_effects_e2e.cpp
            src/ui/AdjustmentWidgets.cpp
            src/ui/GraphEffectWidget.cpp
            src/ui/QtCanvasWidget.cpp
            src/ui/QtMainWindow.cpp
            src/ui/QtModelPreviewWidget.cpp)
        qt_add_resources(pixelart_qt_mps_effects_e2e test_tool_icons
            PREFIX "/"
            BASE "${CMAKE_SOURCE_DIR}/res"
            FILES ${PIXELART_TOOL_ICONS})
        target_link_libraries(pixelart_qt_mps_effects_e2e PRIVATE
            pixelart_app pixelart_version Qt6::Core Qt6::Gui Qt6::Network
            Qt6::Widgets Qt6::OpenGLWidgets Qt6::Test)
        target_compile_definitions(pixelart_qt_mps_effects_e2e PRIVATE GL_SILENCE_DEPRECATION)
        pixelart_configure_project_target(pixelart_qt_mps_effects_e2e)
        add_test(NAME pixelart_qt_mps_effects_e2e COMMAND pixelart_qt_mps_effects_e2e)
        set_tests_properties(pixelart_qt_mps_effects_e2e PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()

    add_executable(pixelart_gpu_backend_benchmark tests/gpu_backend_benchmark.cpp)
    target_link_libraries(pixelart_gpu_backend_benchmark PRIVATE pixelart_app Qt6::Gui)
    target_compile_definitions(pixelart_gpu_backend_benchmark PRIVATE GL_SILENCE_DEPRECATION)
    pixelart_configure_project_target(pixelart_gpu_backend_benchmark)
endif()
