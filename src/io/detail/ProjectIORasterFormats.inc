// Internal implementation fragment for ProjectIO raster and animation formats.
// This file is included by its owning translation unit; do not compile it separately.

bool import_image_as_layer(const std::string& path, Document& document, const std::string& layer_name, std::string* error) {
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!pixels) {
        set_error(error, "Could not import image layer: " + path);
        return false;
    }
    auto before_layers = document.layers;
    auto before_frames = document.frames;
    auto before_tags = document.tags;
    int before_layer = document.active_layer;
    int before_frame = document.active_frame;
    Layer layer;
    layer.name = layer_name.empty() ? std::filesystem::path(path).stem().string() : layer_name;
    document.layers.push_back(std::move(layer));
    for (auto& frame : document.frames) {
        Cel cel;
        cel.pixels.assign(static_cast<std::size_t>(document.width * document.height), 0);
        frame.cels.push_back(std::move(cel));
    }
    document.active_layer = static_cast<int>(document.layers.size()) - 1;
    Cel& cel = document.active_cel();
    int copy_w = std::min(width, document.width);
    int copy_h = std::min(height, document.height);
    for (int y = 0; y < copy_h; ++y) {
        const Pixel* src = reinterpret_cast<const Pixel*>(pixels) + static_cast<std::size_t>(y * width);
        Pixel* dst = cel.pixels.data() + static_cast<std::size_t>(y * document.width);
        std::copy(src, src + copy_w, dst);
    }
    stbi_image_free(pixels);
    document.commit_structure_edit("Import Image Layer", std::move(before_layers), std::move(before_frames), std::move(before_tags), before_layer, before_frame);
    return true;
}

bool export_png(const std::string& path, const Document& document, int frame, std::string* error) {
    auto pixels = document.composite_frame(frame);
    if (!stbi_write_png(path.c_str(), document.width, document.height, 4, pixels.data(), document.width * 4)) {
        set_error(error, "Could not write PNG: " + path);
        return false;
    }
    return true;
}

bool export_spritesheet(const std::string& png_path, const std::string& json_path, const Document& document, std::string* error) {
    int sheet_w = document.width * static_cast<int>(document.frames.size());
    int sheet_h = document.height;
    std::vector<Pixel> sheet(static_cast<std::size_t>(sheet_w * sheet_h), 0);
    for (std::size_t fi = 0; fi < document.frames.size(); ++fi) {
        auto frame = document.composite_frame(static_cast<int>(fi));
        for (int y = 0; y < document.height; ++y) {
            for (int x = 0; x < document.width; ++x) {
                sheet[static_cast<std::size_t>(y * sheet_w + x + static_cast<int>(fi) * document.width)] =
                    frame[static_cast<std::size_t>(y * document.width + x)];
            }
        }
    }
    if (!stbi_write_png(png_path.c_str(), sheet_w, sheet_h, 4, sheet.data(), sheet_w * 4)) {
        set_error(error, "Could not write spritesheet PNG: " + png_path);
        return false;
    }

    nlohmann::json meta;
    meta["frame_width"] = document.width;
    meta["frame_height"] = document.height;
    meta["frames"] = nlohmann::json::array();
    for (std::size_t i = 0; i < document.frames.size(); ++i) {
        meta["frames"].push_back({
            {"x", static_cast<int>(i) * document.width},
            {"y", 0},
            {"w", document.width},
            {"h", document.height},
            {"duration_ms", document.frames[i].duration_ms}
        });
    }
    std::ofstream json_file(json_path);
    if (!json_file) {
        set_error(error, "Could not write spritesheet JSON: " + json_path);
        return false;
    }
    json_file << meta.dump(2);
    return true;
}

