// Internal implementation fragment for ProjectIO recovery serialization.
// This file is included by its owning translation unit; do not compile it separately.

nlohmann::json pixels_to_binary(const std::vector<Pixel>& pixels) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(pixels.size() * 4U);
    for (Pixel pixel : pixels) {
        bytes.push_back(r(pixel));
        bytes.push_back(g(pixel));
        bytes.push_back(b(pixel));
        bytes.push_back(a(pixel));
    }
    return nlohmann::json::binary(std::move(bytes));
}

bool pixels_from_binary(const nlohmann::json& value, std::vector<Pixel>& pixels) {
    if (value.is_null()) {
        pixels.clear();
        return true;
    }
    if (!value.is_binary()) return false;
    const auto& bytes = value.get_binary();
    if (bytes.size() % 4U != 0U) return false;
    pixels.resize(bytes.size() / 4U);
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        const std::size_t offset = index * 4U;
        pixels[index] = rgba(bytes[offset], bytes[offset + 1U], bytes[offset + 2U],
                             bytes[offset + 3U]);
    }
    return true;
}

nlohmann::json bytes_to_binary(const std::vector<std::uint8_t>& bytes) {
    return nlohmann::json::binary(bytes);
}

bool bytes_from_binary(const nlohmann::json& value, std::vector<std::uint8_t>& bytes) {
    if (value.is_null()) {
        bytes.clear();
        return true;
    }
    if (!value.is_binary()) return false;
    const auto& binary = value.get_binary();
    bytes.assign(binary.begin(), binary.end());
    return true;
}

nlohmann::json selection_to_recovery_json(const SelectionMask& selection) {
    return {{"width", selection.width}, {"height", selection.height},
            {"active", selection.active}, {"mask", bytes_to_binary(selection.mask)}};
}

bool selection_from_recovery_json(const nlohmann::json& value, SelectionMask& selection) {
    selection.width = value.value("width", 0);
    selection.height = value.value("height", 0);
    selection.active = value.value("active", false);
    return selection.width >= 0 && selection.height >= 0 &&
           bytes_from_binary(value.at("mask"), selection.mask) &&
           selection.mask.size() == static_cast<std::size_t>(selection.width) *
                                        static_cast<std::size_t>(selection.height);
}

nlohmann::json palette_to_recovery_json(const Palette& palette) {
    return {{"colors", pixels_to_binary(palette.colors)}, {"active", palette.active}};
}

bool palette_from_recovery_json(const nlohmann::json& value, Palette& palette) {
    return pixels_from_binary(value.at("colors"), palette.colors) &&
           (palette.active = value.value("active", 0), true);
}

nlohmann::json layer_to_recovery_json(const Layer& layer) {
    return {{"name", layer.name}, {"visible", layer.visible}, {"opacity", layer.opacity},
            {"blend_mode", layer_blend_mode_name(layer.blend_mode)},
            {"mask_enabled", layer.mask_enabled}, {"clip_to_below", layer.clip_to_below},
            {"mask", bytes_to_binary(layer.mask)}};
}

bool layer_from_recovery_json(const nlohmann::json& value, Layer& layer) {
    layer.name = value.value("name", "Layer");
    layer.visible = value.value("visible", true);
    layer.opacity = value.value("opacity", 1.0f);
    layer.blend_mode = layer_blend_mode_from_json(value.value("blend_mode", "Normal"));
    layer.mask_enabled = value.value("mask_enabled", false);
    layer.clip_to_below = value.value("clip_to_below", false);
    return bytes_from_binary(value.at("mask"), layer.mask);
}

nlohmann::json layers_to_recovery_json(const std::vector<Layer>& layers) {
    nlohmann::json result = nlohmann::json::array();
    for (const Layer& layer : layers) result.push_back(layer_to_recovery_json(layer));
    return result;
}

bool layers_from_recovery_json(const nlohmann::json& value, std::vector<Layer>& layers) {
    if (!value.is_array()) return false;
    layers.clear();
    layers.reserve(value.size());
    for (const auto& item : value) {
        Layer layer;
        if (!layer_from_recovery_json(item, layer)) return false;
        layers.push_back(std::move(layer));
    }
    return true;
}

