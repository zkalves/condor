/*
 * This file is part of Condor.
 *
 * Condor is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Condor is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Condor.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Copyright (C) 2026 Ezequiel Alves. All rights reserved.
 */

/**
 * @file common.hpp
 * @brief Common types, enumerations, terminal utilities, and exception definitions for Condor.
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
#include <vector>
#include <chrono>
#include <cstdint>
#include <regex>
#include <stdexcept>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cstdlib>
#include <algorithm>

namespace condor {

/**
 * @enum JobStatus
 * @brief Execution lifecycle states for a single job or stage in the DAG.
 */
enum class JobStatus {
    QUEUED,         ///< Waiting for dependencies or available scheduler slot.
    RUNNING,        ///< Currently executing under a backend (local process or grid batch).
    PASSED,         ///< Completed execution with zero exit code (or custom pass criteria).
    FAILED,         ///< Completed with non-zero exit code and failure was not allowed.
    FAILED_ALLOWED, ///< Completed with non-zero exit code but marked allow_failure: true.
    SKIPPED,        ///< Skipped due to unmet enabled_if condition or upstream failure barrier.
    ABORTED,        ///< Canceled or killed prematurely by scheduler or user signal.
    TIMEOUT         ///< Terminated because runtime duration exceeded configured timeout.
};

/**
 * @brief Converts a JobStatus enum to a human-readable string.
 * @param status Job status enumeration value.
 * @return String representation suitable for console output.
 */
inline std::string to_string(JobStatus status) {
    switch (status) {
        case JobStatus::QUEUED:         return "QUEUED";
        case JobStatus::RUNNING:        return "RUNNING";
        case JobStatus::PASSED:         return "PASSED";
        case JobStatus::FAILED:         return "FAILED";
        case JobStatus::FAILED_ALLOWED: return "FAILED (ALLOWED)";
        case JobStatus::SKIPPED:        return "SKIPPED";
        case JobStatus::ABORTED:        return "ABORTED";
        case JobStatus::TIMEOUT:        return "TIMEOUT";
    }
    return "UNKNOWN";
}

/**
 * @enum StageExecutionMode
 * @brief Execution mode for a stage during whole-pipeline or selective rerun execution.
 */
enum class StageExecutionMode {
    FULL,        ///< Execute all jobs in stage normally with current configuration.
    FAILED_ONLY, ///< Re-run only jobs that failed or aborted in the base run; cache passed jobs.
    CACHED       ///< Treat all jobs as passed/cached from the base run.
};

inline StageExecutionMode parse_stage_execution_mode(std::string_view mode) {
    if (mode == "full") return StageExecutionMode::FULL;
    if (mode == "failed_only" || mode == "failed" || mode == "failures") return StageExecutionMode::FAILED_ONLY;
    if (mode == "cached" || mode == "skip" || mode == "skipped") return StageExecutionMode::CACHED;
    throw std::runtime_error("Invalid stage execution mode: '" + std::string(mode) + "'. Must be full, failed_only, or cached.");
}

inline std::string to_string(StageExecutionMode mode) {
    switch (mode) {
        case StageExecutionMode::FULL:        return "full";
        case StageExecutionMode::FAILED_ONLY: return "failed_only";
        case StageExecutionMode::CACHED:      return "cached";
    }
    return "full";
}

/**
 * @brief Parses a timeout duration string (e.g. "30s", "10m", "2h", "1800") into seconds.
 * @param str Duration string.
 * @return Duration in seconds (0 = no timeout).
 */
inline int parse_timeout_seconds(std::string_view str) {
    if (str.empty()) return 0;
    std::string s(str);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    if (s.empty()) return 0;

    char unit = s.back();
    int multiplier = 1;
    if (unit == 's' || unit == 'S') {
        multiplier = 1;
        s.pop_back();
    } else if (unit == 'm' || unit == 'M') {
        multiplier = 60;
        s.pop_back();
    } else if (unit == 'h' || unit == 'H') {
        multiplier = 3600;
        s.pop_back();
    } else if (unit == 'd' || unit == 'D') {
        multiplier = 86400;
        s.pop_back();
    }

    try {
        int val = std::stoi(s);
        return (val > 0) ? (val * multiplier) : 0;
    } catch (...) {
        return 0;
    }
}

/**
 * @enum FailureAction
 * @brief Policy determining pipeline behavior when an unexpected job failure occurs.
 */
enum class FailureAction {
    ABORT,         ///< Immediate abort: cancel running jobs and stop scheduling new jobs.
    CONTINUE,      ///< Continue scheduling all remaining independent DAG nodes.
    STAGE_BARRIER  ///< Complete the current stage but do not start subsequent dependent stages.
};

/**
 * @brief Parses a string representation of a failure policy into a FailureAction enum.
 * @param action String view ("abort", "continue", or "stage_barrier").
 * @return Parsed FailureAction enum.
 * @throws std::runtime_error If action string is unrecognized.
 */
