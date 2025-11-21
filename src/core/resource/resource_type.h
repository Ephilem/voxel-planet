#pragma once
#include <string>
#include <glm/fwd.hpp>

#include "asset_id.h"

enum class ResourceType {
    TEXT,
    BINARY,
    SHADER,
    IMAGE,
};

class IResource {
public:

    virtual ~IResource() = default;

    virtual AssetID get_id() const = 0;
    virtual const std::string& get_name() const = 0;
    virtual size_t get_data_size() const = 0;

};

class ShaderResource : public IResource {
    std::unique_ptr<uint8_t[]> _data{};
    size_t _data_size{0};

public:
    AssetID id;
    std::string name;

    ShaderResource(std::string assetName, size_t dataSize,
                   std::unique_ptr<uint8_t[]> data)
        : name(std::move(assetName)), _data_size(dataSize), _data(std::move(data))
    {
        id = hash_string_runtime(name.c_str());
    }

    AssetID get_id() const override { return id; }
    const std::string& get_name() const override { return name; }
    size_t get_data_size() const override { return _data_size; }

    uint8_t* getData() const { return _data.get(); }
};

class ImageResource : public IResource {
    std::unique_ptr<uint8_t[]> _data{};

public:
    AssetID id;
    std::string name;

    uint32_t width;
    uint32_t height;
    uint8_t channel_count;
    bool has_transparency;

    ImageResource(std::string assetName, uint32_t w, uint32_t h,
                  uint8_t channels, std::unique_ptr<uint8_t[]> data)
        : name(std::move(assetName)), width(w), height(h), _data(std::move(data)), channel_count(channels)
    {
        id = hash_string_runtime(name.c_str());

        has_transparency = false;
        for (size_t i = 0; i < width * height; ++i) {
            if (_data[i * channel_count + 3] < 255) {
                has_transparency = true;
                break;
            }
        }
    }

    AssetID get_id() const override { return id; }
    const std::string& get_name() const override { return name; }
    size_t get_data_size() const override { return width * height * channel_count; }

    uint8_t* getData() const { return _data.get(); }
};