bool export_gif(const std::string& path, const Document& document, std::string* error) {
    auto palette_pixels = gif_palette(document);
    std::vector<std::uint8_t> raw_palette(256 * 3, 0);
    for (int i = 0; i < 256; ++i) {
        raw_palette[static_cast<std::size_t>(i * 3 + 0)] = r(palette_pixels[static_cast<std::size_t>(i)]);
        raw_palette[static_cast<std::size_t>(i * 3 + 1)] = g(palette_pixels[static_cast<std::size_t>(i)]);
        raw_palette[static_cast<std::size_t>(i * 3 + 2)] = b(palette_pixels[static_cast<std::size_t>(i)]);
    }

    ge_GIF* gif = ge_new_gif(path.c_str(), static_cast<std::uint16_t>(document.width),
                             static_cast<std::uint16_t>(document.height), raw_palette.data(), 8, -1, 0);
    if (!gif) {
        set_error(error, "Could not create GIF: " + path);
        return false;
    }
    for (std::size_t fi = 0; fi < document.frames.size(); ++fi) {
        auto frame = document.composite_frame(static_cast<int>(fi));
        for (std::size_t i = 0; i < frame.size(); ++i) {
            gif->frame[i] = static_cast<std::uint8_t>(nearest_palette_index(frame[i], palette_pixels));
        }
        ge_add_frame(gif, static_cast<std::uint16_t>(std::max(2, document.frames[fi].duration_ms / 10)));
    }
    ge_close_gif(gif);
    return true;
}

bool export_apng(const std::string& path, const Document& document, std::string* error) {
    if (document.frames.empty()) {
        set_error(error, "Document has no frames");
        return false;
    }
    std::vector<unsigned char> png = {137, 80, 78, 71, 13, 10, 26, 10};

    std::vector<unsigned char> ihdr;
    write_be32(ihdr, static_cast<std::uint32_t>(document.width));
    write_be32(ihdr, static_cast<std::uint32_t>(document.height));
    ihdr.push_back(8);
    ihdr.push_back(6);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    write_chunk(png, "IHDR", ihdr);

    std::vector<unsigned char> actl;
    write_be32(actl, static_cast<std::uint32_t>(document.frames.size()));
    write_be32(actl, 0);
    write_chunk(png, "acTL", actl);

    std::uint32_t sequence = 0;
    for (std::size_t fi = 0; fi < document.frames.size(); ++fi) {
        int delay_ms = std::max(1, document.frames[fi].duration_ms);
        std::vector<unsigned char> fctl;
        write_be32(fctl, sequence++);
        write_be32(fctl, static_cast<std::uint32_t>(document.width));
        write_be32(fctl, static_cast<std::uint32_t>(document.height));
        write_be32(fctl, 0);
        write_be32(fctl, 0);
        write_be16(fctl, static_cast<std::uint16_t>(std::min(delay_ms, 65535)));
        write_be16(fctl, 1000);
        fctl.push_back(0);
        fctl.push_back(0);
        write_chunk(png, "fcTL", fctl);

        auto frame = document.composite_frame(static_cast<int>(fi));
        auto raw = filtered_rgba_scanlines(document.width, document.height, frame);
        std::vector<unsigned char> compressed;
        if (!compress_png_data(raw, compressed)) {
            set_error(error, "Could not compress APNG frame");
            return false;
        }
        if (fi == 0) {
            write_chunk(png, "IDAT", compressed);
        } else {
            std::vector<unsigned char> fdat;
            write_be32(fdat, sequence++);
            fdat.insert(fdat.end(), compressed.begin(), compressed.end());
            write_chunk(png, "fdAT", fdat);
        }
    }
    write_chunk(png, "IEND", {});

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        set_error(error, "Could not write APNG: " + path);
        return false;
    }
    file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return true;
}

