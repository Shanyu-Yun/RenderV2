# ===================================
# Render Library - 模块化渲染库
# ===================================
# 依赖关系：
# - VkCore: 独立模块（Vulkan核心封装）
# - Asset: 依赖 VkCore（资源管理）
# - Renderer: 依赖 Asset 和 VkCore（渲染器）
# ===================================

# 添加子模块（按依赖顺序）
add_subdirectory(VkCore)
add_subdirectory(Asset)
#add_subdirectory(Renderer)

# 创建Render接口库（聚合所有子模块）
add_library(Render INTERFACE)

# Render库依赖所有子模块
target_link_libraries(Render INTERFACE
    VkCore
    Asset
#    Renderer
)

# 创建命名空间别名
add_library(QTRender::Render ALIAS Render)

# ===================================
# 模块说明
# ===================================
# VkCore  - Vulkan核心功能封装（设备、内存、资源等）
# Asset   - 资源管理（模型、纹理、材质加载与管理）
# Renderer - 渲染器实现（渲染管线、渲染图等）
# ===================================

# 输出编译信息
# ===================================
message(STATUS "Render Library Configuration:")
message(STATUS "  VkCore sources: ${VKCORE_SOURCES}")
message(STATUS "  Asset sources: ${ASSET_SOURCES}")
message(STATUS "  Renderer sources: ${RENDERER_SOURCES}")