// Internal implementation fragment for ProjectIO raster codecs.
// This file is included by its owning translation unit; do not compile it separately.

int nearest_palette_index(Pixel p, const std::vector<Pixel>& palette) {
    int best_index = 0;
    int best_d = 1 << 30;
    for (int i = 0; i < static_cast<int>(palette.size()); ++i) {
        int d = color_distance(p, palette[static_cast<std::size_t>(i)], true);
        if (d < best_d) {
            best_d = d;
            best_index = i;
        }
    }
    return best_index;
}

std::vector<Pixel> gif_palette(const Document& document) {
    std::vector<Pixel> palette = document.palette.colors;
    if (palette.empty()) {
        palette = default_palette();
    }
    if (palette.size() > 256) {
        palette.resize(256);
    }
    while (palette.size() < 256) {
        palette.push_back(rgba(0, 0, 0, 0));
    }
    return palette;
}

void write_be32(std::vector<unsigned char>& out, std::uint32_t value) {
    out.push_back(static_cast<unsigned char>((value >> 24) & 0xff));
    out.push_back(static_cast<unsigned char>((value >> 16) & 0xff));
    out.push_back(static_cast<unsigned char>((value >> 8) & 0xff));
    out.push_back(static_cast<unsigned char>(value & 0xff));
}

void write_be16(std::vector<unsigned char>& out, std::uint16_t value) {
    out.push_back(static_cast<unsigned char>((value >> 8) & 0xff));
    out.push_back(static_cast<unsigned char>(value & 0xff));
}

void write_chunk(std::vector<unsigned char>& png, const char type[4], const std::vector<unsigned char>& data) {
    write_be32(png, static_cast<std::uint32_t>(data.size()));
    std::size_t type_offset = png.size();
    png.insert(png.end(), type, type + 4);
    png.insert(png.end(), data.begin(), data.end());
    mz_ulong crc = mz_crc32(0, nullptr, 0);
    crc = mz_crc32(crc, png.data() + type_offset, static_cast<std::size_t>(4 + data.size()));
    write_be32(png, static_cast<std::uint32_t>(crc));
}

std::vector<unsigned char> filtered_rgba_scanlines(int width, int height, const std::vector<Pixel>& pixels) {
    std::vector<unsigned char> raw;
    raw.reserve(static_cast<std::size_t>((width * 4 + 1) * height));
    const auto* bytes = reinterpret_cast<const unsigned char*>(pixels.data());
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        const unsigned char* row = bytes + static_cast<std::size_t>(y * width * 4);
        raw.insert(raw.end(), row, row + static_cast<std::size_t>(width * 4));
    }
    return raw;
}

bool compress_png_data(const std::vector<unsigned char>& raw, std::vector<unsigned char>& compressed) {
    if (raw.size() > static_cast<std::size_t>(std::numeric_limits<mz_ulong>::max())) {
        compressed.clear();
        return false;
    }
    const auto raw_size = static_cast<mz_ulong>(raw.size());
    mz_ulong bound = mz_compressBound(raw_size);
    compressed.resize(static_cast<std::size_t>(bound));
    int result = mz_compress2(compressed.data(), &bound, raw.data(), raw_size, MZ_BEST_COMPRESSION);
    if (result != MZ_OK) {
        compressed.clear();
        return false;
    }
    compressed.resize(static_cast<std::size_t>(bound));
    return true;
}

void write_u8(std::vector<unsigned char>& out, std::uint8_t value) {
    out.push_back(value);
}

void write_le16(std::vector<unsigned char>& out, std::uint16_t value) {
    out.push_back(static_cast<unsigned char>(value & 0xff));
    out.push_back(static_cast<unsigned char>((value >> 8) & 0xff));
}

void write_le32(std::vector<unsigned char>& out, std::uint32_t value) {
    out.push_back(static_cast<unsigned char>(value & 0xff));
    out.push_back(static_cast<unsigned char>((value >> 8) & 0xff));
    out.push_back(static_cast<unsigned char>((value >> 16) & 0xff));
    out.push_back(static_cast<unsigned char>((value >> 24) & 0xff));
}

void write_i16(std::vector<unsigned char>& out, std::int16_t value) {
    write_le16(out, static_cast<std::uint16_t>(value));
}

