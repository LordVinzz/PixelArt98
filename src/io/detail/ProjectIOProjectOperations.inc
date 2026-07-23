// Internal implementation fragment for ProjectIO project operations.
// This file is included by its owning translation unit; do not compile it separately.

Document document_from_pixels(int width, int height, std::vector<Pixel> pixels) {
    MemoryTraceScope trace("document_from_pixels");
    memory_trace_vector("document_from_pixels.input_pixels", pixels);
    Document document = document_from_pixels_impl(width, height, std::move(pixels));
    if (document.has_active_cel()) {
        memory_trace_vector("document_from_pixels.document_pixels", document.active_cel().pixels);
    }
    memory_trace_vector("document_from_pixels.source_after_move", pixels);
    return document;
}

bool decode_png_streaming_rgba(const std::string& path,
                               int& width,
                               int& height,
                               std::vector<Pixel>& pixels,
                               std::string* error,
                               const ImageImportProgressCallback& progress) {
    MemoryTraceScope trace("decode_png_streaming_rgba", path);
    const bool ok = decode_png_streaming_rgba_impl(path, width, height, pixels, error, progress);
    memory_trace_vector("decode_png_streaming_rgba.output", pixels);
    return ok;
}

bool save_project(const std::string& path, const Document& document, const ModelDocument& model, std::string* error) {
    return save_project(std::filesystem::path(path), document, model, error);
}

bool save_project_archive(const std::filesystem::path& path, const Document& document,
                          const ModelDocument& model,
                          const std::vector<unsigned char>* recovery_history,
                          std::string* error) {
    std::FILE* file = open_project_file(path, true);
    if (file == nullptr) {
        set_error(error, "Could not create project archive: " + path_display(path));
        return false;
    }

    mz_zip_archive zip{};
    if (!mz_zip_writer_init_cfile(&zip, file, 0)) {
        std::fclose(file);
        set_error(error, "Could not create project archive: " + path_display(path));
        return false;
    }

    bool ok = true;
    auto root = document_to_json(document);
    std::string json = root.dump(2);
    ok = ok && mz_zip_writer_add_mem(&zip, "project.json", json.data(), json.size(), MZ_BEST_COMPRESSION);
    std::string model_json = model_to_json(model);
    ok = ok && mz_zip_writer_add_mem(&zip, "model.json", model_json.data(), model_json.size(), MZ_BEST_COMPRESSION);
    if (recovery_history != nullptr) {
        ok = ok && mz_zip_writer_add_mem(&zip, "history.cbor", recovery_history->data(),
                                         recovery_history->size(), MZ_BEST_COMPRESSION);
    }

    for (std::size_t fi = 0; fi < document.frames.size() && ok; ++fi) {
        for (std::size_t li = 0; li < document.frames[fi].cels.size() && ok; ++li) {
            std::vector<unsigned char> png;
            ok = encode_png_rgba(document.width, document.height, document.frames[fi].cels[li].pixels, png);
            if (ok) {
                std::string name = "cels/frame_" + std::to_string(fi) + "_layer_" + std::to_string(li) + ".png";
                ok = mz_zip_writer_add_mem(&zip, name.c_str(), png.data(), png.size(), MZ_BEST_COMPRESSION);
            }
        }
    }
    for (std::size_t li = 0; li < document.layers.size() && ok; ++li) {
        const Layer& layer = document.layers[li];
        if (layer.mask.empty()) {
            continue;
        }
        std::vector<unsigned char> png;
        ok = encode_png_rgba(document.width, document.height, mask_to_rgba(layer.mask), png);
        if (ok) {
            std::string name = "masks/layer_" + std::to_string(li) + ".png";
            ok = mz_zip_writer_add_mem(&zip, name.c_str(), png.data(), png.size(), MZ_BEST_COMPRESSION);
        }
    }

    ok = ok && mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);
    const bool closed = std::fclose(file) == 0;
    if (!ok) {
        set_error(error, "Could not finish project archive");
        return false;
    }
    if (!closed) {
        set_error(error, "Could not close project archive: " + path_display(path));
        return false;
    }
    return true;
}

