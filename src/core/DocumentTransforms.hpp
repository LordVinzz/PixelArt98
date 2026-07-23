// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.

#pragma once

#include "core/Filters.hpp"
#include "core/Model.hpp"

namespace px {

[[nodiscard]] bool resize_document_image(Document& document, int width, int height,
                                         ResamplingMode resampling,
                                         ModelDocument* model = nullptr);
[[nodiscard]] bool resize_document_canvas(Document& document, int width, int height,
                                          int offset_x, int offset_y,
                                          ModelDocument* model = nullptr,
                                          const char* undo_name = "Resize Canvas");
[[nodiscard]] bool crop_document(Document& document, int x, int y, int width, int height,
                                 ModelDocument* model = nullptr);
[[nodiscard]] bool flip_document_horizontal(Document& document, ModelDocument* model = nullptr);
[[nodiscard]] bool flip_document_vertical(Document& document, ModelDocument* model = nullptr);
[[nodiscard]] bool rotate_document_90_clockwise(Document& document,
                                                ModelDocument* model = nullptr);
[[nodiscard]] bool rotate_document_90_counter_clockwise(Document& document,
                                                        ModelDocument* model = nullptr);
[[nodiscard]] bool rotate_document_180(Document& document, ModelDocument* model = nullptr);

} // namespace px
