# Copyright (c) 2026 DOMINGUEZ Vincent
# Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
# See LICENSE for details.

if(NOT DEFINED PIXELART_SOURCE_ROOT)
    message(FATAL_ERROR "PIXELART_SOURCE_ROOT must point to the repository root")
endif()

set(pixelart_default_line_limited_sources
    src/ui/QtMainWindow.cpp
    src/io/ProjectIO.cpp
    src/core/Document.cpp
    src/ui/GraphEffectWidget.cpp
    src/core/GraphEffect.cpp
    src/core/Filters.cpp
    src/core/Model.cpp
    tests/core_tests.cpp
    src/render/GpuEffectRenderer.cpp
    src/render/Renderer3D.cpp
    src/core/Svg.cpp
    tests/graph_effect_ui_tests.cpp
    tests/qt_ui_tests.cpp
    tests/graph_effect_tests.cpp
    src/ui/EmbeddedAssets.cpp
    src/ui/QtCanvasWidget.cpp
    src/render/MpsEffectRenderer.mm
    src/depth/DepthMapExtractor.cpp)

set(pixelart_source_list "${PIXELART_SOURCE_ROOT}/list.txt")
if(EXISTS "${pixelart_source_list}")
    file(STRINGS "${pixelart_source_list}" pixelart_source_list_entries)
    set(pixelart_line_limited_sources)
    foreach(source_list_entry IN LISTS pixelart_source_list_entries)
        string(STRIP "${source_list_entry}" source_list_entry)
        if(source_list_entry STREQUAL "")
            continue()
        endif()
        if(NOT source_list_entry MATCHES "^[0-9]+[ \t]+.+$")
            message(FATAL_ERROR "Invalid list.txt entry: ${source_list_entry}")
        endif()
        string(REGEX REPLACE "^[0-9]+[ \t]+" "" relative_path "${source_list_entry}")
        list(APPEND pixelart_line_limited_sources "${relative_path}")
    endforeach()
    if(NOT pixelart_line_limited_sources)
        message(FATAL_ERROR "list.txt does not contain any source paths")
    endif()
else()
    # Keep CI useful if the task manifest is not distributed with the sources.
    set(pixelart_line_limited_sources ${pixelart_default_line_limited_sources})
endif()

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