void write_ase_string(std::vector<unsigned char>& out, const std::string& value) {
    constexpr std::size_t max_ase_string_size = 65535;
    const std::size_t size = std::min(value.size(), max_ase_string_size);
    write_le16(out, static_cast<std::uint16_t>(size));
    const auto signed_size = static_cast<std::string::difference_type>(size);
    out.insert(out.end(), value.begin(), std::next(value.begin(), signed_size));
}

bool read_u8(const std::vector<unsigned char>& in, std::size_t& offset, std::uint8_t& value) {
    if (offset + 1 > in.size()) return false;
    value = in[offset++];
    return true;
}

bool read_le16(const std::vector<unsigned char>& in, std::size_t& offset, std::uint16_t& value) {
    if (offset + 2 > in.size()) return false;
    value = static_cast<std::uint16_t>(in[offset] | (in[offset + 1] << 8));
    offset += 2;
    return true;
}

bool read_i16(const std::vector<unsigned char>& in, std::size_t& offset, std::int16_t& value) {
    std::uint16_t raw = 0;
    if (!read_le16(in, offset, raw)) return false;
    value = static_cast<std::int16_t>(raw);
    return true;
}

bool read_le32(const std::vector<unsigned char>& in, std::size_t& offset, std::uint32_t& value) {
    if (offset + 4 > in.size()) return false;
    value = static_cast<std::uint32_t>(in[offset]) |
            (static_cast<std::uint32_t>(in[offset + 1]) << 8) |
            (static_cast<std::uint32_t>(in[offset + 2]) << 16) |
            (static_cast<std::uint32_t>(in[offset + 3]) << 24);
    offset += 4;
    return true;
}

bool skip_bytes(const std::vector<unsigned char>& in, std::size_t& offset, std::size_t count) {
    if (offset + count > in.size()) return false;
    offset += count;
    return true;
}

bool read_ase_string(const std::vector<unsigned char>& in, std::size_t& offset, std::string& value) {
    std::uint16_t length = 0;
    if (!read_le16(in, offset, length) || offset + length > in.size()) return false;
    value.assign(reinterpret_cast<const char*>(in.data() + offset), length);
    offset += length;
    return true;
}

bool compress_zlib(const std::vector<unsigned char>& raw, std::vector<unsigned char>& compressed) {
    if (raw.size() > static_cast<std::size_t>(std::numeric_limits<mz_ulong>::max())) {
        compressed.clear();
        return false;
    }
    const auto raw_size = static_cast<mz_ulong>(raw.size());
    mz_ulong bound = mz_compressBound(raw_size);
    compressed.resize(static_cast<std::size_t>(bound));
    int result = mz_compress2(compressed.data(), &bound, raw.data(), raw_size, MZ_BEST_SPEED);
    if (result != MZ_OK) {
        compressed.clear();
        return false;
    }
    compressed.resize(static_cast<std::size_t>(bound));
    return true;
}

bool uncompress_zlib(const unsigned char* data, std::size_t size, std::size_t expected_size, std::vector<unsigned char>& raw) {
    raw.resize(expected_size);
    mz_ulong out_size = static_cast<mz_ulong>(raw.size());
    int result = mz_uncompress(raw.data(), &out_size, data, static_cast<mz_ulong>(size));
    if (result != MZ_OK || out_size != expected_size) {
        raw.clear();
        return false;
    }
    return true;
}

std::vector<unsigned char> chunk_with_header(std::uint16_t type, const std::vector<unsigned char>& payload) {
    std::vector<unsigned char> out;
    write_le32(out, static_cast<std::uint32_t>(payload.size() + 6));
    write_le16(out, type);
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

void publish_image_progress(const ImageImportProgressCallback& progress,
                            float fraction,
                            std::string status,
                            std::string phase = {},
                            int done = 0,
                            int total = 0,
                            bool indeterminate = false) {
    if (progress) {
        progress({std::clamp(fraction, 0.0f, 1.0f),
                  done,
                  total,
                  indeterminate,
                  std::move(phase),
                  std::move(status)});
    }
}

Document document_from_pixels_impl(int width, int height, std::vector<Pixel> pixels) {
    Document document;
    document.width = std::max(1, width);
    document.height = std::max(1, height);
    Layer background;
    background.name = "Background";
    document.layers.push_back(std::move(background));
    Frame frame;
    Cel cel;
    cel.pixels = std::move(pixels);
    frame.cels.push_back(std::move(cel));
    document.frames.push_back(std::move(frame));
    document.palette.colors = default_palette();
    document.selection.resize(document.width, document.height);
    document.clear_history();
    return document;
}

std::uint32_t read_be32_bytes(const std::array<unsigned char, 4>& bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 24U) |
           (static_cast<std::uint32_t>(bytes[1]) << 16U) |
           (static_cast<std::uint32_t>(bytes[2]) << 8U) |
           static_cast<std::uint32_t>(bytes[3]);
}

