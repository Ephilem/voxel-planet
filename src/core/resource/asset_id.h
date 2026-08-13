#pragma once
#include <cstdint>
#include <string>
#include <string_view>

#include <fmt/format.h>

constexpr uint64_t hash_fnv1a(std::string_view str) noexcept {
    uint64_t hash = 14695981039346656037ull;
    for (const char c : str) {
        hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
        hash *= 1099511628211ull;
    }
    return hash;
}

enum class AssetID : uint64_t { Invalid = 0 };

inline AssetID hash_string_runtime(std::string_view str) noexcept {
    return static_cast<AssetID>(hash_fnv1a(str));
}

/**
 * Method that get the string value from a namespace string.
 * @return The value, or the whole string when there is no namespace.
 */
constexpr std::string_view asset_id_get_value(std::string_view namespaceString) noexcept {
    const size_t colonPos = namespaceString.find(':');
    if (colonPos == std::string_view::npos) {
        return namespaceString;
    }
    return namespaceString.substr(colonPos + 1);
}

/**
 * Method that get the namespace from a namespace string. Returns an empty view if not found.
 * @return The namespace.
 */
constexpr std::string_view asset_id_get_namespace(std::string_view namespaceString) noexcept {
    const size_t colonPos = namespaceString.find(':');
    if (colonPos == std::string_view::npos) {
        return {};
    }
    return namespaceString.substr(0, colonPos);
}

struct NamedAssetID {
    std::string_view namespaceString;
    AssetID id = AssetID::Invalid;

    constexpr NamedAssetID(std::string_view namespaceString, AssetID id) noexcept
        : namespaceString(namespaceString), id(id) {}

    operator AssetID() const { return id; }
};

constexpr NamedAssetID operator"" _asset(const char* str, size_t len) noexcept {
    const std::string_view sv{str, len};
    return NamedAssetID{sv, static_cast<AssetID>(hash_fnv1a(sv))};
}

/// Lets an AssetID be logged directly, without a cast at every call site
FMT_BEGIN_NAMESPACE

template <> struct formatter<AssetID> : formatter<uint64_t> {
    template <typename FormatContext> auto format(AssetID id, FormatContext& ctx) const {
        return formatter<uint64_t>::format(static_cast<uint64_t>(id), ctx);
    }
};

FMT_END_NAMESPACE
