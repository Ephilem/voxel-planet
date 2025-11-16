//
// Created by raph on 16/11/2025.
//

#include "ImageLoader.h"

#define STB_IMAGE_IMPLEMENTATION
#include <cstring>
#include <stb_image.h>

std::shared_ptr<ImageResource> ImageLoader::load_typed(const std::string &name) {
    const int requiredChannelCount = 4;
    stbi_set_flip_vertically_on_load(true);

    std::string fullFilePath = "assets/" + name + ".png";

    int width, height, channelCount;
    uint8_t* data = stbi_load(fullFilePath.c_str(), &width, &height, &channelCount, requiredChannelCount);

    const char* fail_reason = stbi_failure_reason();
    if (fail_reason) {
        stbi__err(0, 0);

        if (data) {
            stbi_image_free(data);
        }
        throw std::runtime_error("Image resource loader failed to load file '" + fullFilePath + "' with error: " + std::string(fail_reason));
    }

    if (!data) {
        throw std::runtime_error("Image resource loader failed to load file '" + fullFilePath + "' (no data)");
    }

    // Copy data into a unique_ptr for automatic memory management
    size_t data_size = static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(requiredChannelCount);
    std::unique_ptr<uint8_t[]> data_ptr(new uint8_t[data_size]);
    std::memcpy(data_ptr.get(), data, data_size);

    // Free the original stb image data
    stbi_image_free(data);

    return std::make_shared<ImageResource>(name, static_cast<uint32_t>(width), static_cast<uint32_t>(height), requiredChannelCount, std::move(data_ptr));
}
