include(FetchContent)
find_package(OpenGL REQUIRED)

set(FETCHCONTENT_QUIET FALSE)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "Avoid network updates for already populated dependencies" FORCE)

set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
FetchContent_Declare(SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-2.32.10
    UPDATE_DISCONNECTED TRUE
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(SDL2)

if(TARGET SDL2::SDL2-static)
    set(PIXELART_SDL_TARGET SDL2::SDL2-static)
elseif(TARGET SDL2::SDL2)
    set(PIXELART_SDL_TARGET SDL2::SDL2)
elseif(TARGET SDL2-static)
    set(PIXELART_SDL_TARGET SDL2-static)
elseif(TARGET SDL2)
    set(PIXELART_SDL_TARGET SDL2)
else()
    message(FATAL_ERROR "Unable to locate SDL2 target after FetchContent")
endif()

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
if(UNIX AND NOT APPLE)
    set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(glfw
    URL https://github.com/glfw/glfw/archive/refs/tags/3.4.tar.gz
    UPDATE_DISCONNECTED TRUE)
FetchContent_MakeAvailable(glfw)

set(NFD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NFD_BUILD_SDL2_TESTS OFF CACHE BOOL "" FORCE)
set(NFD_INSTALL OFF CACHE BOOL "" FORCE)
if(UNIX AND NOT APPLE)
    set(NFD_PORTAL ${PIXELART_USE_LINUX_PORTAL_FILE_DIALOGS} CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(nfd
    GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
    GIT_TAG v1.3.0
    UPDATE_DISCONNECTED TRUE
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(nfd)

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

FetchContent_Declare(imgui_src
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG docking
    UPDATE_DISCONNECTED TRUE
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(imgui_src)
add_library(imgui_backend STATIC
    ${imgui_src_SOURCE_DIR}/imgui.cpp
    ${imgui_src_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_src_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_src_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_src_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_src_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_src_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${imgui_src_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp)
target_include_directories(imgui_backend SYSTEM PUBLIC
    ${imgui_src_SOURCE_DIR}
    ${imgui_src_SOURCE_DIR}/backends)
target_link_libraries(imgui_backend PUBLIC ${PIXELART_SDL_TARGET} glfw OpenGL::GL)
target_compile_definitions(imgui_backend PUBLIC
    IMGUI_ENABLE_DOCKING
    GL_SILENCE_DEPRECATION)
