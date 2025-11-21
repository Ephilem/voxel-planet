//
// Created by raph on 17/11/25.
//

#include "ShaderLoader.h"

std::shared_ptr<ShaderResource> ShaderLoader::load_typed(const std::string &name) {
    // full file path
    std::string fullFilePath = "assets/shaders/" + name + ".spv";

    // load the file
    FILE* file = fopen(fullFilePath.c_str(), "rb");
    if (!file) {
        throw std::runtime_error("Shader resource loader failed to open file '" + fullFilePath + "'");
    }

    // get file size
    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // read file data
    std::unique_ptr<uint8_t[]> data(new uint8_t[size]);
    size_t bytesRead = fread(data.get(), 1, size, file);
    if (bytesRead != size) {
        throw std::runtime_error("Shader resource loader failed to read file '" + fullFilePath + " the waited size is " + std::to_string(size) + " but read " + std::to_string(bytesRead) + " bytes");
    }
    fclose(file);

    return std::make_shared<ShaderResource>(name, size, std::move(data));
}
