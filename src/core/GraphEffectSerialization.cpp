// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "core/GraphEffect.hpp"

#include "core/Filters.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <queue>
#include <set>
#include <sstream>
#include <streambuf>
#include <system_error>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace px {

namespace {
constexpr std::size_t kMaximumGraphNodes = 4096;
constexpr std::size_t kMaximumGraphLinks = 16384;
constexpr std::size_t kMaximumGraphParameters = 256;
constexpr std::uintmax_t kMaximumGraphFileBytes = 16U * 1024U * 1024U;
void set_error(std::string* error, std::string value) {
    if (error != nullptr) {
        *error = std::move(value);
    }
}
std::string first_validation_error(const GraphEffectValidation& validation) {
    const auto iterator = std::find_if(validation.diagnostics.begin(), validation.diagnostics.end(),
                                       [](const GraphEffectDiagnostic& diagnostic) {
        return diagnostic.severity == GraphEffectDiagnosticSeverity::Error;
    });
    return iterator == validation.diagnostics.end() ? "Graph validation failed" : iterator->message;
}
nlohmann::json parameter_to_json(const GraphEffectParameter& parameter) {
    return std::visit([](const auto& value) -> nlohmann::json {
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, bool>) {
            return {{"type", "bool"}, {"value", value}};
        } else if constexpr (std::is_same_v<Value, std::int64_t>) {
            return {{"type", "integer"}, {"value", value}};
        } else if constexpr (std::is_same_v<Value, double>) {
            return {{"type", "number"}, {"value", value}};
        } else if constexpr (std::is_same_v<Value, std::string>) {
            return {{"type", "string"}, {"value", value}};
        } else if constexpr (std::is_same_v<Value, Pixel>) {
            return {{"type", "color"}, {"value", {r(value), g(value), b(value), a(value)}}};
        } else {
            return {{"type", "number-array"}, {"value", value}};
        }
    }, parameter);
}

GraphEffectParameter parameter_from_json(const nlohmann::json& json) {
    const std::string type = json.at("type").get<std::string>();
    const nlohmann::json& value = json.at("value");
    if (type == "bool") return value.get<bool>();
    if (type == "integer") return value.get<std::int64_t>();
    if (type == "number") {
        const double number = value.get<double>();
        if (!std::isfinite(number)) throw std::runtime_error("Graph parameter number is not finite");
        return number;
    }
    if (type == "string") return value.get<std::string>();
    if (type == "color") {
        if (!value.is_array() || value.size() != 4U) throw std::runtime_error("Graph color must have four channels");
        const auto channel = [&value](std::size_t index) {
            const int channel_value = value.at(index).get<int>();
            if (channel_value < 0 || channel_value > 255) {
                throw std::runtime_error("Graph color channel is outside the 0..255 range");
            }
            return static_cast<std::uint8_t>(channel_value);
        };
        return rgba(channel(0), channel(1), channel(2), channel(3));
    }
    if (type == "number-array") {
        std::vector<double> numbers = value.get<std::vector<double>>();
        if (numbers.size() > kMaximumGraphParameters ||
            std::any_of(numbers.begin(), numbers.end(), [](double number) { return !std::isfinite(number); })) {
            throw std::runtime_error("Graph number array is invalid");
        }
        return numbers;
    }
    throw std::runtime_error("Unknown graph parameter type: " + type);
}

nlohmann::json graph_to_json(const GraphEffectGraph& graph) {
    nlohmann::json root;
    root["format"] = kGraphEffectFormat;
    root["version"] = kGraphEffectFormatVersion;
    root["name"] = graph.name;
    root["nodes"] = nlohmann::json::array();
    for (const GraphEffectNode& node : graph.nodes) {
        nlohmann::json parameters = nlohmann::json::object();
        for (const auto& [id, value] : node.parameters) {
            parameters[id] = parameter_to_json(value);
        }
        root["nodes"].push_back({{"id", node.id}, {"type_id", node.type_id},
                                  {"position", {{"x", node.x}, {"y", node.y}}},
                                  {"enabled", node.enabled}, {"parameters", std::move(parameters)}});
    }
    root["links"] = nlohmann::json::array();
    for (const GraphEffectLink& link : graph.links) {
        root["links"].push_back({{"from", {{"node", link.from_node}, {"port", link.from_port}}},
                                  {"to", {{"node", link.to_node}, {"port", link.to_port}}}});
    }
    return root;
}

