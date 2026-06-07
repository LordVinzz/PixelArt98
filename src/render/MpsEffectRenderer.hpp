// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#pragma once

#include "render/GpuEffectRenderer.hpp"

#include <memory>
#include <string>
#include <vector>

namespace px {

class MpsEffectRenderer {
public:
    MpsEffectRenderer();
    ~MpsEffectRenderer();

    MpsEffectRenderer(const MpsEffectRenderer&) = delete;
    MpsEffectRenderer& operator=(const MpsEffectRenderer&) = delete;

    bool available() const;
    bool render_active_cel(const Document& document, const GpuEffectRequest& request);
    bool read_output_pixels(std::vector<Pixel>& pixels) const;
    void destroy();

    [[nodiscard]] GpuBackendCapabilities capabilities() const;
    [[nodiscard]] bool used_chunking() const;
    [[nodiscard]] const std::string& last_error() const noexcept { return last_error_; }

private:
    struct Impl;

    bool render_full_active_cel(const Document& document, const GpuEffectRequest& request);

    std::unique_ptr<Impl> impl_;
    std::string last_error_;
};

} // namespace px
