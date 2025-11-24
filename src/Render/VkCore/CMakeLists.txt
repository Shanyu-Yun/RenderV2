# ===================================
# VkCore 模块 - Vulkan核心封装
# ===================================

# 创建VkCore静态库
add_library(VkCore STATIC)

# 禁用Qt自动处理
set_target_properties(VkCore PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

# 收集源文件
file(GLOB_RECURSE VKCORE_HEADERS "public/*.hpp")
file(GLOB_RECURSE VKCORE_SOURCES "private/*.cpp")

target_sources(VkCore PRIVATE ${VKCORE_SOURCES})

# 设置包含目录
target_include_directories(VkCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/public
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_include_directories(VkCore PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/private
)

# 链接依赖
find_package(Vulkan REQUIRED)
find_package(glm REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)

target_link_libraries(VkCore PUBLIC
    Vulkan::Vulkan
    glm::glm
    GPUOpen::VulkanMemoryAllocator
)

# 添加编译定义
target_compile_definitions(VkCore PUBLIC
    VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
)

# 创建命名空间别名
add_library(QTRender::VkCore ALIAS VkCore)