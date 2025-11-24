/**
 * @file Logger.hpp
 * @brief 日志宏定义，根据ENABLE_DEBUG_LOG编译选项控制日志输出
 * @details
 * - 当启用ENABLE_DEBUG_LOG时，日志会输出到std::cout
 * - 当禁用时，宏会被优化掉，不会产生任何代码
 */

#pragma once

#include <iostream>

#ifdef ENABLE_DEBUG_LOG
#define LOG_DEBUG(msg) std::cout << "[DEBUG] " << msg << std::endl
#define LOG_INFO(msg) std::cout << "[INFO] " << msg << std::endl
#define LOG_WARN(msg) std::cout << "[WARN] " << msg << std::endl
#define LOG_ERROR(msg) std::cerr << "[ERROR] " << msg << std::endl
#else
#define LOG_DEBUG(msg) ((void)0)
#define LOG_INFO(msg) ((void)0)
#define LOG_WARN(msg) ((void)0)
#define LOG_ERROR(msg) ((void)0)
#endif