bool read_exact(std::ifstream& file, unsigned char* data, std::size_t size) {
    file.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(size));
    return file.good() || (size == 0U && !file.bad());
}

bool skip_exact(std::ifstream& file, std::uint32_t size) {
    constexpr std::streamoff kSkipChunk = 1024 * 1024;
    std::uint32_t remaining = size;
    while (remaining > 0U) {
        const std::streamoff step = std::min<std::streamoff>(kSkipChunk, static_cast<std::streamoff>(remaining));
        file.seekg(step, std::ios::cur);
        if (!file) {
            return false;
        }
        remaining -= static_cast<std::uint32_t>(step);
    }
    return true;
}

int png_channel_count(int color_type) {
    switch (color_type) {
        case 0: return 1;
        case 2: return 3;
        case 3: return 1;
        case 4: return 2;
        case 6: return 4;
        default: return 0;
    }
}

unsigned char png_paeth(unsigned char a_value, unsigned char b_value, unsigned char c_value) {
    const int a_int = static_cast<int>(a_value);
    const int b_int = static_cast<int>(b_value);
    const int c_int = static_cast<int>(c_value);
    const int p = a_int + b_int - c_int;
    const int pa = std::abs(p - a_int);
    const int pb = std::abs(p - b_int);
    const int pc = std::abs(p - c_int);
    if (pa <= pb && pa <= pc) {
        return a_value;
    }
    return pb <= pc ? b_value : c_value;
}

struct StreamingPngState {
    int width = 0;
    int height = 0;
    int bit_depth = 0;
    int color_type = 0;
    int channels = 0;
    std::size_t row_bytes = 0;
    std::size_t scanline_bytes = 0;
    std::size_t scanline_offset = 0;
    int row = 0;
    std::vector<unsigned char> scanline;
    std::vector<unsigned char> previous;
    std::vector<unsigned char> current;
    std::vector<Pixel> pixels;
    std::array<Pixel, 256> palette = {};
    std::array<std::uint8_t, 256> palette_alpha = {};
    int palette_size = 0;
};

bool process_png_scanline(StreamingPngState& state, std::string& error) {
    MemoryTraceScope trace("process_png_scanline");
    if (state.scanline.empty() || state.row >= state.height) {
        error = "PNG scanline overflow";
        return false;
    }
    const unsigned char filter = state.scanline[0];
    if (filter > 4U) {
        error = "Unsupported PNG filter";
        return false;
    }
    for (std::size_t i = 0; i < state.row_bytes; ++i) {
        const unsigned char raw = state.scanline[i + 1U];
        const unsigned char left = i >= static_cast<std::size_t>(state.channels) ? state.current[i - static_cast<std::size_t>(state.channels)] : 0U;
        const unsigned char up = state.previous.empty() ? 0U : state.previous[i];
        const unsigned char up_left = (!state.previous.empty() && i >= static_cast<std::size_t>(state.channels))
                                          ? state.previous[i - static_cast<std::size_t>(state.channels)]
                                          : 0U;
        unsigned int value = raw;
        switch (filter) {
            case 1: value += left; break;
            case 2: value += up; break;
            case 3: value += (static_cast<unsigned int>(left) + static_cast<unsigned int>(up)) / 2U; break;
            case 4: value += png_paeth(left, up, up_left); break;
            default: break;
        }
        state.current[i] = static_cast<unsigned char>(value & 0xffU);
    }

    for (int x = 0; x < state.width; ++x) {
        const std::size_t src = static_cast<std::size_t>(x) * static_cast<std::size_t>(state.channels);
        Pixel pixel = 0;
        switch (state.color_type) {
            case 0: {
                const std::uint8_t value = state.current[src];
                pixel = rgba(value, value, value, 255);
                break;
            }
            case 2:
                pixel = rgba(state.current[src], state.current[src + 1U], state.current[src + 2U], 255);
                break;
            case 3: {
                const int index = state.current[src];
                pixel = index >= 0 && index < state.palette_size ? state.palette[static_cast<std::size_t>(index)] : rgba(0, 0, 0, 0);
                break;
            }
            case 4: {
                const std::uint8_t value = state.current[src];
                pixel = rgba(value, value, value, state.current[src + 1U]);
                break;
            }
            case 6:
                pixel = rgba(state.current[src], state.current[src + 1U], state.current[src + 2U], state.current[src + 3U]);
                break;
            default:
                error = "Unsupported PNG color type";
                return false;
        }
        state.pixels[static_cast<std::size_t>(state.row) * static_cast<std::size_t>(state.width) + static_cast<std::size_t>(x)] = pixel;
    }

    state.previous = state.current;
    ++state.row;
    return true;
}

