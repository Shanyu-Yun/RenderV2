/**
 * @file VmaImplementation.cpp
 * @brief Vulkan Memory Allocator 实现文件
 * @details 定义VMA_IMPLEMENTATION以包含VMA的实现代码
 */

#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vk_mem_alloc.h>

// 定义Vulkan-Hpp的默认分发加载器
#include <vulkan/vulkan.hpp>
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
