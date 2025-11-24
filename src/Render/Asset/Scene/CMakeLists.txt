# ===================================
# Scene 子模块
# ===================================

# 创建 Scene 对象库
add_library(Scene OBJECT)

# 禁用Qt自动处理
set_target_properties(Scene PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

# 收集源文件
file(GLOB_RECURSE SCENE_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/public/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/public/*.h"
)

file(GLOB_RECURSE SCENE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/private/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/private/*.c"
)

# 添加源文件到目标
target_sources(Scene
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/public
        FILES ${SCENE_HEADERS}
    PRIVATE
        ${SCENE_SOURCES}
)

# 设置包含目录
target_include_directories(Scene
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/public>
        $<INSTALL_INTERFACE:include/Asset/Scene>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/private
)

# 查找依赖
find_package(glm REQUIRED)

# 链接依赖模块以便访问其头文件
target_link_libraries(Scene
    PUBLIC
        VkCore
        glm::glm
        ResourceManager
)

# 输出信息
message(STATUS "Scene module:")
message(STATUS "  Headers: ${SCENE_HEADERS}")
message(STATUS "  Sources: ${SCENE_SOURCES}")