GraphEffectGraph graph_from_json(const nlohmann::json& root) {
    if (root.at("format").get<std::string>() != kGraphEffectFormat) {
        throw std::runtime_error("Not a PixelArt98 GraphEffect file");
    }
    const int version = root.at("version").get<int>();
    if (version != kGraphEffectFormatVersion) {
        throw std::runtime_error("Unsupported GraphEffect version: " + std::to_string(version));
    }
    GraphEffectGraph graph;
    const auto name_iterator = root.find("name");
    graph.name = name_iterator == root.end()
                     ? "Untitled Graph"
                     : name_iterator->get<std::string>();
    const nlohmann::json& nodes = root.at("nodes");
    const nlohmann::json& links = root.at("links");
    if (!nodes.is_array() || nodes.size() > kMaximumGraphNodes ||
        !links.is_array() || links.size() > kMaximumGraphLinks) {
        throw std::runtime_error("GraphEffect file exceeds structural limits");
    }
    for (const nlohmann::json& node_json : nodes) {
        GraphEffectNode node;
        node.id = node_json.at("id").get<GraphEffectNodeId>();
        node.type_id = node_json.at("type_id").get<std::string>();
        node.x = node_json.at("position").at("x").get<double>();
        node.y = node_json.at("position").at("y").get<double>();
        const auto enabled_iterator = node_json.find("enabled");
        node.enabled = enabled_iterator == node_json.end()
                           ? true
                           : enabled_iterator->get<bool>();
        const auto parameters_iterator = node_json.find("parameters");
        if (parameters_iterator != node_json.end()) {
            const nlohmann::json& parameters = *parameters_iterator;
            if (!parameters.is_object() || parameters.size() > kMaximumGraphParameters) {
                throw std::runtime_error("Graph node has too many parameters");
            }
            for (const auto& [id, parameter_json] : parameters.items()) {
                node.parameters.emplace(id, parameter_from_json(parameter_json));
            }
        }
        graph.nodes.push_back(std::move(node));
    }
    for (const nlohmann::json& link_json : links) {
        GraphEffectLink link;
        link.from_node = link_json.at("from").at("node").get<GraphEffectNodeId>();
        link.from_port = link_json.at("from").at("port").get<std::string>();
        link.to_node = link_json.at("to").at("node").get<GraphEffectNodeId>();
        link.to_port = link_json.at("to").at("port").get<std::string>();
        graph.links.push_back(std::move(link));
    }
    return graph;
}

class BoundedStringBuffer final : public std::streambuf {
public:
    explicit BoundedStringBuffer(std::size_t limit) : limit_(limit) {
        data_.reserve(std::min<std::size_t>(limit, 64U * 1024U));
    }

    [[nodiscard]] bool exceeded() const noexcept { return exceeded_; }
    [[nodiscard]] std::string take() { return std::move(data_); }

protected:
    int_type overflow(int_type character) override {
        if (traits_type::eq_int_type(character, traits_type::eof())) return traits_type::not_eof(character);
        if (data_.size() >= limit_) {
            exceeded_ = true;
            return traits_type::eof();
        }
        data_.push_back(traits_type::to_char_type(character));
        return character;
    }

    std::streamsize xsputn(const char* source, std::streamsize count) override {
        if (count <= 0) return 0;
        const std::size_t requested = static_cast<std::size_t>(count);
        const std::size_t remaining = limit_ - data_.size();
        const std::size_t written = std::min(requested, remaining);
        data_.append(source, written);
        if (written != requested) exceeded_ = true;
        return static_cast<std::streamsize>(written);
    }

private:
    std::size_t limit_ = 0;
    std::string data_;
    bool exceeded_ = false;
};

bool serialize_graph_bounded(const GraphEffectGraph& graph, std::string& serialized, std::string* error) {
    BoundedStringBuffer buffer(static_cast<std::size_t>(kMaximumGraphFileBytes));
    std::ostream stream(&buffer);
    stream << std::setw(2) << graph_to_json(graph) << '\n';
    stream.flush();
    if (buffer.exceeded() || !stream) {
        set_error(error, "GraphEffect serialization exceeds the maximum file size");
        return false;
    }
    serialized = buffer.take();
    return true;
}

void remove_temporary_file(const std::filesystem::path& path) noexcept {
    if (path.empty()) return;
    std::error_code cleanup_error;
    std::filesystem::remove(path, cleanup_error);
}

