#pragma once
#include <cstdint>
#include <string>
#include <string_view>

#include <fmt/format.h>

namespace vp::core {
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
} // namespace vp::core

/// Lets an AssetID be logged directly, without a cast at every call site
FMT_BEGIN_NAMESPACE

template <> struct formatter<vp::core::AssetID> : formatter<uint64_t> {
    template <typename FormatContext> auto format(vp::core::AssetID id, FormatContext& ctx) const {
        return formatter<uint64_t>::format(static_cast<uint64_t>(id), ctx);
    }
};

FMT_END_NAMESPACE
