# ===================================
# Asset 模块 - 资源管理
# ===================================

# 添加子模块（按依赖顺序）
add_subdirectory(ResourceManager)
add_subdirectory(Scene)
add_subdirectory(MaterialManager)

# 创建Asset静态库（聚合所有子模块）
add_library(Asset STATIC)

# 禁用Qt自动处理
set_target_properties(Asset PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

# 链接所有子模块的对象库
target_sources(Asset PRIVATE
    $<TARGET_OBJECTS:ResourceManager>
    $<TARGET_OBJECTS:MaterialManager>
    $<TARGET_OBJECTS:Scene>
)

# 设置包含目录（从子模块继承）
target_include_directories(Asset PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 查找依赖包
find_package(nlohmann_json CONFIG REQUIRED)
find_package(glm REQUIRED)

# 链接子模块和外部依赖
target_link_libraries(Asset 
    PUBLIC
        VkCore
        glm::glm
        Vulkan::Vulkan
        nlohmann_json::nlohmann_json
        ResourceManager
        MaterialManager
        Scene
)

# 注意：stb 是单头文件库
find_path(STB_INCLUDE_DIRS "stb_image.h")
if(STB_INCLUDE_DIRS)
    target_include_directories(Asset PRIVATE ${STB_INCLUDE_DIRS})
    message(STATUS "Found stb_image.h at: ${STB_INCLUDE_DIRS}")
else()
    message(WARNING "stb_image.h not found. Please install via: vcpkg install stb")
endif()

# 创建命名空间别名
add_library(QTRender::Asset ALIAS Asset)
