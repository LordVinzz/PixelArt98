#include "ui/SplashScreen.hpp"

#include "core/Pixel.hpp"

#include <imgui.h>
#include <stb_image.h>
#include <vector>

namespace px {

namespace {

void set_error(std::string* error, const std::string& value) {
    if (error != nullptr) {
        *error = value;
    }
}

ImTextureID gl_texture_id(unsigned int id) {
    return static_cast<ImTextureID>(static_cast<unsigned long long>(id));
}

} // namespace

bool SplashScreen::load(std::span<const unsigned char> png_bytes, std::string* error) {
    if (png_bytes.empty()) {
        set_error(error, "Splash art is empty");
        return false;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* decoded =
        stbi_load_from_memory(png_bytes.data(), static_cast<int>(png_bytes.size()), &width, &height, &channels, 4);
    if (decoded == nullptr) {
        set_error(error, "Could not load embedded splash art");
        return false;
    }
    if (width <= 0 || height <= 0) {
        stbi_image_free(decoded);
        set_error(error, "Embedded splash art has invalid dimensions");
        return false;
    }

    std::vector<Pixel> pixels;
    const std::size_t pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    pixels.reserve(pixel_count);
    for (std::size_t index = 0; index < pixel_count; ++index) {
        const std::size_t offset = index * 4;
        pixels.push_back(rgba(decoded[offset], decoded[offset + 1], decoded[offset + 2], decoded[offset + 3]));
    }
    stbi_image_free(decoded);

    texture_.update(width, height, pixels);
    return texture_.id() != 0;
}

void SplashScreen::render_full_window() const {
    if (texture_.id() == 0 || texture_.width() <= 0 || texture_.height() <= 0) {
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    texture_.bind_nearest();
    ImGui::GetBackgroundDrawList()->AddImage(gl_texture_id(texture_.id()),
                                             viewport->Pos,
                                             ImVec2(viewport->Pos.x + viewport->Size.x,
                                                    viewport->Pos.y + viewport->Size.y));
}

} // namespace px
