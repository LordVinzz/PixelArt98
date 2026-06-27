// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace px {

bool memory_trace_enabled();
std::size_t memory_trace_rss_bytes();

void memory_trace_event(std::string_view event,
                        std::string_view phase,
                        std::string_view label = {},
                        const void* address = nullptr,
                        std::size_t size = 0,
                        std::size_t capacity = 0,
                        std::size_t element_size = 0,
                        std::string_view extra = {});

void memory_trace_note(std::string_view label, std::string_view extra = {});

template <typename T>
void memory_trace_vector(std::string_view label, const std::vector<T>& values, std::string_view extra = {}) {
    if (!memory_trace_enabled()) {
        return;
    }
    memory_trace_event("vector",
                       {},
                       label,
                       values.empty() ? nullptr : values.data(),
                       values.size(),
                       values.capacity(),
                       sizeof(T),
                       extra);
}

class MemoryTraceScope {
public:
    explicit MemoryTraceScope(std::string_view phase, std::string_view extra = {});
    ~MemoryTraceScope();

    MemoryTraceScope(const MemoryTraceScope&) = delete;
    MemoryTraceScope& operator=(const MemoryTraceScope&) = delete;
    MemoryTraceScope(MemoryTraceScope&&) = delete;
    MemoryTraceScope& operator=(MemoryTraceScope&&) = delete;

private:
    bool enabled_ = false;
    std::string phase_;
};

} // namespace px