nlohmann::json frames_to_recovery_json(const std::vector<Frame>& frames) {
    nlohmann::json result = nlohmann::json::array();
    for (const Frame& frame : frames) {
        nlohmann::json cels = nlohmann::json::array();
        for (const Cel& cel : frame.cels) {
            cels.push_back({{"x", cel.x}, {"y", cel.y}, {"pixels", pixels_to_binary(cel.pixels)}});
        }
        result.push_back({{"duration_ms", frame.duration_ms}, {"cels", std::move(cels)}});
    }
    return result;
}

bool frames_from_recovery_json(const nlohmann::json& value, std::vector<Frame>& frames) {
    if (!value.is_array()) return false;
    frames.clear();
    frames.reserve(value.size());
    for (const auto& item : value) {
        Frame frame;
        frame.duration_ms = item.value("duration_ms", 100);
        const auto& cels = item.at("cels");
        if (!cels.is_array()) return false;
        for (const auto& cel_value : cels) {
            Cel cel;
            cel.x = cel_value.value("x", 0);
            cel.y = cel_value.value("y", 0);
            if (!pixels_from_binary(cel_value.at("pixels"), cel.pixels)) return false;
            frame.cels.push_back(std::move(cel));
        }
        frames.push_back(std::move(frame));
    }
    return true;
}

nlohmann::json tags_to_recovery_json(const std::vector<AnimationTag>& tags) {
    nlohmann::json result = nlohmann::json::array();
    for (const AnimationTag& tag : tags) {
        result.push_back({{"name", tag.name}, {"from", tag.from}, {"to", tag.to}});
    }
    return result;
}

bool tags_from_recovery_json(const nlohmann::json& value, std::vector<AnimationTag>& tags) {
    if (!value.is_array()) return false;
    tags.clear();
    for (const auto& item : value) {
        tags.push_back({item.value("name", "Tag"), item.value("from", 0), item.value("to", 0)});
    }
    return true;
}

nlohmann::json floating_to_recovery_json(const FloatingSelection& floating) {
    return {{"active", floating.active}, {"source_x", floating.source_x},
            {"source_y", floating.source_y}, {"offset_x", floating.offset_x},
            {"offset_y", floating.offset_y}, {"width", floating.width},
            {"height", floating.height}, {"pixels", pixels_to_binary(floating.pixels)},
            {"mask", bytes_to_binary(floating.mask)}};
}

bool floating_from_recovery_json(const nlohmann::json& value, FloatingSelection& floating) {
    floating.active = value.value("active", false);
    floating.source_x = value.value("source_x", 0);
    floating.source_y = value.value("source_y", 0);
    floating.offset_x = value.value("offset_x", 0);
    floating.offset_y = value.value("offset_y", 0);
    floating.width = value.value("width", 0);
    floating.height = value.value("height", 0);
    return pixels_from_binary(value.at("pixels"), floating.pixels) &&
           bytes_from_binary(value.at("mask"), floating.mask);
}

bool history_payload_to_recovery_json(const Document& document,
                                      const HistoryPixelPayload& payload,
                                      nlohmann::json& value,
                                      std::string* error) {
    if (payload.empty()) {
        value = nullptr;
        return true;
    }
    std::vector<Pixel> pixels;
    if (!document.materialize_history_payload(payload, pixels)) {
        set_error(error, "Could not read a disk-backed undo history payload");
        return false;
    }
    value = pixels_to_binary(pixels);
    return true;
}

bool history_payload_from_recovery_json(const nlohmann::json& value,
                                        HistoryPixelPayload& payload) {
    payload = {};
    if (value.is_null()) return true;
    if (!pixels_from_binary(value, payload.pixels)) return false;
    payload.pixel_count = static_cast<std::uint64_t>(payload.pixels.size());
    return true;
}

