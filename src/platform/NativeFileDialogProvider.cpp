#include "platform/NativeFileDialogProvider.hpp"

#include <utility>
#include <vector>

namespace px {

namespace {

std::vector<nfdu8filteritem_t> to_native_filters(FileFilterList filters) {
    std::vector<nfdu8filteritem_t> native_filters;
    native_filters.reserve(filters.count);
    for (std::size_t i = 0; i < filters.count; ++i) {
        const FileFilter& filter = filters.items[i];
        if (filter.name != nullptr && filter.spec != nullptr && filter.spec[0] != '\0') {
            native_filters.push_back({filter.name, filter.spec});
        }
    }
    return native_filters;
}

DialogResult result_from_nfd(nfdresult_t result, nfdu8char_t* out_path) {
    if (result == NFD_OKAY) {
        DialogResult dialog;
        dialog.accepted = true;
        if (out_path != nullptr) {
            dialog.path = out_path;
            NFD_FreePathU8(out_path);
        }
        return dialog;
    }

    if (result == NFD_CANCEL) {
        return {};
    }

    const char* error = NFD_GetError();
    DialogResult dialog;
    dialog.error = error != nullptr ? error : "Native file dialog failed";
    return dialog;
}

} // namespace

NativeFileDialogProvider::NativeFileDialogProvider(ParentWindowProvider parent_window)
    : parent_window_(std::move(parent_window)) {
}

DialogResult NativeFileDialogProvider::open_file(FileFilterList filters, const std::string& default_path) {
    std::vector<nfdu8filteritem_t> native_filters = to_native_filters(filters);
    nfdopendialogu8args_t args = {};
    args.filterList = native_filters.empty() ? nullptr : native_filters.data();
    args.filterCount = static_cast<nfdfiltersize_t>(native_filters.size());
    args.defaultPath = default_path.empty() ? nullptr : default_path.c_str();
    args.parentWindow = parent_window();

    nfdu8char_t* out_path = nullptr;
    return result_from_nfd(NFD_OpenDialogU8_With(&out_path, &args), out_path);
}

DialogResult NativeFileDialogProvider::save_file(FileFilterList filters,
                                                 const std::string& default_path,
                                                 const std::string& default_name) {
    std::vector<nfdu8filteritem_t> native_filters = to_native_filters(filters);
    nfdsavedialogu8args_t args = {};
    args.filterList = native_filters.empty() ? nullptr : native_filters.data();
    args.filterCount = static_cast<nfdfiltersize_t>(native_filters.size());
    args.defaultPath = default_path.empty() ? nullptr : default_path.c_str();
    args.defaultName = default_name.empty() ? nullptr : default_name.c_str();
    args.parentWindow = parent_window();

    nfdu8char_t* out_path = nullptr;
    return result_from_nfd(NFD_SaveDialogU8_With(&out_path, &args), out_path);
}

nfdwindowhandle_t NativeFileDialogProvider::parent_window() const {
    if (parent_window_) {
        return parent_window_();
    }
    return {};
}

} // namespace px