bool process_png_output_bytes(StreamingPngState& state,
                              const unsigned char* bytes,
                              std::size_t size,
                              const ImageImportProgressCallback& progress,
                              std::string& error) {
    MemoryTraceScope trace("process_png_output_bytes");
    memory_trace_event("buffer", {}, "process_png_output_bytes.input", bytes, size, size, 1U);
    std::size_t offset = 0;
    while (offset < size) {
        const std::size_t copy_size = std::min(size - offset, state.scanline_bytes - state.scanline_offset);
        std::copy_n(bytes + offset, copy_size, state.scanline.data() + state.scanline_offset);
        offset += copy_size;
        state.scanline_offset += copy_size;
        if (state.scanline_offset == state.scanline_bytes) {
            if (!process_png_scanline(state, error)) {
                return false;
            }
            state.scanline_offset = 0;
            if ((state.row & 255) == 0 || state.row == state.height) {
                const float fraction = 0.10f + 0.85f * (static_cast<float>(state.row) / static_cast<float>(std::max(1, state.height)));
                publish_image_progress(progress,
                                       fraction,
                                       "Decoding PNG rows " + std::to_string(state.row) + " / " + std::to_string(state.height),
                                       "Decoding PNG",
                                       state.row,
                                       state.height);
            }
        }
    }
    return true;
}

