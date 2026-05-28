#pragma once

#include "ui/FileDialogs.hpp"

#include <functional>

#include <nfd.h>

namespace px {

class NativeFileDialogProvider final : public FileDialogProvider {
public:
    using ParentWindowProvider = std::function<nfdwindowhandle_t()>;

    explicit NativeFileDialogProvider(ParentWindowProvider parent_window = {});

    DialogResult open_file(FileFilterList filters, const std::string& default_path) override;
    DialogResult save_file(FileFilterList filters,
                           const std::string& default_path,
                           const std::string& default_name) override;

private:
    nfdwindowhandle_t parent_window() const;

    ParentWindowProvider parent_window_;
};

} // namespace px
