// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "render/MpsEffectRenderer.hpp"

namespace px {

struct MpsEffectRenderer::Impl {};

MpsEffectRenderer::MpsEffectRenderer() = default;

MpsEffectRenderer::~MpsEffectRenderer() = default;

bool MpsEffectRenderer::available() const {
    return false;
}

bool MpsEffectRenderer::render_active_cel(const Document&, const GpuEffectRequest&) {
    last_error_ = "MPS backend is only available on macOS";
    return false;
}

bool MpsEffectRenderer::read_output_pixels(std::vector<Pixel>&) const {
    return false;
}

GpuBackendCapabilities MpsEffectRenderer::capabilities() const {
    return {};
}

bool MpsEffectRenderer::used_chunking() const {
    return false;
}

void MpsEffectRenderer::destroy() {}

} // namespace px