bool export_aseprite(const std::string& path, const Document& document, std::string* error) {
    if (document.width <= 0 || document.height <= 0 || document.layers.empty() || document.frames.empty()) {
        set_error(error, "Cannot export empty document as Aseprite");
        return false;
    }

    std::vector<unsigned char> file;
    write_le32(file, 0);
    write_le16(file, 0xA5E0);
    write_le16(file, static_cast<std::uint16_t>(std::min<std::size_t>(document.frames.size(), 65535)));
    write_le16(file, static_cast<std::uint16_t>(std::min(document.width, 65535)));
    write_le16(file, static_cast<std::uint16_t>(std::min(document.height, 65535)));
    write_le16(file, 32);
    write_le32(file, 1);
    write_le16(file, 100);
    write_le32(file, 0);
    write_le32(file, 0);
    write_u8(file, 0);
    write_u8(file, 0);
    write_u8(file, 0);
    write_u8(file, 0);
    write_le16(file, 0);
    write_u8(file, 1);
    write_u8(file, 1);
    write_i16(file, 0);
    write_i16(file, 0);
    write_le16(file, static_cast<std::uint16_t>(std::min(document.width, 65535)));
    write_le16(file, static_cast<std::uint16_t>(std::min(document.height, 65535)));
    while (file.size() < 128) {
        file.push_back(0);
    }

    for (std::size_t fi = 0; fi < document.frames.size(); ++fi) {
        std::vector<std::vector<unsigned char>> chunks;
        if (fi == 0) {
            for (const auto& layer : document.layers) {
                std::vector<unsigned char> payload;
                write_le16(payload, layer.visible ? 1 : 0);
                write_le16(payload, 0);
                write_le16(payload, 0);
                write_le16(payload, static_cast<std::uint16_t>(std::min(document.width, 65535)));
                write_le16(payload, static_cast<std::uint16_t>(std::min(document.height, 65535)));
                write_le16(payload, 0);
                write_u8(payload, static_cast<std::uint8_t>(std::clamp(layer.opacity, 0.0f, 1.0f) * 255.0f + 0.5f));
                write_u8(payload, 0);
                write_u8(payload, 0);
                write_u8(payload, 0);
                write_ase_string(payload, layer.name);
                chunks.push_back(chunk_with_header(0x2004, payload));
            }

            std::vector<unsigned char> palette_payload;
            std::size_t count = std::clamp<std::size_t>(document.palette.colors.size(), 1, 256);
            write_le32(palette_payload, static_cast<std::uint32_t>(count));
            write_le32(palette_payload, 0);
            write_le32(palette_payload, static_cast<std::uint32_t>(count - 1));
            for (int i = 0; i < 8; ++i) write_u8(palette_payload, 0);
            for (std::size_t i = 0; i < count; ++i) {
                Pixel p = i < document.palette.colors.size() ? document.palette.colors[i] : rgba(0, 0, 0, 0);
                write_le16(palette_payload, 0);
                write_u8(palette_payload, r(p));
                write_u8(palette_payload, g(p));
                write_u8(palette_payload, b(p));
                write_u8(palette_payload, a(p));
            }
            chunks.push_back(chunk_with_header(0x2019, palette_payload));

            if (!document.tags.empty()) {
                std::vector<unsigned char> tag_payload;
                write_le16(tag_payload, static_cast<std::uint16_t>(std::min<std::size_t>(document.tags.size(), 65535)));
                for (int i = 0; i < 8; ++i) write_u8(tag_payload, 0);
                for (const auto& tag : document.tags) {
                    write_le16(tag_payload, static_cast<std::uint16_t>(std::clamp(tag.from, 0, static_cast<int>(document.frames.size()) - 1)));
                    write_le16(tag_payload, static_cast<std::uint16_t>(std::clamp(tag.to, 0, static_cast<int>(document.frames.size()) - 1)));
                    write_u8(tag_payload, document.playback_mode == PlaybackMode::PingPong ? 2 : 0);
                    for (int i = 0; i < 8; ++i) write_u8(tag_payload, 0);
                    write_u8(tag_payload, 0);
                    write_u8(tag_payload, 0);
                    write_u8(tag_payload, 0);
                    write_u8(tag_payload, 0);
                    write_ase_string(tag_payload, tag.name);
                }
                chunks.push_back(chunk_with_header(0x2018, tag_payload));
            }
        }

        for (std::size_t li = 0; li < document.layers.size(); ++li) {
            if (li >= document.frames[fi].cels.size()) {
                continue;
            }
            const Cel& cel = document.frames[fi].cels[li];
            std::vector<unsigned char> raw(reinterpret_cast<const unsigned char*>(cel.pixels.data()),
                                           reinterpret_cast<const unsigned char*>(cel.pixels.data()) + cel.pixels.size() * 4);
            std::vector<unsigned char> compressed;
            if (!compress_zlib(raw, compressed)) {
                set_error(error, "Could not compress Aseprite cel");
                return false;
            }
            std::vector<unsigned char> payload;
            write_le16(payload, static_cast<std::uint16_t>(li));
            write_i16(payload, static_cast<std::int16_t>(std::clamp(cel.x, -32768, 32767)));
            write_i16(payload, static_cast<std::int16_t>(std::clamp(cel.y, -32768, 32767)));
            write_u8(payload, 255);
            write_le16(payload, 2);
            for (int i = 0; i < 7; ++i) write_u8(payload, 0);
            write_le16(payload, static_cast<std::uint16_t>(std::min(document.width, 65535)));
            write_le16(payload, static_cast<std::uint16_t>(std::min(document.height, 65535)));
            payload.insert(payload.end(), compressed.begin(), compressed.end());
            chunks.push_back(chunk_with_header(0x2005, payload));
        }

        std::vector<unsigned char> frame;
        write_le32(frame, 0);
        write_le16(frame, 0xF1FA);
        write_le16(frame, static_cast<std::uint16_t>(std::min<std::size_t>(chunks.size(), 65535)));
        write_le16(frame, static_cast<std::uint16_t>(std::clamp(document.frames[fi].duration_ms, 1, 65535)));
        write_le16(frame, 0);
        write_le32(frame, static_cast<std::uint32_t>(chunks.size()));
        for (const auto& chunk : chunks) {
            frame.insert(frame.end(), chunk.begin(), chunk.end());
        }
        std::uint32_t frame_size = static_cast<std::uint32_t>(frame.size());
        frame[0] = static_cast<unsigned char>(frame_size & 0xff);
        frame[1] = static_cast<unsigned char>((frame_size >> 8) & 0xff);
        frame[2] = static_cast<unsigned char>((frame_size >> 16) & 0xff);
        frame[3] = static_cast<unsigned char>((frame_size >> 24) & 0xff);
        file.insert(file.end(), frame.begin(), frame.end());
    }

    std::uint32_t file_size = static_cast<std::uint32_t>(file.size());
    file[0] = static_cast<unsigned char>(file_size & 0xff);
    file[1] = static_cast<unsigned char>((file_size >> 8) & 0xff);
    file[2] = static_cast<unsigned char>((file_size >> 16) & 0xff);
    file[3] = static_cast<unsigned char>((file_size >> 24) & 0xff);

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        set_error(error, "Could not write Aseprite file: " + path);
        return false;
    }
    out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    return true;
}