bool save_project(const std::filesystem::path& path, const Document& document,
                  const ModelDocument& model, std::string* error) {
    return save_project_archive(path, document, model, nullptr, error);
}

bool save_recovery_project(const std::filesystem::path& path, const Document& document,
                           const ModelDocument& model, std::string* error) {
    std::vector<unsigned char> history;
    if (!serialize_recovery_history(document, history, error)) return false;
    return save_project_archive(path, document, model, &history, error);
}

bool load_project(const std::string& path, ProjectBundle& out_bundle, std::string* error) {
    return load_project(std::filesystem::path(path), out_bundle, error);
}

bool load_project(const std::filesystem::path& path, ProjectBundle& out_bundle, std::string* error) {
    std::FILE* file = open_project_file(path, false);
    if (file == nullptr) {
        set_error(error, "Could not open project archive: " + path_display(path));
        return false;
    }

    mz_zip_archive zip{};
    if (!mz_zip_reader_init_cfile(&zip, file, 0, 0)) {
        std::fclose(file);
        set_error(error, "Could not open project archive: " + path_display(path));
        return false;
    }

    std::size_t json_size = 0;
    void* json_mem = mz_zip_reader_extract_file_to_heap(&zip, "project.json", &json_size, 0);
    if (!json_mem) {
        mz_zip_reader_end(&zip);
        std::fclose(file);
        set_error(error, "project.json missing from archive");
        return false;
    }

    Document document;
    bool ok = false;
    try {
        ok = json_to_document(nlohmann::json::parse(std::string(static_cast<char*>(json_mem), json_size)), document, error);
    } catch (const std::exception& ex) {
        set_error(error, ex.what());
    }
    mz_free(json_mem);

    if (ok) {
        auto root = document_to_json(document);
        (void)root;
        for (std::size_t fi = 0; fi < document.frames.size() && ok; ++fi) {
            for (std::size_t li = 0; li < document.frames[fi].cels.size() && ok; ++li) {
                std::string name = "cels/frame_" + std::to_string(fi) + "_layer_" + std::to_string(li) + ".png";
                std::size_t png_size = 0;
                void* png_mem = mz_zip_reader_extract_file_to_heap(&zip, name.c_str(), &png_size, 0);
                if (!png_mem) {
                    set_error(error, "Missing cel image: " + name);
                    ok = false;
                    break;
                }
                ok = decode_png_rgba(static_cast<unsigned char*>(png_mem), png_size, document.width, document.height,
                                     document.frames[fi].cels[li].pixels);
                mz_free(png_mem);
            }
        }
        for (std::size_t li = 0; li < document.layers.size() && ok; ++li) {
            std::string name = "masks/layer_" + std::to_string(li) + ".png";
            std::size_t png_size = 0;
            void* png_mem = mz_zip_reader_extract_file_to_heap(&zip, name.c_str(), &png_size, 0);
            if (!png_mem) {
                if (document.layers[li].mask_enabled) {
                    document.layers[li].mask.assign(static_cast<std::size_t>(document.width * document.height), 255);
                }
                continue;
            }
            std::vector<Pixel> pixels;
            ok = decode_png_rgba(static_cast<unsigned char*>(png_mem), png_size, document.width, document.height, pixels);
            mz_free(png_mem);
            if (ok) {
                document.layers[li].mask = rgba_to_mask(pixels);
            }
        }
    }

    ModelDocument model = ModelDocument::create_default();
    std::size_t model_size = 0;
    void* model_mem = mz_zip_reader_extract_file_to_heap(&zip, "model.json", &model_size, 0);
    if (model_mem) {
        std::string model_text(static_cast<char*>(model_mem), model_size);
        std::string model_error;
        if (!model_from_json(model_text, model, &model_error)) {
            set_error(error, model_error);
            ok = false;
        }
        mz_free(model_mem);
    }

    mz_zip_reader_end(&zip);
    std::fclose(file);
    if (!ok) {
        return false;
    }
    out_bundle = {std::move(document), std::move(model)};
    return true;
}

