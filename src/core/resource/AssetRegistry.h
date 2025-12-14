#pragma once
#include <unordered_map>
#include <string>
#include <filesystem>
#include <optional>

#include "asset_id.h"
#include "resource_type.h"

struct AssetMetadata {
    std::string path;
    std::string fullPath;
    ResourceType type;
    size_t fileSize = 0;

    AssetMetadata() = default;
    AssetMetadata(std::string p, std::string fp, ResourceType t, size_t size = 0)
        : path(std::move(p)), fullPath(std::move(fp)), type(t), fileSize(size) {}
};

/**
 * Central registry for all game assets.
 * Scans the assets directory and creates a mapping from AssetID to asset metadata
 * This allows the rest of the engine to reference assets by their hash ID
 */
class AssetRegistry {
public:
    AssetRegistry() {
        scan_assets("assets");
    }
    ~AssetRegistry() = default;

    /**
     * Scans a directory recursively and registers all found assets
     * @param assetDir Root assets directory to scan
     * @param namespaceName Namespace prefix for asset IDs
     */
    void scan_assets(const std::filesystem::path& assetDir, const std::string& namespaceName = "voxelplanet");
    void register_asset(AssetID id, const std::string& path, const std::string& fullPath, ResourceType type);

    const AssetMetadata* get_metadata(AssetID id) const;

    std::string get_path(AssetID id) const;
    std::string get_relative_path(AssetID id) const;
    std::optional<ResourceType> get_type(AssetID id) const;
    bool has_asset(AssetID id) const;
    size_t get_asset_count() const { return m_registry.size(); }

    std::string get_debug_name(AssetID id) const;

private:
    static ResourceType get_type_from_extension(const std::string& extension);
    static std::string build_asset_name(const std::string& relativePath, const std::string& namespaceName);

    std::unordered_map<AssetID, AssetMetadata> m_registry;
    std::unordered_map<AssetID, std::string> m_debugNames;
};