bool command_to_recovery_json(const Document& document, const UndoCommand& command,
                              nlohmann::json& value, std::string* error) {
    value = {{"name", command.name}, {"frame", command.frame}, {"layer", command.layer},
             {"before_active_layer", command.before_active_layer},
             {"before_active_frame", command.before_active_frame},
             {"after_active_layer", command.after_active_layer},
             {"after_active_frame", command.after_active_frame}};
    nlohmann::json diffs = nlohmann::json::array();
    for (const TileDiff& diff : command.pixel_diffs) {
        diffs.push_back({{"frame", diff.frame}, {"layer", diff.layer}, {"x", diff.x},
                         {"y", diff.y}, {"w", diff.w}, {"h", diff.h},
                         {"before", pixels_to_binary(diff.before)},
                         {"after", diff.after.empty() ? nlohmann::json(nullptr)
                                                       : pixels_to_binary(diff.after)}});
    }
    value["pixel_diffs"] = std::move(diffs);
    if (command.dense_pixel_diff) {
        nlohmann::json before;
        nlohmann::json after;
        if (!history_payload_to_recovery_json(document, command.dense_pixel_diff->before,
                                              before, error) ||
            !history_payload_to_recovery_json(document, command.dense_pixel_diff->after,
                                              after, error)) {
            return false;
        }
        value["dense_pixel_diff"] = {{"frame", command.dense_pixel_diff->frame},
                                      {"layer", command.dense_pixel_diff->layer},
                                      {"width", command.dense_pixel_diff->width},
                                      {"height", command.dense_pixel_diff->height},
                                      {"before", std::move(before)},
                                      {"after", std::move(after)}};
    } else {
        value["dense_pixel_diff"] = nullptr;
    }
    value["before_selection"] = command.before_selection
                                      ? selection_to_recovery_json(*command.before_selection)
                                      : nlohmann::json(nullptr);
    value["after_selection"] = command.after_selection
                                     ? selection_to_recovery_json(*command.after_selection)
                                     : nlohmann::json(nullptr);
    value["before_floating_selection"] = command.before_floating_selection
        ? floating_to_recovery_json(*command.before_floating_selection) : nlohmann::json(nullptr);
    value["after_floating_selection"] = command.after_floating_selection
        ? floating_to_recovery_json(*command.after_floating_selection) : nlohmann::json(nullptr);
    value["before_palette"] = command.before_palette
                                    ? palette_to_recovery_json(*command.before_palette)
                                    : nlohmann::json(nullptr);
    value["after_palette"] = command.after_palette
                                   ? palette_to_recovery_json(*command.after_palette)
                                   : nlohmann::json(nullptr);
    value["before_layers"] = command.before_layers
                                   ? layers_to_recovery_json(*command.before_layers)
                                   : nlohmann::json(nullptr);
    value["after_layers"] = command.after_layers
                                  ? layers_to_recovery_json(*command.after_layers)
                                  : nlohmann::json(nullptr);
    value["before_frames"] = command.before_frames
                                   ? frames_to_recovery_json(*command.before_frames)
                                   : nlohmann::json(nullptr);
    value["after_frames"] = command.after_frames
                                  ? frames_to_recovery_json(*command.after_frames)
                                  : nlohmann::json(nullptr);
    value["before_tags"] = command.before_tags ? tags_to_recovery_json(*command.before_tags)
                                                : nlohmann::json(nullptr);
    value["after_tags"] = command.after_tags ? tags_to_recovery_json(*command.after_tags)
                                              : nlohmann::json(nullptr);
    value["before_frame_duration_ms"] = command.before_frame_duration_ms
                                             ? nlohmann::json(*command.before_frame_duration_ms)
                                             : nlohmann::json(nullptr);
    value["after_frame_duration_ms"] = command.after_frame_duration_ms
                                            ? nlohmann::json(*command.after_frame_duration_ms)
                                            : nlohmann::json(nullptr);
    value["before_width"] = command.before_width ? nlohmann::json(*command.before_width)
                                                   : nlohmann::json(nullptr);
    value["before_height"] = command.before_height ? nlohmann::json(*command.before_height)
                                                     : nlohmann::json(nullptr);
    value["after_width"] = command.after_width ? nlohmann::json(*command.after_width)
                                                 : nlohmann::json(nullptr);
    value["after_height"] = command.after_height ? nlohmann::json(*command.after_height)
                                                   : nlohmann::json(nullptr);
    value["before_model"] = command.before_model
        ? nlohmann::json::parse(model_to_json(*command.before_model)) : nlohmann::json(nullptr);
    value["after_model"] = command.after_model
        ? nlohmann::json::parse(model_to_json(*command.after_model)) : nlohmann::json(nullptr);
    return true;
}

