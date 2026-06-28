// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#pragma once

#include "core/Document.hpp"
#include "core/Model.hpp"

#include <functional>
#include <string>
#include <vector>

namespace px {

struct ImageImportProgress {
    float fraction = 0.0f;
    int done = 0;
    int total = 0;
    bool indeterminate = false;
    std::string phase;
    std::string status;
};

using ImageImportProgressCallback = std::function<void(const ImageImportProgress&)>;

struct ProjectBundle {
    Document document;
    ModelDocument model;
};

bool save_project(const std::string& path, const Document& document, const ModelDocument& model, std::string* error = nullptr);
bool load_project(const std::string& path, ProjectBundle& out_bundle, std::string* error = nullptr);

Document document_from_pixels(int width, int height, std::vector<Pixel> pixels);
bool decode_png_streaming_rgba(const std::string& path,
                               int& width,
                               int& height,
                               std::vector<Pixel>& pixels,
                               std::string* error = nullptr,
                               const ImageImportProgressCallback& progress = {});
bool import_image(const std::string& path,
                  Document& out_document,
                  std::string* error = nullptr,
                  const ImageImportProgressCallback& progress = {});
bool import_image_as_layer(const std::string& path, Document& document, const std::string& layer_name = "", std::string* error = nullptr);
bool export_png(const std::string& path, const Document& document, int frame, std::string* error = nullptr);
bool export_spritesheet(const std::string& png_path, const std::string& json_path, const Document& document, std::string* error = nullptr);
bool export_gif(const std::string& path, const Document& document, std::string* error = nullptr);
bool export_apng(const std::string& path, const Document& document, std::string* error = nullptr);
bool import_aseprite(const std::string& path, Document& out_document, std::string* error = nullptr);
bool export_aseprite(const std::string& path, const Document& document, std::string* error = nullptr);
bool export_model_json(const std::string& path, const ModelDocument& model, std::string* error = nullptr);
bool import_model_json(const std::string& path, ModelDocument& out_model, std::string* error = nullptr);
bool export_gltf_model(const std::string& path, const ModelDocument& model, const std::string& texture_path = "texture.png", std::string* error = nullptr);
bool export_threejs_pack(const std::string& path, const Document& document, const ModelDocument& model, std::string* error = nullptr);
bool import_stl_model(const std::string& path,
                      ModelDocument& out_model,
                      std::string* error = nullptr,
                      MeshUvUnwrapResult* unwrap_result = nullptr);
bool export_stl_model(const std::string& path, const ModelDocument& model, std::string* error = nullptr);
bool export_minecraft_model(const std::string& path, const ModelDocument& model, const std::string& texture_path = "texture.png", std::string* error = nullptr);
bool import_minecraft_model(const std::string& path, ModelDocument& out_model, std::string* error = nullptr);

} // namespace px
