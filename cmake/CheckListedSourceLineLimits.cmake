# Copyright (c) 2026 DOMINGUEZ Vincent
# Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
# See LICENSE for details.

cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED PIXELART_SOURCE_ROOT)
    message(FATAL_ERROR "PIXELART_SOURCE_ROOT must point to the repository root")
endif()
if(NOT IS_DIRECTORY "${PIXELART_SOURCE_ROOT}")
    message(FATAL_ERROR "PIXELART_SOURCE_ROOT is not a directory: ${PIXELART_SOURCE_ROOT}")
endif()

cmake_path(ABSOLUTE_PATH PIXELART_SOURCE_ROOT NORMALIZE
    OUTPUT_VARIABLE pixelart_source_root)

set(pixelart_source_extensions
    c cc cpp cxx h hh hpp hxx ipp inl inc tpp m mm cu cuh metal
    glsl vert frag qml py js jsx ts tsx java kt kts swift rs go
    sh bash zsh ps1 cmake)
set(pixelart_gitignored_directory_names
    out dist package CMakeFiles Testing _deps examples logs models)

function(pixelart_should_prune_directory relative_path directory_name output_variable)
    set(should_prune FALSE)
    if(directory_name MATCHES "^[.]")
        set(should_prune TRUE)
    elseif(relative_path STREQUAL directory_name AND
           (directory_name MATCHES "^build" OR
            directory_name STREQUAL "third_party" OR
            directory_name STREQUAL "vendor"))
        set(should_prune TRUE)
    else()
        list(FIND pixelart_gitignored_directory_names
            "${directory_name}" ignored_index)
        if(NOT ignored_index EQUAL -1 OR
           directory_name STREQUAL "build" OR
           directory_name MATCHES "^build-" OR
           directory_name MATCHES "^cmake-build-" OR
           directory_name MATCHES ".+-build$" OR
           directory_name MATCHES ".+-subbuild$" OR
           directory_name MATCHES "[.]app$" OR
           directory_name MATCHES "[.]dSYM$")
            set(should_prune TRUE)
        endif()
    endif()
    set(${output_variable} "${should_prune}" PARENT_SCOPE)
endfunction()

set_property(GLOBAL PROPERTY PIXELART_LINE_LIMIT_SOURCE_FILES "")
function(pixelart_collect_source_files absolute_directory relative_directory)
    file(GLOB directory_entries LIST_DIRECTORIES TRUE
        RELATIVE "${absolute_directory}" "${absolute_directory}/*")
    foreach(entry IN LISTS directory_entries)
        if(relative_directory STREQUAL "")
            set(relative_path "${entry}")
        else()
            set(relative_path "${relative_directory}/${entry}")
        endif()
        set(absolute_path "${absolute_directory}/${entry}")

        if(IS_SYMLINK "${absolute_path}")
            continue()
        endif()
        if(IS_DIRECTORY "${absolute_path}")
            pixelart_should_prune_directory(
                "${relative_path}" "${entry}" should_prune)
            if(NOT should_prune)
                pixelart_collect_source_files(
                    "${absolute_path}" "${relative_path}")
            endif()
            continue()
        endif()

        if(entry MATCHES "^[.]" OR
           entry STREQUAL "CTestTestfile.cmake" OR
           entry STREQUAL "cmake_install.cmake")
            continue()
        endif()
        if(entry STREQUAL "CMakeLists.txt" OR entry STREQUAL "Makefile")
            set_property(GLOBAL APPEND PROPERTY
                PIXELART_LINE_LIMIT_SOURCE_FILES "${relative_path}")
            continue()
        endif()
        get_filename_component(source_extension "${entry}" LAST_EXT)
        if(NOT source_extension STREQUAL "")
            string(SUBSTRING "${source_extension}" 1 -1 source_extension)
            list(FIND pixelart_source_extensions
                "${source_extension}" extension_index)
            if(NOT extension_index EQUAL -1)
                set_property(GLOBAL APPEND PROPERTY
                    PIXELART_LINE_LIMIT_SOURCE_FILES "${relative_path}")
            endif()
        endif()
    endforeach()
endfunction()

pixelart_collect_source_files("${pixelart_source_root}" "")
get_property(pixelart_line_limited_sources GLOBAL PROPERTY
    PIXELART_LINE_LIMIT_SOURCE_FILES)
if(NOT pixelart_line_limited_sources)
    message(FATAL_ERROR "The generated source line list is empty")
endif()
list(REMOVE_DUPLICATES pixelart_line_limited_sources)
list(SORT pixelart_line_limited_sources)

set(pixelart_max_source_lines 800)
foreach(relative_path IN LISTS pixelart_line_limited_sources)
    set(absolute_path "${PIXELART_SOURCE_ROOT}/${relative_path}")
    if(NOT EXISTS "${absolute_path}")
        message(FATAL_ERROR "Listed source does not exist: ${relative_path}")
    endif()

    file(READ "${absolute_path}" source_contents)
    string(REGEX MATCHALL "\n" source_newlines "${source_contents}")
    list(LENGTH source_newlines source_line_count)
    if(NOT source_contents STREQUAL "" AND NOT source_contents MATCHES "\n$")
        math(EXPR source_line_count "${source_line_count} + 1")
    endif()

    if(source_line_count GREATER pixelart_max_source_lines)
        message(FATAL_ERROR
            "${relative_path} contains ${source_line_count} lines; "
            "the maximum is ${pixelart_max_source_lines}")
    endif()
    message(STATUS "${relative_path}: ${source_line_count}/${pixelart_max_source_lines} lines")
endforeach()
