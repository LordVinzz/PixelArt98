// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "core/Document.hpp"
#include "core/DocumentInternal.hpp"

#include "core/MemoryTrace.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <numeric>
#include <sstream>
#include <system_error>

#if defined(_WIN32)
#include <process.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace px {

namespace history_detail {

constexpr std::size_t kDefaultDenseHistoryMinBytes = 64U * 1024U * 1024U;
constexpr std::size_t kDefaultHistorySpillBytes = 64U * 1024U * 1024U;
constexpr std::size_t kHistoryIoChunkBytes = 64U * 1024U * 1024U;
constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;
constexpr std::uint64_t kHistoryBlockVersion = 1;
constexpr std::uint64_t kHistoryBlockFixedHeaderBytes = 8U + 9U * 8U;
constexpr char kHistoryJournalMagic[8] = {'P', 'X', 'H', 'I', 'S', 'T', '2', '\n'};
constexpr char kHistoryBlockMagic[8] = {'P', 'X', 'H', 'B', 'L', 'K', '2', '\n'};

std::size_t pixel_byte_count(std::size_t pixel_count) {
    if (pixel_count > std::numeric_limits<std::size_t>::max() / sizeof(Pixel)) {
        return std::numeric_limits<std::size_t>::max();
    }
    return pixel_count * sizeof(Pixel);
}

std::size_t env_size_bytes(const char* name, std::size_t fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }
    std::istringstream in(value);
    unsigned long long parsed = 0;
    if (!(in >> parsed)) {
        return fallback;
    }
    const unsigned long long max_value =
        static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max());
    if (parsed > max_value) {
        return std::numeric_limits<std::size_t>::max();
    }
    return static_cast<std::size_t>(parsed);
}

std::size_t dense_history_min_bytes() {
    return env_size_bytes("PIXELART_DENSE_HISTORY_MIN_BYTES", kDefaultDenseHistoryMinBytes);
}

std::size_t history_spill_bytes() {
    return env_size_bytes("PIXELART_HISTORY_SPILL_BYTES", kDefaultHistorySpillBytes);
}

TileDiffStats analyze_tile_diff_stats(const std::vector<Pixel>& before,
                                      const std::vector<Pixel>& after,
                                      int width,
                                      int height,
                                      int tile_size) {
    TileDiffStats stats;
    if (before.size() != after.size() || width <= 0 || height <= 0 || tile_size <= 0) {
        return stats;
    }
    for (int ty = 0; ty < height; ty += tile_size) {
        for (int tx = 0; tx < width; tx += tile_size) {
            ++stats.total_tiles;
            const int tw = std::min(tile_size, width - tx);
            const int th = std::min(tile_size, height - ty);
            bool changed = false;
            for (int y = 0; y < th && !changed; ++y) {
                for (int x = 0; x < tw; ++x) {
                    const std::size_t i = static_cast<std::size_t>((ty + y) * width + tx + x);
                    if (before[i] != after[i]) {
                        changed = true;
                        break;
                    }
                }
            }
            if (changed) {
                ++stats.changed_tiles;
                stats.payload_pixels += static_cast<std::size_t>(tw) * static_cast<std::size_t>(th);
            }
        }
    }
    return stats;
}

bool should_use_dense_pixel_history(const TileDiffStats& stats, std::size_t full_pixel_count) {
    if (!stats.any_changed() || full_pixel_count == 0) {
        return false;
    }
    const std::size_t payload_bytes = pixel_byte_count(stats.payload_pixels);
    if (payload_bytes < dense_history_min_bytes()) {
        return false;
    }
    return stats.payload_pixels >= (full_pixel_count / 4U);
}

std::uint64_t checksum_update(std::uint64_t checksum, const char* data, std::size_t byte_count) {
    for (std::size_t i = 0; i < byte_count; ++i) {
        checksum ^= static_cast<unsigned char>(data[i]);
        checksum *= kFnvPrime;
    }
    return checksum;
}

