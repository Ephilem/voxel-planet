#pragma once
#include "VulkanBackend.h"

class VulkanPipeline {
public:
    VulkanPipeline();
    ~VulkanPipeline();



private:
    VulkanBackend *_backend;
};