bool load_recovery_project(const std::filesystem::path& path, ProjectBundle& out_bundle,
                           std::string* error) {
    if (!load_project(path, out_bundle, error)) return false;
    std::FILE* file = open_project_file(path, false);
    if (file == nullptr) {
        set_error(error, "Could not reopen recovery archive: " + path_display(path));
        return false;
    }
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_cfile(&zip, file, 0, 0)) {
        std::fclose(file);
        set_error(error, "Could not read recovery archive history");
        return false;
    }
    std::size_t history_size = 0;
    void* history_memory =
        mz_zip_reader_extract_file_to_heap(&zip, "history.cbor", &history_size, 0);
    bool ok = history_memory != nullptr;
    if (!ok) {
        set_error(error, "history.cbor missing from recovery archive");
    } else {
        ok = deserialize_recovery_history(static_cast<unsigned char*>(history_memory),
                                          history_size, out_bundle.document, error);
        mz_free(history_memory);
    }
    mz_zip_reader_end(&zip);
    std::fclose(file);
    return ok;
}

bool import_image(const std::string& path,
                  Document& out_document,
                  std::string* error,
                  const ImageImportProgressCallback& progress) {
    MemoryTraceScope trace("import_image", path);
    publish_image_progress(progress, 0.01f, "Opening image", "Opening file", 0, 0, true);
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = nullptr;
    {
        MemoryTraceScope stbi_trace("stbi_load", path);
        pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
        if (pixels != nullptr && width > 0 && height > 0) {
            const std::size_t pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
            memory_trace_event("buffer",
                               {},
                               "stbi_load.result",
                               pixels,
                               pixel_count,
                               pixel_count,
                               sizeof(Pixel),
                               "channels=" + std::to_string(channels));
        }
    }
    if (!pixels) {
        const char* reason = stbi_failure_reason();
        const std::string failure = reason == nullptr ? std::string("unknown decoder failure") : std::string(reason);
        std::vector<Pixel> streaming_pixels;
        std::string streaming_error;
        if (decode_png_streaming_rgba(path, width, height, streaming_pixels, &streaming_error, progress)) {
            memory_trace_vector("import_image.streaming_pixels_before_document", streaming_pixels);
            out_document = document_from_pixels(width, height, std::move(streaming_pixels));
            if (out_document.has_active_cel()) {
                memory_trace_vector("import_image.out_document_pixels", out_document.active_cel().pixels);
            }
            publish_image_progress(progress,
                                   1.0f,
                                   "Decoded " + std::to_string(width) + " x " + std::to_string(height) + " PNG",
                                   "Decoded image",
                                   1,
                                   1);
            return true;
        }
        set_error(error, "Could not import image: " + path + " (" + failure + "; streaming PNG fallback: " + streaming_error + ")");
        return false;
    }
    const std::size_t pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<Pixel> imported_pixels(reinterpret_cast<Pixel*>(pixels),
                                       reinterpret_cast<Pixel*>(pixels + pixel_count * sizeof(Pixel)));
    memory_trace_vector("import_image.imported_pixels", imported_pixels);
    stbi_image_free(pixels);
    memory_trace_note("import_image.stbi_buffer_freed");
    out_document = document_from_pixels(width, height, std::move(imported_pixels));
    if (out_document.has_active_cel()) {
        memory_trace_vector("import_image.out_document_pixels", out_document.active_cel().pixels);
    }
    publish_image_progress(progress,
                           1.0f,
                           "Decoded " + std::to_string(width) + " x " + std::to_string(height) + " image",
                           "Decoded image",
                           1,
                           1);
    return true;
}

