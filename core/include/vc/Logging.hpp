#pragma once

/**
 * @file Logging.hpp
 *
 * @ingroup Util
 */

#include <spdlog/spdlog.h>

#include <filesystem>
/**
 * @namespace vc::logging
 * @brief Logging utilities
 */
namespace vc
{
namespace logging
{
/**
 * @brief Add a log file output to the logger
 *
 * @throw spdlog::spdlog_ex
 */
void AddLogFile(const std::filesystem::path& path);

/** @brief Set the logging level */
void SetLogLevel(const std::string& s);
}  // namespace logging

/** @brief Volume Cartographer global logger */
auto Logger() -> std::shared_ptr<spdlog::logger>;

}  // namespace vc
