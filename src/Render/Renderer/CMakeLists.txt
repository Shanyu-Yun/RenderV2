# ===================================
# Renderer 模块 - 渲染器
# ===================================


# 创建Renderer静态库（聚合所有子模块）
add_library(Renderer STATIC)

# 禁用Qt自动处理
set_target_properties(Renderer PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

# 收集源文件
file(GLOB_RECURSE RENDERER_HEADERS "public/*.hpp")
file(GLOB_RECURSE RENDERER_SOURCES "private/*.cpp")

target_sources(Renderer PRIVATE ${RENDERER_SOURCES})

# 设置包含目录
target_include_directories(Renderer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/public
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_include_directories(Renderer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/private
)

target_link_libraries(ResourceManager
    PUBLIC
        VkCore
)