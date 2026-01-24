#pragma once
#include <string>

constexpr uint64_t hash_fnv1a(const char* str) {
    uint64_t hash = 14695981039346656037ull;
    while (*str) {
        hash ^= static_cast<uint64_t>(*str++);
        hash *= 1099511628211ull;
    }
    return hash;
}

using AssetID = uint64_t;

constexpr AssetID operator"" _asset(const char* str, size_t) {
    return hash_fnv1a(str);
}

inline AssetID hash_string_runtime(const char* str) {
    uint64_t hash = 14695981039346656037ull;
    while (*str) {
        hash ^= static_cast<uint64_t>(*str++);
        hash *= 1099511628211ull;
    }
    return hash;
}

/**
 * Method that get the string value from a namespace string. Returns empty string if not found.
 * @return The string value.
 */
inline std::string asset_id_get_value(std::string& namespaceString) {
    size_t colonPos = namespaceString.find(':');
    if (colonPos != std::string::npos && colonPos + 1 < namespaceString.length()) {
        return namespaceString.substr(colonPos + 1);
    }
    return "";
}

/**
 * Method that get the namespace from a namespace string. Returns empty string if not found.
 * @return The namespace.
 */
inline std::string asset_id_get_namespace(std::string& namespaceString) {
    size_t colonPos = namespaceString.find(':');
    if (colonPos != std::string::npos) {
        return namespaceString.substr(0, colonPos);
    }
    return "";
}