bool command_from_recovery_json(const nlohmann::json& value, UndoCommand& command) {
    command = {};
    command.name = value.value("name", "Recovered edit");
    command.frame = value.value("frame", 0);
    command.layer = value.value("layer", 0);
    command.before_active_layer = value.value("before_active_layer", 0);
    command.before_active_frame = value.value("before_active_frame", 0);
    command.after_active_layer = value.value("after_active_layer", 0);
    command.after_active_frame = value.value("after_active_frame", 0);
    for (const auto& diff_value : value.at("pixel_diffs")) {
        TileDiff diff;
        diff.frame = diff_value.value("frame", 0);
        diff.layer = diff_value.value("layer", 0);
        diff.x = diff_value.value("x", 0);
        diff.y = diff_value.value("y", 0);
        diff.w = diff_value.value("w", 0);
        diff.h = diff_value.value("h", 0);
        if (!pixels_from_binary(diff_value.at("before"), diff.before) ||
            !pixels_from_binary(diff_value.at("after"), diff.after)) return false;
        command.pixel_diffs.push_back(std::move(diff));
    }
    if (!value.at("dense_pixel_diff").is_null()) {
        const auto& dense_value = value.at("dense_pixel_diff");
        DensePixelDiff dense;
        dense.frame = dense_value.value("frame", 0);
        dense.layer = dense_value.value("layer", 0);
        dense.width = dense_value.value("width", 0);
        dense.height = dense_value.value("height", 0);
        if (!history_payload_from_recovery_json(dense_value.at("before"), dense.before) ||
            !history_payload_from_recovery_json(dense_value.at("after"), dense.after)) return false;
        command.dense_pixel_diff = std::move(dense);
    }
    const auto decode_selection = [&value](const char* key, std::optional<SelectionMask>& target) {
        if (!value.contains(key) || value.at(key).is_null()) return true;
        SelectionMask selection;
        if (!selection_from_recovery_json(value.at(key), selection)) return false;
        target = std::move(selection);
        return true;
    };
    const auto decode_floating = [&value](const char* key,
                                          std::optional<FloatingSelection>& target) {
        if (!value.contains(key) || value.at(key).is_null()) return true;
        FloatingSelection floating;
        if (!floating_from_recovery_json(value.at(key), floating)) return false;
        target = std::move(floating);
        return true;
    };
    const auto decode_palette = [&value](const char* key, std::optional<Palette>& target) {
        if (value.at(key).is_null()) return true;
        Palette palette;
        if (!palette_from_recovery_json(value.at(key), palette)) return false;
        target = std::move(palette);
        return true;
    };
    const auto decode_layers = [&value](const char* key, std::optional<std::vector<Layer>>& target) {
        if (value.at(key).is_null()) return true;
        std::vector<Layer> layers;
        if (!layers_from_recovery_json(value.at(key), layers)) return false;
        target = std::move(layers);
        return true;
    };
    const auto decode_frames = [&value](const char* key, std::optional<std::vector<Frame>>& target) {
        if (value.at(key).is_null()) return true;
        std::vector<Frame> frames;
        if (!frames_from_recovery_json(value.at(key), frames)) return false;
        target = std::move(frames);
        return true;
    };
    const auto decode_tags = [&value](const char* key, std::optional<std::vector<AnimationTag>>& target) {
        if (value.at(key).is_null()) return true;
        std::vector<AnimationTag> tags;
        if (!tags_from_recovery_json(value.at(key), tags)) return false;
        target = std::move(tags);
        return true;
    };
    if (!decode_selection("before_selection", command.before_selection) ||
        !decode_selection("after_selection", command.after_selection) ||
        !decode_floating("before_floating_selection", command.before_floating_selection) ||
        !decode_floating("after_floating_selection", command.after_floating_selection) ||
        !decode_palette("before_palette", command.before_palette) ||
        !decode_palette("after_palette", command.after_palette) ||
        !decode_layers("before_layers", command.before_layers) ||
        !decode_layers("after_layers", command.after_layers) ||
        !decode_frames("before_frames", command.before_frames) ||
        !decode_frames("after_frames", command.after_frames) ||
        !decode_tags("before_tags", command.before_tags) ||
        !decode_tags("after_tags", command.after_tags)) return false;
    if (!value.at("before_frame_duration_ms").is_null()) {
        command.before_frame_duration_ms = value.at("before_frame_duration_ms").get<int>();
    }
    if (!value.at("after_frame_duration_ms").is_null()) {
        command.after_frame_duration_ms = value.at("after_frame_duration_ms").get<int>();
    }
    const auto decode_int = [&value](const char* key, std::optional<int>& target) {
        if (value.contains(key) && !value.at(key).is_null()) target = value.at(key).get<int>();
    };
    decode_int("before_width", command.before_width);
    decode_int("before_height", command.before_height);
    decode_int("after_width", command.after_width);
    decode_int("after_height", command.after_height);
    const auto decode_model = [&value](const char* key, std::optional<ModelDocument>& target) {
        if (!value.contains(key) || value.at(key).is_null()) return true;
        ModelDocument model;
        if (!model_from_json(value.at(key).dump(), model)) return false;
        target = std::move(model);
        return true;
    };
    if (!decode_model("before_model", command.before_model) ||
        !decode_model("after_model", command.after_model)) return false;
    return true;
}

