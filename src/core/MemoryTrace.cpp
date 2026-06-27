// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "core/MemoryTrace.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

#if defined(__APPLE__)
#include <mach/mach.h>
#elif defined(__linux__)
#include <unistd.h>

#include <cstdio>
#endif

namespace px {

namespace {

using Clock = std::chrono::steady_clock;

const Clock::time_point kTraceStart = Clock::now();
std::mutex g_trace_mutex;
std::ofstream g_trace_file;
thread_local std::vector<std::string> g_trace_stack;

bool env_truthy(const char* value) {
    if (value == nullptr || value[0] == '\0') {
        return false;
    }
    std::string text(value);
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text != "0" && text != "false" && text != "off" && text != "no";
}

std::filesystem::path trace_path() {
    if (const char* path = std::getenv("PIXELART_TRACE_MEMORY_FILE"); path != nullptr && path[0] != '\0') {
        return std::filesystem::path(path);
    }
    return std::filesystem::path("logs") / "huge_image_memory_trace.csv";
}

bool ensure_trace_file_locked() {
    if (g_trace_file.is_open()) {
        return true;
    }
    const std::filesystem::path path = trace_path();
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::error_code error;
        std::filesystem::create_directories(parent, error);
    }
    g_trace_file.open(path, std::ios::out | std::ios::trunc);
    if (!g_trace_file.is_open()) {
        return false;
    }
    g_trace_file << "time_ms,thread,event,phase,stack,rss_bytes,label,address,size,capacity,element_size,bytes,extra\n";
    return true;
}

std::string csv_escape(std::string_view value) {
    bool needs_quotes = false;
    for (char ch : value) {
        if (ch == '"' || ch == ',' || ch == '\n' || ch == '\r') {
            needs_quotes = true;
            break;
        }
    }
    if (!needs_quotes) {
        return std::string(value);
    }
    std::string escaped;
    escaped.reserve(value.size() + 2U);
    escaped.push_back('"');
    for (char ch : value) {
        if (ch == '"') {
            escaped.push_back('"');
        }
        escaped.push_back(ch);
    }
    escaped.push_back('"');
    return escaped;
}

std::string stack_string() {
    std::string out;
    for (std::size_t i = 0; i < g_trace_stack.size(); ++i) {
        if (i > 0U) {
            out += ">";
        }
        out += g_trace_stack[i];
    }
    return out;
}

std::string pointer_string(const void* address) {
    if (address == nullptr) {
        return {};
    }
    std::ostringstream out;
    out << address;
    return out.str();
}

} // namespace

bool memory_trace_enabled() {
    static const bool enabled = env_truthy(std::getenv("PIXELART_TRACE_MEMORY"));
    return enabled;
}

std::size_t memory_trace_rss_bytes() {
#if defined(__APPLE__)
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
        return static_cast<std::size_t>(info.resident_size);
    }
    return 0;
#elif defined(__linux__)
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        return 0;
    }
    FILE* file = std::fopen("/proc/self/statm", "r");
    if (file == nullptr) {
        return 0;
    }
    unsigned long pages = 0;
    unsigned long resident = 0;
    const int scanned = std::fscanf(file, "%lu %lu", &pages, &resident);
    std::fclose(file);
    if (scanned != 2) {
        return 0;
    }
    return static_cast<std::size_t>(resident) * static_cast<std::size_t>(page_size);
#else
    return 0;
#endif
}

void memory_trace_event(std::string_view event,
                        std::string_view phase,
                        std::string_view label,
                        const void* address,
                        std::size_t size,
                        std::size_t capacity,
                        std::size_t element_size,
                        std::string_view extra) {
    if (!memory_trace_enabled()) {
        return;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - kTraceStart).count();
    const std::size_t rss = memory_trace_rss_bytes();
    const std::size_t bytes = capacity * element_size;
    const std::size_t thread_id = std::hash<std::thread::id>{}(std::this_thread::get_id());
    const std::string stack = stack_string();
    const std::string address_text = pointer_string(address);

    std::lock_guard lock(g_trace_mutex);
    if (!ensure_trace_file_locked()) {
        return;
    }
    g_trace_file << elapsed << ','
                 << thread_id << ','
                 << csv_escape(event) << ','
                 << csv_escape(phase) << ','
                 << csv_escape(stack) << ','
                 << rss << ','
                 << csv_escape(label) << ','
                 << address_text << ','
                 << size << ','
                 << capacity << ','
                 << element_size << ','
                 << bytes << ','
                 << csv_escape(extra) << '\n';
    g_trace_file.flush();
}

void memory_trace_note(std::string_view label, std::string_view extra) {
    memory_trace_event("note", {}, label, nullptr, 0, 0, 0, extra);
}

MemoryTraceScope::MemoryTraceScope(std::string_view phase, std::string_view extra)
    : enabled_(memory_trace_enabled()),
      phase_(phase) {
    if (!enabled_) {
        return;
    }
    g_trace_stack.push_back(phase_);
    memory_trace_event("begin", phase_, {}, nullptr, 0, 0, 0, extra);
}

MemoryTraceScope::~MemoryTraceScope() {
    if (!enabled_) {
        return;
    }
    memory_trace_event("end", phase_);
    if (!g_trace_stack.empty()) {
        g_trace_stack.pop_back();
    }
}

} // namespace px
