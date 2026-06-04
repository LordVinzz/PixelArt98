// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#pragma once

#include <cstddef>
#include <string>

namespace px {

struct FileFilter {
    const char* name = "";
    const char* spec = "";
};

struct FileFilterList {
    const FileFilter* items = nullptr;
    std::size_t count = 0;
};

struct DialogResult {
    bool accepted = false;
    std::string path;
    std::string error;
};

class FileDialogProvider {
public:
    virtual ~FileDialogProvider() = default;

    virtual DialogResult open_file(FileFilterList filters, const std::string& default_path) = 0;
    virtual DialogResult save_file(FileFilterList filters,
                                   const std::string& default_path,
                                   const std::string& default_name) = 0;
};

} // namespace px