bool serialize_recovery_history(const Document& document, std::vector<unsigned char>& bytes,
                                std::string* error) {
    try {
        nlohmann::json root = {{"format", "pixelart98-recovery-history"}, {"version", 1},
                               {"selection", selection_to_recovery_json(document.selection)},
                               {"floating_selection", floating_to_recovery_json(document.floating_selection)},
                               {"palette_active", document.palette.active}};
        for (const auto& [key, history] :
             {std::pair{"undo", &document.undo_history_for_recovery()},
              std::pair{"redo", &document.redo_history_for_recovery()}}) {
            nlohmann::json commands = nlohmann::json::array();
            for (const UndoCommand& command : *history) {
                nlohmann::json command_value;
                if (!command_to_recovery_json(document, command, command_value, error)) return false;
                commands.push_back(std::move(command_value));
            }
            root[key] = std::move(commands);
        }
        bytes = nlohmann::json::to_cbor(root);
        return true;
    } catch (const std::exception& exception) {
        set_error(error, std::string("Could not serialize recovery history: ") + exception.what());
        return false;
    }
}

bool deserialize_recovery_history(const unsigned char* bytes, std::size_t size,
                                  Document& document, std::string* error) {
    try {
        const nlohmann::json root = nlohmann::json::from_cbor(bytes, bytes + size);
        if (root.value("format", std::string()) != "pixelart98-recovery-history" ||
            root.value("version", 0) != 1) {
            set_error(error, "Unsupported recovery history format");
            return false;
        }
        SelectionMask selection;
        FloatingSelection floating;
        if (!selection_from_recovery_json(root.at("selection"), selection) ||
            !floating_from_recovery_json(root.at("floating_selection"), floating)) {
            set_error(error, "Invalid recovery selection state");
            return false;
        }
        std::deque<UndoCommand> undo;
        std::deque<UndoCommand> redo;
        const auto decode_history = [&root](const char* key, std::deque<UndoCommand>& history) {
            const auto& values = root.at(key);
            if (!values.is_array() || values.size() > 128U) return false;
            for (const auto& command_value : values) {
                UndoCommand command;
                if (!command_from_recovery_json(command_value, command)) return false;
                history.push_back(std::move(command));
            }
            return true;
        };
        if (!decode_history("undo", undo) || !decode_history("redo", redo)) {
            set_error(error, "Invalid recovery undo history");
            return false;
        }
        document.selection = std::move(selection);
        document.floating_selection = std::move(floating);
        document.palette.active = root.value("palette_active", 0);
        document.restore_history_for_recovery(std::move(undo), std::move(redo));
        return true;
    } catch (const std::exception& exception) {
        set_error(error, std::string("Could not read recovery history: ") + exception.what());
        return false;
    }
}