bool import_aseprite(const std::string& path, Document& out_document, std::string* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        set_error(error, "Could not open Aseprite file: " + path);
        return false;
    }
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::size_t offset = 0;
    std::uint32_t file_size = 0;
    std::uint16_t magic = 0;
    std::uint16_t frame_count = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint16_t color_depth = 0;
    if (!read_le32(bytes, offset, file_size) ||
        !read_le16(bytes, offset, magic) ||
        !read_le16(bytes, offset, frame_count) ||
        !read_le16(bytes, offset, width) ||
        !read_le16(bytes, offset, height) ||
        !read_le16(bytes, offset, color_depth) ||
        magic != 0xA5E0 || color_depth != 32 || width == 0 || height == 0) {
        set_error(error, "Unsupported Aseprite file header");
        return false;
    }
    offset = 128;

    struct ImportedCel {
        int frame = 0;
        int layer = 0;
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        std::vector<Pixel> pixels;
    };

    std::vector<Layer> layers;
    const auto duration_count = static_cast<std::size_t>(frame_count == 0 ? 1U : frame_count);
    std::vector<int> durations(duration_count, 100);
    std::vector<ImportedCel> imported_cels;
    Palette palette;
    palette.colors = default_palette();
    std::vector<AnimationTag> tags;
    PlaybackMode playback_mode = PlaybackMode::Loop;

    for (int fi = 0; fi < frame_count && offset < bytes.size(); ++fi) {
        std::size_t frame_start = offset;
        std::uint32_t frame_bytes = 0;
        std::uint16_t frame_magic = 0;
        std::uint16_t old_chunks = 0;
        std::uint16_t duration = 0;
        std::uint16_t reserved = 0;
        std::uint32_t new_chunks = 0;
        if (!read_le32(bytes, offset, frame_bytes) ||
            !read_le16(bytes, offset, frame_magic) ||
            !read_le16(bytes, offset, old_chunks) ||
            !read_le16(bytes, offset, duration) ||
            !read_le16(bytes, offset, reserved) ||
            !read_le32(bytes, offset, new_chunks) ||
            frame_magic != 0xF1FA || frame_bytes < 16) {
            set_error(error, "Invalid Aseprite frame");
            return false;
        }
        durations[static_cast<std::size_t>(fi)] = std::max<int>(1, duration);
        std::uint32_t chunk_count = old_chunks == 0xFFFF ? new_chunks : old_chunks;
        std::size_t frame_end = std::min<std::size_t>(bytes.size(), frame_start + frame_bytes);
        for (std::uint32_t ci = 0; ci < chunk_count && offset + 6 <= frame_end; ++ci) {
            std::size_t chunk_start = offset;
            std::uint32_t chunk_size = 0;
            std::uint16_t chunk_type = 0;
            if (!read_le32(bytes, offset, chunk_size) || !read_le16(bytes, offset, chunk_type) || chunk_size < 6) {
                set_error(error, "Invalid Aseprite chunk");
                return false;
            }
            std::size_t chunk_end = std::min<std::size_t>(frame_end, chunk_start + chunk_size);
            if (chunk_type == 0x2004) {
                std::uint16_t flags = 0;
                std::uint16_t layer_type = 0;
                std::uint16_t child_level = 0;
                std::uint16_t default_w = 0;
                std::uint16_t default_h = 0;
                std::uint16_t blend = 0;
                std::uint8_t opacity = 255;
                std::string name;
                if (read_le16(bytes, offset, flags) &&
                    read_le16(bytes, offset, layer_type) &&
                    read_le16(bytes, offset, child_level) &&
                    read_le16(bytes, offset, default_w) &&
                    read_le16(bytes, offset, default_h) &&
                    read_le16(bytes, offset, blend) &&
                    read_u8(bytes, offset, opacity) &&
                    skip_bytes(bytes, offset, 3) &&
                    read_ase_string(bytes, offset, name)) {
                    Layer layer;
                    layer.name = name.empty() ? "Layer" : name;
                    layer.visible = (flags & 1) != 0;
                    layer.opacity = static_cast<float>(opacity) / 255.0f;
                    layer.blend_mode = LayerBlendMode::Normal;
                    layers.push_back(std::move(layer));
                }
            } else if (chunk_type == 0x2005) {
                std::uint16_t layer_index = 0;
                std::int16_t cel_x = 0;
                std::int16_t cel_y = 0;
                std::uint8_t opacity = 0;
                std::uint16_t cel_type = 0;
                std::uint16_t cel_w = 0;
                std::uint16_t cel_h = 0;
                if (read_le16(bytes, offset, layer_index) &&
                    read_i16(bytes, offset, cel_x) &&
                    read_i16(bytes, offset, cel_y) &&
                    read_u8(bytes, offset, opacity) &&
                    read_le16(bytes, offset, cel_type) &&
                    skip_bytes(bytes, offset, 7)) {
                    (void)opacity;
                    if ((cel_type == 0 || cel_type == 2) &&
                        read_le16(bytes, offset, cel_w) &&
                        read_le16(bytes, offset, cel_h) &&
                        cel_w > 0 && cel_h > 0) {
                        std::vector<unsigned char> raw;
                        std::size_t expected = static_cast<std::size_t>(cel_w) * cel_h * 4;
                        bool ok = false;
                        if (cel_type == 0 && offset + expected <= chunk_end) {
                            raw.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                                       bytes.begin() + static_cast<std::ptrdiff_t>(offset + expected));
                            ok = true;
                        } else if (cel_type == 2 && offset < chunk_end) {
                            ok = uncompress_zlib(bytes.data() + offset, chunk_end - offset, expected, raw);
                        }
                        if (ok) {
                            ImportedCel cel;
                            cel.frame = fi;
                            cel.layer = static_cast<int>(layer_index);
                            cel.x = cel_x;
                            cel.y = cel_y;
                            cel.width = cel_w;
                            cel.height = cel_h;
                            cel.pixels.assign(reinterpret_cast<Pixel*>(raw.data()),
                                              reinterpret_cast<Pixel*>(raw.data() + raw.size()));
                            imported_cels.push_back(std::move(cel));
                        }
                    }
                }
            } else if (chunk_type == 0x2019) {
                std::uint32_t palette_size = 0;
                std::uint32_t first = 0;
                std::uint32_t last = 0;
                if (read_le32(bytes, offset, palette_size) &&
                    read_le32(bytes, offset, first) &&
                    read_le32(bytes, offset, last) &&
                    skip_bytes(bytes, offset, 8) &&
                    palette_size > 0) {
                    palette.colors.assign(static_cast<std::size_t>(palette_size), rgba(0, 0, 0, 0));
                    for (std::uint32_t pi = first; pi <= last && pi < palette_size && offset + 6 <= chunk_end; ++pi) {
                        std::uint16_t flags = 0;
                        std::uint8_t rr = 0, gg = 0, bb = 0, aa = 0;
                        if (!read_le16(bytes, offset, flags) ||
                            !read_u8(bytes, offset, rr) ||
                            !read_u8(bytes, offset, gg) ||
                            !read_u8(bytes, offset, bb) ||
                            !read_u8(bytes, offset, aa)) {
                            break;
                        }
                        palette.colors[static_cast<std::size_t>(pi)] = rgba(rr, gg, bb, aa);
                        if ((flags & 1) != 0) {
                            std::string ignored;
                            if (!read_ase_string(bytes, offset, ignored)) break;
                        }
                    }
                }
            } else if (chunk_type == 0x2018) {
                std::uint16_t tag_count = 0;
                if (read_le16(bytes, offset, tag_count) && skip_bytes(bytes, offset, 8)) {
                    for (std::uint16_t ti = 0; ti < tag_count && offset + 16 <= chunk_end; ++ti) {
                        std::uint16_t from = 0, to = 0;
                        std::uint8_t direction = 0;
                        std::string name;
                        if (!read_le16(bytes, offset, from) ||
                            !read_le16(bytes, offset, to) ||
                            !read_u8(bytes, offset, direction) ||
                            !skip_bytes(bytes, offset, 12) ||
                            !read_ase_string(bytes, offset, name)) {
                            break;
                        }
                        AnimationTag tag;
                        tag.name = name.empty() ? "Tag" : name;
                        tag.from = from;
                        tag.to = to;
                        tags.push_back(std::move(tag));
                        if (direction == 2) playback_mode = PlaybackMode::PingPong;
                    }
                }
            }
            offset = chunk_end;
        }
        offset = frame_end;
    }

    if (layers.empty()) {
        Layer layer;
        layer.name = "Layer 1";
        layers.push_back(std::move(layer));
    }
    Document document = Document::create(width, height);
    document.layers = std::move(layers);
    document.frames.clear();
    for (int fi = 0; fi < std::max<int>(1, frame_count); ++fi) {
        Frame frame;
        frame.duration_ms = durations[static_cast<std::size_t>(fi)];
        for (std::size_t li = 0; li < document.layers.size(); ++li) {
            Cel cel;
            cel.pixels.assign(static_cast<std::size_t>(document.width * document.height), 0);
            frame.cels.push_back(std::move(cel));
        }
        document.frames.push_back(std::move(frame));
    }
    for (const auto& imported : imported_cels) {
        if (imported.frame < 0 || imported.frame >= static_cast<int>(document.frames.size()) ||
            imported.layer < 0 || imported.layer >= static_cast<int>(document.layers.size())) {
            continue;
        }
        Cel& dst = document.frames[static_cast<std::size_t>(imported.frame)].cels[static_cast<std::size_t>(imported.layer)];
        for (int y = 0; y < imported.height; ++y) {
            for (int x = 0; x < imported.width; ++x) {
                int dx = imported.x + x;
                int dy = imported.y + y;
                if (dx < 0 || dy < 0 || dx >= document.width || dy >= document.height) {
                    continue;
                }
                std::size_t src_i = static_cast<std::size_t>(y * imported.width + x);
                std::size_t dst_i = static_cast<std::size_t>(document.pixel_index(dx, dy));
                if (src_i < imported.pixels.size()) {
                    dst.pixels[dst_i] = imported.pixels[src_i];
                }
            }
        }
    }
    if (!palette.colors.empty()) {
        document.palette = std::move(palette);
    }
    int last = std::max(0, static_cast<int>(document.frames.size()) - 1);
    for (auto& tag : tags) {
        tag.from = std::clamp(tag.from, 0, last);
        tag.to = std::clamp(tag.to, tag.from, last);
    }
    document.tags = std::move(tags);
    document.playback_mode = playback_mode;
    document.active_frame = 0;
    document.active_layer = 0;
    document.selection.resize(document.width, document.height);
    document.clear_history();
    out_document = std::move(document);
    return true;
}