bool decode_png_streaming_rgba_impl(const std::string& path,
                                    int& width,
                                    int& height,
                                    std::vector<Pixel>& pixels,
                                    std::string* error,
                                    const ImageImportProgressCallback& progress) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        set_error(error, "Could not open PNG: " + path);
        return false;
    }

    constexpr std::array<unsigned char, 8> kPngSignature = {137, 80, 78, 71, 13, 10, 26, 10};
    std::array<unsigned char, 8> signature{};
    if (!read_exact(file, signature.data(), signature.size()) || signature != kPngSignature) {
        set_error(error, "File is not a PNG: " + path);
        return false;
    }

    StreamingPngState state;
    std::fill(state.palette_alpha.begin(), state.palette_alpha.end(), 255);
    mz_stream stream{};
    bool inflate_started = false;
    bool inflate_finished = false;
    std::vector<unsigned char> input_buffer(1024U * 1024U);
    std::vector<unsigned char> output_buffer(1024U * 1024U);
    memory_trace_vector("decode_png_streaming_rgba.input_buffer", input_buffer);
    memory_trace_vector("decode_png_streaming_rgba.output_buffer", output_buffer);
    publish_image_progress(progress, 0.02f, "Reading PNG header", "Reading header", 0, 0, true);

    auto finish_with_error = [&](const std::string& message) {
        if (inflate_started) {
            mz_inflateEnd(&stream);
        }
        set_error(error, message);
        return false;
    };

    for (;;) {
        std::array<unsigned char, 4> length_bytes{};
        std::array<unsigned char, 4> type_bytes{};
        if (!read_exact(file, length_bytes.data(), length_bytes.size()) ||
            !read_exact(file, type_bytes.data(), type_bytes.size())) {
            return finish_with_error("Unexpected end of PNG");
        }
        const std::uint32_t chunk_length = read_be32_bytes(length_bytes);
        const std::string chunk_type(reinterpret_cast<const char*>(type_bytes.data()), type_bytes.size());

        if (chunk_type == "IHDR") {
            if (chunk_length != 13U) {
                return finish_with_error("Invalid PNG IHDR length");
            }
            std::array<unsigned char, 13> ihdr{};
            if (!read_exact(file, ihdr.data(), ihdr.size())) {
                return finish_with_error("Could not read PNG IHDR");
            }
            state.width = static_cast<int>((static_cast<std::uint32_t>(ihdr[0]) << 24U) |
                                           (static_cast<std::uint32_t>(ihdr[1]) << 16U) |
                                           (static_cast<std::uint32_t>(ihdr[2]) << 8U) |
                                           static_cast<std::uint32_t>(ihdr[3]));
            state.height = static_cast<int>((static_cast<std::uint32_t>(ihdr[4]) << 24U) |
                                            (static_cast<std::uint32_t>(ihdr[5]) << 16U) |
                                            (static_cast<std::uint32_t>(ihdr[6]) << 8U) |
                                            static_cast<std::uint32_t>(ihdr[7]));
            state.bit_depth = ihdr[8];
            state.color_type = ihdr[9];
            const int compression = ihdr[10];
            const int filter = ihdr[11];
            const int interlace = ihdr[12];
            std::array<unsigned char, 4> crc{};
            if (!read_exact(file, crc.data(), crc.size())) {
                return finish_with_error("Could not read PNG IHDR CRC");
            }
            if (state.width <= 0 || state.height <= 0 || state.bit_depth != 8 || compression != 0 || filter != 0 || interlace != 0) {
                return finish_with_error("Streaming PNG import supports non-interlaced 8-bit PNG images");
            }
            state.channels = png_channel_count(state.color_type);
            if (state.channels == 0) {
                return finish_with_error("Unsupported PNG color type");
            }
            const std::size_t pixel_count = static_cast<std::size_t>(state.width) * static_cast<std::size_t>(state.height);
            if (pixel_count == 0U || pixel_count > std::numeric_limits<std::size_t>::max() / sizeof(Pixel)) {
                return finish_with_error("PNG dimensions are too large");
            }
            state.row_bytes = static_cast<std::size_t>(state.width) * static_cast<std::size_t>(state.channels);
            state.scanline_bytes = state.row_bytes + 1U;
            state.scanline.assign(state.scanline_bytes, 0);
            state.previous.assign(state.row_bytes, 0);
            state.current.assign(state.row_bytes, 0);
            state.pixels.resize(pixel_count);
            memory_trace_vector("decode_png_streaming_rgba.scanline", state.scanline);
            memory_trace_vector("decode_png_streaming_rgba.previous_row", state.previous);
            memory_trace_vector("decode_png_streaming_rgba.current_row", state.current);
            memory_trace_vector("decode_png_streaming_rgba.pixels", state.pixels);
            width = state.width;
            height = state.height;
            publish_image_progress(progress,
                                   0.05f,
                                   "Allocating " + std::to_string(state.width) + " x " + std::to_string(state.height) + " image",
                                   "Allocating image",
                                   0,
                                   0,
                                   true);
            continue;
        }

        if (chunk_type == "PLTE") {
            if (chunk_length % 3U != 0U || chunk_length > 256U * 3U) {
                return finish_with_error("Invalid PNG palette");
            }
            std::vector<unsigned char> palette_bytes(chunk_length);
            if (!read_exact(file, palette_bytes.data(), palette_bytes.size())) {
                return finish_with_error("Could not read PNG palette");
            }
            state.palette_size = static_cast<int>(chunk_length / 3U);
            for (int i = 0; i < state.palette_size; ++i) {
                const std::size_t src = static_cast<std::size_t>(i) * 3U;
                state.palette[static_cast<std::size_t>(i)] =
                    rgba(palette_bytes[src], palette_bytes[src + 1U], palette_bytes[src + 2U], state.palette_alpha[static_cast<std::size_t>(i)]);
            }
            std::array<unsigned char, 4> crc{};
            if (!read_exact(file, crc.data(), crc.size())) {
                return finish_with_error("Could not read PNG palette CRC");
            }
            continue;
        }

        if (chunk_type == "tRNS") {
            std::vector<unsigned char> alpha_bytes(chunk_length);
            if (!read_exact(file, alpha_bytes.data(), alpha_bytes.size())) {
                return finish_with_error("Could not read PNG transparency");
            }
            if (state.color_type == 3) {
                const std::size_t alpha_count = std::min<std::size_t>(alpha_bytes.size(), state.palette_alpha.size());
                for (std::size_t i = 0; i < alpha_count; ++i) {
                    state.palette_alpha[i] = alpha_bytes[i];
                    if (i < static_cast<std::size_t>(state.palette_size)) {
                        state.palette[i] = with_alpha(state.palette[i], alpha_bytes[i]);
                    }
                }
            }
            std::array<unsigned char, 4> crc{};
            if (!read_exact(file, crc.data(), crc.size())) {
                return finish_with_error("Could not read PNG transparency CRC");
            }
            continue;
        }

        if (chunk_type == "IDAT") {
            if (state.width <= 0 || state.height <= 0) {
                return finish_with_error("PNG IDAT arrived before IHDR");
            }
            if (!inflate_started) {
                if (mz_inflateInit(&stream) != MZ_OK) {
                    return finish_with_error("Could not initialize PNG inflater");
                }
                inflate_started = true;
                publish_image_progress(progress, 0.08f, "Decoding PNG", "Decoding PNG", 0, state.height);
            }
            std::uint32_t remaining = chunk_length;
            while (remaining > 0U) {
                const std::size_t step = std::min<std::size_t>(input_buffer.size(), remaining);
                if (!read_exact(file, input_buffer.data(), step)) {
                    return finish_with_error("Could not read PNG image data");
                }
                remaining -= static_cast<std::uint32_t>(step);
                stream.next_in = input_buffer.data();
                stream.avail_in = static_cast<unsigned int>(step);
                do {
                    stream.next_out = output_buffer.data();
                    stream.avail_out = static_cast<unsigned int>(output_buffer.size());
                    const int result = mz_inflate(&stream, MZ_NO_FLUSH);
                    const std::size_t produced = output_buffer.size() - static_cast<std::size_t>(stream.avail_out);
                    std::string process_error;
                    if (produced > 0U && !process_png_output_bytes(state, output_buffer.data(), produced, progress, process_error)) {
                        return finish_with_error(process_error);
                    }
                    if (result == MZ_STREAM_END) {
                        inflate_finished = true;
                        break;
                    }
                    if (result != MZ_OK) {
                        return finish_with_error("PNG inflate failed");
                    }
                } while (stream.avail_in > 0U || stream.avail_out == 0U);
            }
            std::array<unsigned char, 4> crc{};
            if (!read_exact(file, crc.data(), crc.size())) {
                return finish_with_error("Could not read PNG image data CRC");
            }
            continue;
        }

        if (chunk_type == "IEND") {
            if (!skip_exact(file, chunk_length)) {
                return finish_with_error("Could not read PNG end chunk");
            }
            std::array<unsigned char, 4> crc{};
            if (!read_exact(file, crc.data(), crc.size())) {
                return finish_with_error("Could not read PNG end chunk CRC");
            }
            break;
        }

        if (!skip_exact(file, chunk_length)) {
            return finish_with_error("Could not skip PNG chunk");
        }
        std::array<unsigned char, 4> crc{};
        if (!read_exact(file, crc.data(), crc.size())) {
            return finish_with_error("Could not read PNG chunk CRC");
        }
    }

    if (inflate_started) {
        mz_inflateEnd(&stream);
    }
    if (!inflate_finished || state.row != state.height || state.scanline_offset != 0U) {
        set_error(error, "PNG ended before all image rows were decoded");
        return false;
    }
    memory_trace_vector("decode_png_streaming_rgba.state_pixels_before_move", state.pixels);
    pixels = std::move(state.pixels);
    memory_trace_vector("decode_png_streaming_rgba.output_pixels_after_move", pixels);
    memory_trace_vector("decode_png_streaming_rgba.state_pixels_after_move", state.pixels);
    publish_image_progress(progress, 0.96f, "Publishing decoded pixels", "Publishing image", 0, 0, true);
    return true;
}

