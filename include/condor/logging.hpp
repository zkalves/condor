/**
 * @file logging.hpp
 * @brief Thread-safe logging facility and convenience logging macros.
 * @author Ezequiel Alves
 * @copyright Copyright (C) 2026 Ezequiel Alves. All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <string>
#include <string_view>
#include <mutex>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace condor {

/**
 * @enum LogLevel
 * @brief Severity levels for log messages emitted by Condor.
 */
enum class LogLevel {
    DEBUG,  ///< Fine-grained diagnostic information enabled via --verbose-logging.
    INFO,   ///< Informational execution milestone messages.
    WARN,   ///< Non-fatal warnings and unexpected recovery conditions.
    ERROR   ///< Critical errors causing execution or configuration failure.
};

/**
 * @class Logger
 * @brief Thread-safe singleton logger with timestamping, level filtering, and mutex synchronization.
 */
class Logger {
public:
    /**
     * @brief Gets the global singleton instance of the Logger.
     * @return Reference to Logger instance.
     */
    static Logger& instance();

    /**
     * @brief Enables or disables verbose debug log emission.
     * @param verbose True to enable debug logging, false to suppress.
     */
    void set_verbose(bool verbose);

    /**
     * @brief Checks if verbose debug logging is enabled.
     * @return True if verbose logging is enabled, false otherwise.
     */
    bool is_verbose() const;

    /**
     * @brief Emits a log entry with specified level and message string.
     * @param level Severity level of message.
     * @param message Text content of log message.
     */
    void log(LogLevel level, std::string_view message);

    /** @brief Emits a debug level log message. */
    void debug(std::string_view message) { log(LogLevel::DEBUG, message); }

    /** @brief Emits an info level log message. */
    void info(std::string_view message)  { log(LogLevel::INFO, message); }

    /** @brief Emits a warning level log message. */
    void warn(std::string_view message)  { log(LogLevel::WARN, message); }

    /** @brief Emits an error level log message. */
    void error(std::string_view message) { log(LogLevel::ERROR, message); }

    /**
     * @brief Formats the current wall-clock time into a standard timestamp string.
     * @return Timestamp formatted as `[YYYY-MM-DD HH:MM:SS.mmm]`.
     */
    static std::string current_timestamp();

private:
    Logger() = default;
    bool verbose_{false};
    mutable std::mutex mutex_;
};

#define CONDOR_LOG_DEBUG(msg) condor::Logger::instance().debug(msg)
#define CONDOR_LOG_INFO(msg)  condor::Logger::instance().info(msg)
#define CONDOR_LOG_WARN(msg)  condor::Logger::instance().warn(msg)
#define CONDOR_LOG_ERROR(msg) condor::Logger::instance().error(msg)

} // namespace condor
