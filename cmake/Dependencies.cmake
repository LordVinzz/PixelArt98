# Copyright (c) 2026 DOMINGUEZ Vincent
# Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
# See LICENSE for details.

include(FetchContent)
find_package(OpenGL REQUIRED)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets OpenGLWidgets)

set(FETCHCONTENT_QUIET FALSE)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "Avoid network updates for already populated dependencies" FORCE)

FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
    UPDATE_DISCONNECTED TRUE
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(miniz_src
    GIT_REPOSITORY https://github.com/richgel999/miniz.git
    GIT_TAG 3.1.1
    UPDATE_DISCONNECTED TRUE
    GIT_SHALLOW TRUE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_FUZZERS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(INSTALL_PROJECT OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(miniz_src)
target_include_directories(miniz SYSTEM PUBLIC ${miniz_src_SOURCE_DIR})
add_library(miniz_lib ALIAS miniz)

FetchContent_Declare(stb_src
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 31c1ad37456438565541f4919958214b6e762fb4
    UPDATE_DISCONNECTED TRUE)
FetchContent_MakeAvailable(stb_src)
add_library(stb_headers INTERFACE)
target_include_directories(stb_headers SYSTEM INTERFACE ${stb_src_SOURCE_DIR})

FetchContent_Declare(gifenc_src
    GIT_REPOSITORY https://github.com/lecram/gifenc.git
    GIT_TAG 87acd487dfa2f2a638eec751a1d6c2fff60822da
    UPDATE_DISCONNECTED TRUE)
FetchContent_MakeAvailable(gifenc_src)
add_library(gifenc_lib STATIC ${gifenc_src_SOURCE_DIR}/gifenc.c)
target_include_directories(gifenc_lib SYSTEM PUBLIC ${gifenc_src_SOURCE_DIR})

add_library(glad_gl_core_33 STATIC ${CMAKE_CURRENT_LIST_DIR}/../src/third_party/glad_gl.c)
target_include_directories(glad_gl_core_33 SYSTEM PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../src/third_party)
target_compile_definitions(glad_gl_core_33 PUBLIC GL_SILENCE_DEPRECATION)
