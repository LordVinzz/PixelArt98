# Copyright (c) 2026 DOMINGUEZ Vincent
# Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.

if(NOT DEFINED PIXELART_DEPLOY_ROOT OR PIXELART_DEPLOY_ROOT STREQUAL "")
    message(FATAL_ERROR "PIXELART_DEPLOY_ROOT must name the installed package directory")
endif()
if(NOT DEFINED PIXELART_DEPLOY_PLATFORM OR PIXELART_DEPLOY_PLATFORM STREQUAL "")
    message(FATAL_ERROR "PIXELART_DEPLOY_PLATFORM must be windows, linux, or macos")
endif()

cmake_path(ABSOLUTE_PATH PIXELART_DEPLOY_ROOT NORMALIZE
    OUTPUT_VARIABLE pixelart_deploy_root)

function(pixelart_require_path relative_path description)
    set(full_path "${pixelart_deploy_root}/${relative_path}")
    if(NOT EXISTS "${full_path}")
        message(FATAL_ERROR
            "Deployment is missing ${description}: ${full_path}")
    endif()
    message(STATUS "Found ${description}: ${relative_path}")
endfunction()

function(pixelart_require_qt_conf relative_path)
    pixelart_require_path("${relative_path}" "qt.conf")
    file(READ "${pixelart_deploy_root}/${relative_path}" qt_conf_contents)
    if(NOT qt_conf_contents MATCHES "\\[Paths\\]")
        message(FATAL_ERROR "${relative_path} has no [Paths] section")
    endif()
endfunction()

if(PIXELART_DEPLOY_PLATFORM STREQUAL "windows")
    pixelart_require_path("bin/PixelArt98.exe" "application executable")
    pixelart_require_path("bin/Qt6Core.dll" "Qt Core runtime")
    pixelart_require_path("bin/Qt6Gui.dll" "Qt GUI runtime")
    pixelart_require_path("bin/Qt6Widgets.dll" "Qt Widgets runtime")
    pixelart_require_path("plugins/platforms/qwindows.dll" "Qt Windows platform plugin")
    pixelart_require_path("plugins/imageformats/qjpeg.dll" "Qt JPEG image plugin")
    pixelart_require_qt_conf("bin/qt.conf")

    file(GLOB compiler_runtime_files
        "${pixelart_deploy_root}/vc_redist.x64.exe"
        "${pixelart_deploy_root}/bin/vc_redist.x64.exe"
        "${pixelart_deploy_root}/bin/msvcp*.dll"
        "${pixelart_deploy_root}/bin/vcruntime*.dll")
    if(NOT compiler_runtime_files)
        message(FATAL_ERROR
            "Deployment is missing the Microsoft Visual C++ runtime redistributable")
    endif()
    message(STATUS "Found Microsoft Visual C++ runtime deployment")
elseif(PIXELART_DEPLOY_PLATFORM STREQUAL "linux")
    pixelart_require_path("bin/PixelArt98" "application executable")
    pixelart_require_path("lib/libQt6Core.so.6" "Qt Core runtime")
    pixelart_require_path("lib/libQt6Gui.so.6" "Qt GUI runtime")
    pixelart_require_path("lib/libQt6Widgets.so.6" "Qt Widgets runtime")
    pixelart_require_path("plugins/platforms/libqxcb.so" "Qt XCB platform plugin")
    pixelart_require_path("plugins/imageformats/libqjpeg.so" "Qt JPEG image plugin")
    pixelart_require_qt_conf("bin/qt.conf")
elseif(PIXELART_DEPLOY_PLATFORM STREQUAL "macos")
    set(bundle_root "PixelArt98.app/Contents")
    pixelart_require_path(
        "${bundle_root}/MacOS/PixelArt98" "application executable")
    pixelart_require_path(
        "${bundle_root}/Frameworks/QtCore.framework/Versions/A/QtCore"
        "Qt Core framework")
    pixelart_require_path(
        "${bundle_root}/Frameworks/QtGui.framework/Versions/A/QtGui"
        "Qt GUI framework")
    pixelart_require_path(
        "${bundle_root}/Frameworks/QtWidgets.framework/Versions/A/QtWidgets"
        "Qt Widgets framework")
    pixelart_require_path(
        "${bundle_root}/PlugIns/platforms/libqcocoa.dylib"
        "Qt Cocoa platform plugin")
    pixelart_require_path(
        "${bundle_root}/PlugIns/imageformats/libqjpeg.dylib"
        "Qt JPEG image plugin")
    pixelart_require_qt_conf("${bundle_root}/Resources/qt.conf")

    if(PIXELART_REQUIRE_UNIVERSAL_MACOS)
        execute_process(
            COMMAND lipo -archs
                "${pixelart_deploy_root}/${bundle_root}/MacOS/PixelArt98"
            RESULT_VARIABLE lipo_result
            OUTPUT_VARIABLE executable_architectures
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_VARIABLE lipo_error)
        if(NOT lipo_result EQUAL 0)
            message(FATAL_ERROR
                "Could not inspect macOS executable architectures: ${lipo_error}")
        endif()
        if(NOT executable_architectures MATCHES "(^| )arm64( |$)" OR
           NOT executable_architectures MATCHES "(^| )x86_64( |$)")
            message(FATAL_ERROR
                "macOS executable is not universal: ${executable_architectures}")
        endif()
        message(STATUS
            "Found universal macOS executable: ${executable_architectures}")
    endif()
else()
    message(FATAL_ERROR
        "Unsupported PIXELART_DEPLOY_PLATFORM: ${PIXELART_DEPLOY_PLATFORM}")
endif()

message(STATUS
    "PixelArt98 ${PIXELART_DEPLOY_PLATFORM} deployment structure is complete")