bool install_graph_file(const std::filesystem::path& temporary,
                        const std::filesystem::path& destination,
                        std::string* error) {
#if defined(_WIN32)
    std::error_code exists_error;
    const bool destination_exists = std::filesystem::exists(destination, exists_error);
    if (exists_error) {
        set_error(error, "Could not inspect GraphEffect destination: " + exists_error.message());
        return false;
    }
    BOOL installed = FALSE;
    if (destination_exists) {
        installed = ReplaceFileW(destination.c_str(), temporary.c_str(), nullptr,
                                 REPLACEFILE_WRITE_THROUGH, nullptr, nullptr);
    } else {
        installed = MoveFileExW(temporary.c_str(), destination.c_str(),
                                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    }
    if (installed == FALSE) {
        const std::error_code windows_error(static_cast<int>(GetLastError()), std::system_category());
        set_error(error, "Could not install GraphEffect file: " + windows_error.message());
        return false;
    }
    return true;
#else
    std::error_code rename_error;
    std::filesystem::rename(temporary, destination, rename_error);
    if (rename_error) {
        set_error(error, "Could not install GraphEffect file: " + rename_error.message());
        return false;
    }
    return true;
#endif
}
} // namespace
bool save_graph_effect(const std::filesystem::path& path,
                       const GraphEffectGraph& graph,
                       std::string* error) {
    if (error != nullptr) error->clear();
    std::filesystem::path temporary;
    try {
        const GraphEffectValidation validation =
            validate_graph_effect(graph, GraphEffectValidationMode::Structural);
        if (!validation.ok()) {
            set_error(error, first_validation_error(validation));
            return false;
        }
        if (path.empty()) {
            set_error(error, "GraphEffect path is empty");
            return false;
        }

        std::string serialized;
        if (!serialize_graph_bounded(graph, serialized, error)) return false;

        static std::atomic<std::uint64_t> sequence{0};
        temporary = path;
        const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        temporary += ".tmp-" + std::to_string(timestamp) + "-" +
                     std::to_string(sequence.fetch_add(1U, std::memory_order_relaxed));

        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            set_error(error, "Could not create temporary GraphEffect file");
            return false;
        }
        file.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
        file.flush();
        const bool write_succeeded = static_cast<bool>(file);
        file.close();
        const bool close_succeeded = static_cast<bool>(file);
        if (!write_succeeded || !close_succeeded) {
            remove_temporary_file(temporary);
            set_error(error, "Could not write GraphEffect file");
            return false;
        }

        if (!install_graph_file(temporary, path, error)) {
            remove_temporary_file(temporary);
            return false;
        }
        return true;
    } catch (const std::exception& exception) {
        remove_temporary_file(temporary);
        set_error(error, std::string("Could not save GraphEffect: ") + exception.what());
        return false;
    } catch (...) {
        remove_temporary_file(temporary);
        set_error(error, "Could not save GraphEffect: unknown error");
        return false;
    }
}

bool load_graph_effect(const std::filesystem::path& path,
                       GraphEffectGraph& graph,
                       std::string* error) {
    if (error != nullptr) error->clear();
    try {
        std::error_code size_error;
        const std::uintmax_t file_size = std::filesystem::file_size(path, size_error);
        if (size_error || file_size > kMaximumGraphFileBytes) {
            set_error(error, size_error ? "Could not inspect GraphEffect file: " + size_error.message()
                                        : "GraphEffect file is too large");
            return false;
        }
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            set_error(error, "Could not open GraphEffect file");
            return false;
        }
        nlohmann::json root;
        file >> root;
        GraphEffectGraph loaded = graph_from_json(root);
        const GraphEffectValidation validation = validate_graph_effect(loaded, GraphEffectValidationMode::Structural);
        if (!validation.ok()) {
            set_error(error, first_validation_error(validation));
            return false;
        }
        graph = std::move(loaded);
        return true;
    } catch (const std::exception& exception) {
        set_error(error, std::string("Could not load GraphEffect: ") + exception.what());
        return false;
    } catch (...) {
        set_error(error, "Could not load GraphEffect: unknown error");
        return false;
    }
}

bool save_graph_effect(const std::string& path,
                       const GraphEffectGraph& graph,
                       std::string* error) {
    return save_graph_effect(std::filesystem::path(path), graph, error);
}

bool load_graph_effect(const std::string& path,
                       GraphEffectGraph& graph,
                       std::string* error) {
    return load_graph_effect(std::filesystem::path(path), graph, error);
}
} // namespace px
