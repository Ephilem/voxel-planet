#include "AssetRegistry.h"
#include "core/log/Logger.h"

#include <algorithm>

void AssetRegistry::scan_assets(const std::filesystem::path& assetDir, const std::string& namespaceName) {
    if (!std::filesystem::exists(assetDir)) {
        LOG_ERROR("AssetRegistry", "Asset directory does not exist: {}", assetDir.string());
        return;
    }

    if (!std::filesystem::is_directory(assetDir)) {
        LOG_ERROR("AssetRegistry", "Asset path is not a directory: {}", assetDir.string());
        return;
    }

    LOG_INFO("AssetRegistry", "Scanning assets in: {}", assetDir.string());

    size_t assetCount = 0;
    try {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(assetDir)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            const auto& filePath = entry.path();
            std::string extension = filePath.extension().string();

            if (extension.empty() || filePath.filename().string()[0] == '.') {
                continue;
            }

            std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

            ResourceType type = get_type_from_extension(extension);

            std::filesystem::path relativePath = std::filesystem::relative(filePath, assetDir);

            std::string relativePathStr = relativePath.string();
            size_t lastDot = relativePathStr.find_last_of('.');
            if (lastDot != std::string::npos) {
                relativePathStr = relativePathStr.substr(0, lastDot);
            }

            std::replace(relativePathStr.begin(), relativePathStr.end(), '\\', '/');

            std::string assetName = build_asset_name(relativePathStr, namespaceName);
            AssetID assetID = hash_string_runtime(assetName.c_str());

            size_t fileSize = std::filesystem::file_size(filePath);
            m_registry[assetID] = AssetMetadata(relativePathStr, filePath.string(), type, fileSize);

            m_debugNames[assetID] = assetName;

            assetCount++;
        }
    } catch (const std::filesystem::filesystem_error& e) {
        LOG_ERROR("AssetRegistry", "Filesystem error while scanning: {}", e.what());
    }

    LOG_INFO("AssetRegistry", "Registered {} assets", assetCount);
}

void AssetRegistry::register_asset(AssetID id, const std::string& path, const std::string& fullPath, ResourceType type) {
    if (m_registry.find(id) != m_registry.end()) {
        LOG_WARN("AssetRegistry", "Asset ID {} already registered, overwriting", id);
    }

    m_registry[id] = AssetMetadata(path, fullPath, type);
}

const AssetMetadata* AssetRegistry::get_metadata(AssetID id) const {
    auto it = m_registry.find(id);
    if (it != m_registry.end()) {
        return &it->second;
    }
    return nullptr;
}

std::string AssetRegistry::get_path(AssetID id) const {
    auto it = m_registry.find(id);
    if (it != m_registry.end()) {
        return it->second.fullPath;
    }
    return "";
}

std::string AssetRegistry::get_relative_path(AssetID id) const {
    auto it = m_registry.find(id);
    if (it != m_registry.end()) {
        return it->second.path;
    }
    return "";
}

std::optional<ResourceType> AssetRegistry::get_type(AssetID id) const {
    auto it = m_registry.find(id);
    if (it != m_registry.end()) {
        return it->second.type;
    }
    return std::nullopt;
}

bool AssetRegistry::has_asset(AssetID id) const {
    return m_registry.find(id) != m_registry.end();
}

std::string AssetRegistry::get_debug_name(AssetID id) const {
    auto it = m_debugNames.find(id);
    if (it != m_debugNames.end()) {
        return it->second;
    }
    return "";
}

ResourceType AssetRegistry::get_type_from_extension(const std::string& extension) {
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
        extension == ".bmp" || extension == ".tga") {
        return ResourceType::IMAGE;
    }

    if (extension == ".spv" || extension == ".glsl" || extension == ".vert" ||
        extension == ".frag" || extension == ".comp") {
        return ResourceType::SHADER;
    }

    if (extension == ".txt" || extension == ".json" || extension == ".xml") {
        return ResourceType::TEXT;
    }

    return ResourceType::BINARY;
}

std::string AssetRegistry::build_asset_name(const std::string& relativePath, const std::string& namespaceName) {
    return namespaceName + ":" + relativePath;
}