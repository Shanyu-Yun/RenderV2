# 创建 MaterialManager 对象库
add_library(MaterialManager OBJECT)

# 禁用Qt自动处理
set_target_properties(MaterialManager PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

# 收集源文件
file(GLOB_RECURSE MATERIAL_MANAGER_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/public/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/public/*.h"
)

file(GLOB_RECURSE MATERIAL_MANAGER_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/private/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/private/*.c"
)


# 添加源文件到目标
target_sources(MaterialManager
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/public
        FILES ${MATERIAL_MANAGER_HEADERS}
    PRIVATE
        ${MATERIAL_MANAGER_SOURCES}
)

# 设置包含目录
target_include_directories(MaterialManager
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/public>
        $<INSTALL_INTERFACE:include/Asset/MaterialManager>
        ${SPIRV_REFLECT_INCLUDE_DIR}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/private
)
find_package(glm REQUIRED)
find_package(nlohmann_json REQUIRED)
# 链接VkCore以便访问其头文件
target_link_libraries(MaterialManager
    PUBLIC
        VkCore
        ResourceManager
        glm::glm
        nlohmann_json::nlohmann_json
        ${SPIRV_REFLECT_LIBRARY}
)

# 输出信息
message(STATUS "MaterialManager module:")
message(STATUS "  Headers: ${MATERIAL_MANAGER_HEADERS}")
message(STATUS "  Sources: ${MATERIAL_MANAGER_SOURCES}")
