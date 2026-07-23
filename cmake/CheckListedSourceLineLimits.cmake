# Copyright (c) 2026 DOMINGUEZ Vincent
# Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
# See LICENSE for details.

if(NOT DEFINED PIXELART_SOURCE_ROOT)
    message(FATAL_ERROR "PIXELART_SOURCE_ROOT must point to the repository root")
endif()

find_program(pixelart_rg rg REQUIRED)
find_program(pixelart_xargs xargs REQUIRED)
find_program(pixelart_wc wc REQUIRED)
find_program(pixelart_sed sed REQUIRED)
find_program(pixelart_sort sort REQUIRED)

execute_process(
    COMMAND "${pixelart_rg}" --files -0
        -g "*.{c,cc,cpp,cxx,h,hh,hpp,hxx,ipp,inl,inc,tpp,m,mm,cu,cuh,metal,glsl,vert,frag,qml,py,js,jsx,ts,tsx,java,kt,kts,swift,rs,go,sh,bash,zsh,ps1,cmake}"
        -g "CMakeLists.txt"
        -g "Makefile"
        -g "!build*/**"
        -g "!third_party/**"
        -g "!vendor/**"
    COMMAND "${pixelart_xargs}" -0 "${pixelart_wc}" -l
    COMMAND "${pixelart_sed}" "/ total$/d"
    COMMAND "${pixelart_sort}" -nr
    WORKING_DIRECTORY "${PIXELART_SOURCE_ROOT}"
    RESULTS_VARIABLE pixelart_source_list_results
    OUTPUT_VARIABLE pixelart_source_list_output
    ERROR_VARIABLE pixelart_source_list_error
    OUTPUT_STRIP_TRAILING_WHITESPACE)

foreach(command_result IN LISTS pixelart_source_list_results)
    if(NOT command_result EQUAL 0)
        message(FATAL_ERROR
            "Failed to generate the source line list "
            "(pipeline results: ${pixelart_source_list_results}):\n"
            "${pixelart_source_list_error}")
    endif()
endforeach()

if(pixelart_source_list_output STREQUAL "")
    message(FATAL_ERROR "The generated source line list is empty")
endif()

string(REPLACE "\n" ";" pixelart_source_list_entries "${pixelart_source_list_output}")
set(pixelart_line_limited_sources)
foreach(source_list_entry IN LISTS pixelart_source_list_entries)
    string(STRIP "${source_list_entry}" source_list_entry)
    if(NOT source_list_entry MATCHES "^[0-9]+[ \t]+.+$")
        message(FATAL_ERROR "Invalid generated source-list entry: ${source_list_entry}")
    endif()
    string(REGEX REPLACE "^[0-9]+[ \t]+" "" relative_path "${source_list_entry}")
    list(APPEND pixelart_line_limited_sources "${relative_path}")
endforeach()

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