void write_u64(std::ostream& out, std::uint64_t value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

void write_u64_at(std::fstream& file, std::uint64_t offset, std::uint64_t value) {
    file.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
    write_u64(file, value);
}

std::uint64_t encode_i32(int value) {
    return static_cast<std::uint64_t>(static_cast<std::int64_t>(value));
}

int current_process_id() {
#if defined(_WIN32)
    return _getpid();
#else
    return getpid();
#endif
}

std::filesystem::path history_journal_directory() {
    if (const char* path = std::getenv("PIXELART_HISTORY_DIR"); path != nullptr && path[0] != '\0') {
        return std::filesystem::path(path);
    }
    return std::filesystem::temp_directory_path() / "pixelart_history";
}

std::filesystem::path history_journal_path() {
    if (const char* path = std::getenv("PIXELART_HISTORY_JOURNAL_FILE"); path != nullptr && path[0] != '\0') {
        return std::filesystem::path(path);
    }
    static const std::filesystem::path path = [] {
        const auto now = std::chrono::system_clock::now().time_since_epoch();
        const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
        std::ostringstream name;
        name << "pixelart_history_" << current_process_id() << '_' << millis << ".pxhist";
        return history_journal_directory() / name.str();
    }();
    return path;
}

void apply_private_permissions(const std::filesystem::path& path, bool directory) {
    std::error_code error;
    const auto permissions = directory
                                 ? std::filesystem::perms::owner_all
                                 : (std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
    std::filesystem::permissions(path, permissions, std::filesystem::perm_options::replace, error);
}

void ensure_history_journal_lock(const std::filesystem::path& journal_path) {
#if !defined(_WIN32)
    static int lock_fd = -1;
    if (lock_fd >= 0) {
        return;
    }
    std::filesystem::path lock_path = journal_path;
    lock_path += ".lock";
    lock_fd = open(lock_path.c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if (lock_fd >= 0) {
        static_cast<void>(flock(lock_fd, LOCK_EX));
        apply_private_permissions(lock_path, false);
    }
#else
    (void)journal_path;
#endif
}

bool ensure_history_journal_file_locked(const std::filesystem::path& journal_path) {
    const std::filesystem::path parent = journal_path.parent_path();
    if (!parent.empty()) {
        std::error_code error;
        std::filesystem::create_directories(parent, error);
        apply_private_permissions(parent, true);
    }
    ensure_history_journal_lock(journal_path);

    std::error_code error;
    const bool has_existing_header =
        std::filesystem::exists(journal_path, error) && std::filesystem::file_size(journal_path, error) > 0U;
    if (has_existing_header) {
        apply_private_permissions(journal_path, false);
        return true;
    }

    std::ofstream out(journal_path, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out.write(kHistoryJournalMagic, sizeof(kHistoryJournalMagic));
    out.flush();
    apply_private_permissions(journal_path, false);
    return static_cast<bool>(out);
}

std::mutex& history_journal_mutex() {
    static std::mutex mutex;
    return mutex;
}

HistoryPayloadRef append_pixels_to_history_journal(const Pixel* pixels,
                                                   std::size_t pixel_count,
                                                   int frame,
                                                   int layer,
                                                   int width,
                                                   int height,
                                                   std::string_view label) {
    HistoryPayloadRef ref;
    if (pixels == nullptr || pixel_count == 0) {
        return ref;
    }

    std::lock_guard lock(history_journal_mutex());
    const std::filesystem::path journal_path = history_journal_path();
    if (!ensure_history_journal_file_locked(journal_path)) {
        return ref;
    }

    std::error_code error;
    const std::uint64_t block_offset =
        static_cast<std::uint64_t>(std::filesystem::file_size(journal_path, error));
    if (error) {
        return ref;
    }
    const std::uint64_t label_size = static_cast<std::uint64_t>(label.size());
    const std::uint64_t data_offset = block_offset + kHistoryBlockFixedHeaderBytes + label_size;
    const std::size_t total_bytes = pixel_byte_count(pixel_count);
    const std::uint64_t byte_count = static_cast<std::uint64_t>(total_bytes);
    const std::uint64_t checksum_offset = block_offset + 8U + 7U * 8U;

    std::fstream file(journal_path, std::ios::binary | std::ios::in | std::ios::out);
    if (!file.is_open()) {
        return ref;
    }
    file.seekp(static_cast<std::streamoff>(block_offset), std::ios::beg);
    file.write(kHistoryBlockMagic, sizeof(kHistoryBlockMagic));
    write_u64(file, kHistoryBlockVersion);
    write_u64(file, encode_i32(frame));
    write_u64(file, encode_i32(layer));
    write_u64(file, encode_i32(width));
    write_u64(file, encode_i32(height));
    write_u64(file, static_cast<std::uint64_t>(pixel_count));
    write_u64(file, byte_count);
    write_u64(file, 0);
    write_u64(file, label_size);
    file.write(label.data(), static_cast<std::streamsize>(label.size()));

    const char* bytes = reinterpret_cast<const char*>(pixels);
    std::size_t written = 0;
    std::uint64_t checksum = kFnvOffset;
    while (written < total_bytes) {
        const std::size_t chunk = std::min(kHistoryIoChunkBytes, total_bytes - written);
        file.write(bytes + written, static_cast<std::streamsize>(chunk));
        if (!file) {
            return {};
        }
        checksum = checksum_update(checksum, bytes + written, chunk);
        written += chunk;
    }
    write_u64_at(file, checksum_offset, checksum);
    file.flush();
    if (!file) {
        return {};
    }

    ref.journal_path = journal_path.string();
    ref.data_offset = data_offset;
    ref.pixel_count = static_cast<std::uint64_t>(pixel_count);
    ref.checksum = checksum;
    memory_trace_event("counter",
                       {},
                       "history_journal.spilled_pixels",
                       nullptr,
                       pixel_count,
                       pixel_count,
                       sizeof(Pixel),
                       label);
    return ref;
}

bool read_pixels_from_history_journal(const HistoryPayloadRef& ref, Pixel* pixels, std::size_t pixel_count) {
    if (!ref.valid() || pixels == nullptr || ref.pixel_count != static_cast<std::uint64_t>(pixel_count)) {
        return false;
    }
    std::ifstream file(ref.journal_path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    file.seekg(static_cast<std::streamoff>(ref.data_offset), std::ios::beg);
    char* bytes = reinterpret_cast<char*>(pixels);
    const std::size_t total_bytes = pixel_byte_count(pixel_count);
    std::size_t read = 0;
    std::uint64_t checksum = kFnvOffset;
    while (read < total_bytes) {
        const std::size_t chunk = std::min(kHistoryIoChunkBytes, total_bytes - read);
        file.read(bytes + read, static_cast<std::streamsize>(chunk));
        if (file.gcount() != static_cast<std::streamsize>(chunk)) {
            return false;
        }
        checksum = checksum_update(checksum, bytes + read, chunk);
        read += chunk;
    }
    return checksum == ref.checksum;
}

bool should_spill_history_payload(std::size_t pixel_count) {
    return pixel_count > 0 && pixel_byte_count(pixel_count) >= history_spill_bytes();
}

void clear_payload_memory(HistoryPixelPayload& payload) {
    std::vector<Pixel>().swap(payload.pixels);
}

bool store_history_payload(HistoryPixelPayload& payload,
                           const Pixel* pixels,
                           std::size_t pixel_count,
                           int frame,
                           int layer,
                           int width,
                           int height,
                           std::string_view label) {
    payload = {};
    payload.pixel_count = static_cast<std::uint64_t>(pixel_count);
    if (pixel_count == 0) {
        return true;
    }
    if (should_spill_history_payload(pixel_count)) {
        payload.ref = append_pixels_to_history_journal(pixels, pixel_count, frame, layer, width, height, label);
        if (payload.ref.valid()) {
            return true;
        }
        memory_trace_note("history_journal.spill_failed", label);
    }
    payload.pixels.assign(pixels, pixels + pixel_count);
    return payload.pixels.size() == pixel_count;
}

bool dense_diff_target_valid(const Document& doc, const DensePixelDiff& diff) {
    return diff.frame >= 0 &&
           diff.layer >= 0 &&
           diff.frame < static_cast<int>(doc.frames.size()) &&
           diff.layer < static_cast<int>(doc.layers.size());
}

bool apply_history_payload(Document& doc, const DensePixelDiff& diff, const HistoryPixelPayload& payload) {
    if (!dense_diff_target_valid(doc, diff)) {
        return false;
    }
    const std::size_t expected_pixels = static_cast<std::size_t>(std::max(0, diff.width)) *
                                        static_cast<std::size_t>(std::max(0, diff.height));
    if (payload.pixel_count != static_cast<std::uint64_t>(expected_pixels)) {
        return false;
    }
    auto& pixels = doc.cel(diff.frame, diff.layer).pixels;
    if (pixels.size() != expected_pixels) {
        pixels.assign(expected_pixels, 0);
    }
    if (payload.ref.valid()) {
        return read_pixels_from_history_journal(payload.ref, pixels.data(), expected_pixels);
    }
    if (payload.pixels.size() != expected_pixels) {
        return false;
    }
    std::copy(payload.pixels.begin(), payload.pixels.end(), pixels.begin());
    return true;
}

bool capture_dense_after_pixels(Document& doc, DensePixelDiff& diff) {
    if (!dense_diff_target_valid(doc, diff)) {
        return false;
    }
    const auto& pixels = doc.cel(diff.frame, diff.layer).pixels;
    return store_history_payload(diff.after,
                                 pixels.data(),
                                 pixels.size(),
                                 diff.frame,
                                 diff.layer,
                                 diff.width,
                                 diff.height,
                                 "dense_after");
}

void release_dense_after_pixels(DensePixelDiff& diff) {
    clear_payload_memory(diff.after);
    diff.after = {};
}

DensePixelDiff make_dense_pixel_diff(const std::vector<Pixel>& before,
                                     int frame,
                                     int layer,
                                     int width,
                                     int height,
                                     std::string_view label) {
    DensePixelDiff diff;
    diff.frame = frame;
    diff.layer = layer;
    diff.width = width;
    diff.height = height;
    static_cast<void>(store_history_payload(diff.before,
                                            before.data(),
                                            before.size(),
                                            frame,
                                            layer,
                                            width,
                                            height,
                                            label));
    return diff;
}

void trace_tile_diff_storage(std::string_view prefix, const std::vector<TileDiff>& diffs) {
    memory_trace_event("vector",
                       {},
                       std::string(prefix) + ".pixel_diffs",
                       diffs.empty() ? nullptr : diffs.data(),
                       diffs.size(),
                       diffs.capacity(),
                       sizeof(TileDiff));
    std::size_t diff_payload_pixels = 0;
    for (const TileDiff& diff : diffs) {
        diff_payload_pixels += diff.before.capacity() + diff.after.capacity();
    }
    memory_trace_event("counter",
                       {},
                       std::string(prefix) + ".pixel_diff_payload_pixels",
                       nullptr,
                       diff_payload_pixels,
                       diff_payload_pixels,
                       sizeof(Pixel));
}

void trace_dense_diff_storage(std::string_view prefix, const DensePixelDiff& diff) {
    memory_trace_event("counter",
                       {},
                       std::string(prefix) + ".dense_before_pixels",
                       nullptr,
                       static_cast<std::size_t>(diff.before.pixel_count),
                       static_cast<std::size_t>(diff.before.pixel_count),
                       sizeof(Pixel),
                       diff.before.ref.valid() ? "disk" : "memory");
}

} // namespace history_detail

std::vector<TileDiff> make_tile_diffs(const std::vector<Pixel>& before,
                                      const std::vector<Pixel>& after,
                                      int width,
                                      int height,
                                      int frame,
                                      int layer,
                                      int tile_size,
                                      bool store_after_pixels) {
    std::vector<TileDiff> diffs;
    if (before.size() != after.size() || width <= 0 || height <= 0) {
        return diffs;
    }
    for (int ty = 0; ty < height; ty += tile_size) {
        for (int tx = 0; tx < width; tx += tile_size) {
            int tw = std::min(tile_size, width - tx);
            int th = std::min(tile_size, height - ty);
            bool changed = false;
            for (int y = 0; y < th && !changed; ++y) {
                for (int x = 0; x < tw; ++x) {
                    std::size_t i = static_cast<std::size_t>((ty + y) * width + tx + x);
                    if (before[i] != after[i]) {
                        changed = true;
                        break;
                    }
                }
            }
            if (!changed) {
                continue;
            }
            TileDiff diff;
            diff.frame = frame;
            diff.layer = layer;
            diff.x = tx;
            diff.y = ty;
            diff.w = tw;
            diff.h = th;
            diff.before.reserve(static_cast<std::size_t>(tw * th));
            if (store_after_pixels) {
                diff.after.reserve(static_cast<std::size_t>(tw * th));
            }
            for (int y = 0; y < th; ++y) {
                for (int x = 0; x < tw; ++x) {
                    std::size_t i = static_cast<std::size_t>((ty + y) * width + tx + x);
                    diff.before.push_back(before[i]);
                    if (store_after_pixels) {
                        diff.after.push_back(after[i]);
                    }
                }
            }
            diffs.push_back(std::move(diff));
        }
    }
    return diffs;
}
bool Document::materialize_history_payload(const HistoryPixelPayload& payload,
                                           std::vector<Pixel>& pixels) const {
    if (!payload.pixels.empty()) {
        pixels = payload.pixels;
        return payload.pixel_count == 0U ||
               payload.pixel_count == static_cast<std::uint64_t>(pixels.size());
    }
    if (!payload.ref.valid()) {
        pixels.clear();
        return payload.pixel_count == 0U;
    }
    if (payload.pixel_count > static_cast<std::uint64_t>(
                                  std::numeric_limits<std::size_t>::max())) {
        pixels.clear();
        return false;
    }
    pixels.resize(static_cast<std::size_t>(payload.pixel_count));
    if (!history_detail::read_pixels_from_history_journal(payload.ref, pixels.data(), pixels.size())) {
        pixels.clear();
        return false;
    }
    return true;
}
} // namespace px
