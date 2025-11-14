#pragma once
#include <memory>

struct Renderer {
    std::unique_ptr<struct VulkanBackend> backend;
};