inline FailureAction parse_failure_action(std::string_view action) {
    if (action == "abort") return FailureAction::ABORT;
    if (action == "continue") return FailureAction::CONTINUE;
    if (action == "stage_barrier") return FailureAction::STAGE_BARRIER;
    throw std::runtime_error("Invalid on_failure_action: '" + std::string(action) + "'. Must be abort, continue, or stage_barrier.");
}

/**
 * @brief Converts a FailureAction enum into its canonical YAML string representation.
 * @param action FailureAction enum value.
 * @return String representation ("abort", "continue", or "stage_barrier").
 */
inline std::string to_string(FailureAction action) {
    switch (action) {
        case FailureAction::ABORT:         return "abort";
        case FailureAction::CONTINUE:      return "continue";
        case FailureAction::STAGE_BARRIER: return "stage_barrier";
    }
    return "unknown";
}

/**
 * @enum OutputMode
 * @brief Terminal verbosity and filtering mode for job execution events.
 */
enum class OutputMode {
    ALL,       ///< Emit console events for all job dispatches, starts, and completions.
    FAILURES,  ///< Emit events only for failed jobs to eliminate terminal noise.
    SUMMARY    ///< Suppress per-job events, showing only overall progress bar and final summary.
};

/**
 * @brief Parses a string into an OutputMode enumeration value.
 * @param mode String view ("all", "failures", or "summary").
 * @return Parsed OutputMode enum.
 * @throws std::runtime_error If mode string is unrecognized.
 */
inline OutputMode parse_output_mode(std::string_view mode) {
    if (mode == "all") return OutputMode::ALL;
    if (mode == "failures" || mode == "failures_only" || mode == "failed") return OutputMode::FAILURES;
    if (mode == "summary" || mode == "summary_only") return OutputMode::SUMMARY;
    throw std::runtime_error("Invalid output-mode: '" + std::string(mode) +
                             "'. Must be all, failures, or summary.");
}

/**
 * @brief Converts an OutputMode enum into its canonical string representation.
 * @param mode OutputMode enum value.
 * @return String representation ("all", "failures", or "summary").
 */
inline std::string to_string(OutputMode mode) {
    switch (mode) {
        case OutputMode::ALL:      return "all";
        case OutputMode::FAILURES: return "failures";
        case OutputMode::SUMMARY:  return "summary";
    }
    return "all";
}

/**
 * @enum PathStyle
 * @brief Configures whether exported environment variables and paths are absolute or relative.
 */
enum class PathStyle {
    ABSOLUTE,  ///< Expand all environment and workspace paths to canonical absolute paths.
    RELATIVE   ///< Preserve paths relative to the pipeline configuration directory.
};

/**
 * @brief Parses a string into a PathStyle enumeration value.
 * @param style String view ("absolute", "abs", "relative", or "rel").
 * @return Parsed PathStyle enum.
 * @throws std::runtime_error If style string is unrecognized.
 */
inline PathStyle parse_path_style(std::string_view style) {
    if (style == "absolute" || style == "abs") return PathStyle::ABSOLUTE;
    if (style == "relative" || style == "rel") return PathStyle::RELATIVE;
    throw std::runtime_error("Invalid path_style: '" + std::string(style) +
                             "'. Must be 'absolute' or 'relative'.");
}

/**
 * @brief Converts a PathStyle enum into its canonical string representation.
 * @param style PathStyle enum value.
 * @return String representation ("absolute" or "relative").
 */
inline std::string to_string(PathStyle style) {
    switch (style) {
        case PathStyle::ABSOLUTE: return "absolute";
        case PathStyle::RELATIVE: return "relative";
    }
    return "absolute";
}

/**
 * @brief Dynamically detects current terminal window width via ioctl or COLUMNS environment.
 * @param default_width Fallback width in characters if detection fails (default: 80).
 * @return Detected or fallback column count.
 */
inline int get_terminal_width(int default_width = 80) {
    struct winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        return static_cast<int>(ws.ws_col);
    }
    if (ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        return static_cast<int>(ws.ws_col);
    }
    const char* col_env = std::getenv("COLUMNS");
    if (col_env) {
        int c = std::atoi(col_env);
        if (c > 0) return c;
    }
    return default_width;
}

/**
 * @brief Validates that a string conforms to POSIX-safe identifier rules (^[a-zA-Z0-9_-]+$).
 * @param str String identifier to test.
 * @return True if valid POSIX identifier, false otherwise.
 */
inline bool is_valid_identifier(std::string_view str) {
    if (str.empty()) return false;
    for (char c : str) {
        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

/**
 * @class ConfigurationError
 * @brief Exception thrown when pipeline YAML specification, schema, or DAG cycle validation fails.
 */
class ConfigurationError : public std::runtime_error {
public:
    explicit ConfigurationError(const std::string& msg) : std::runtime_error(msg) {}
};

/**
 * @class ExecutionError
 * @brief Exception thrown when process spawning, backend communication, or workspace operations fail.
 */
class ExecutionError : public std::runtime_error {
public:
    explicit ExecutionError(const std::string& msg) : std::runtime_error(msg) {}
};

} // namespace condor
