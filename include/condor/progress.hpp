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
 * @file progress.hpp
 * @brief Dynamic ANSI progress display, stage status bars, and CI event streaming.
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
#include <vector>
#include <chrono>
#include <mutex>
#include "condor/common.hpp"

namespace condor {

/**
 * @struct ProgressStats
 * @brief Aggregate counts and metrics across all jobs in the pipeline.
 */
struct ProgressStats {
    size_t total_jobs{0};    ///< Total number of jobs across all stages.
    size_t queued_jobs{0};   ///< Count of jobs awaiting dispatch.
    size_t running_jobs{0};  ///< Count of currently executing jobs.
    size_t passed_jobs{0};   ///< Count of successfully completed jobs.
    size_t failed_jobs{0};   ///< Count of failed jobs.
    size_t skipped_jobs{0};  ///< Count of skipped jobs.
    double elapsed_sec{0.0}; ///< Total pipeline elapsed time in seconds.
};

/**
 * @struct ActiveJobInfo
 * @brief Real-time status snippet for an active job displayed in the interactive live HUD.
 */
struct ActiveJobInfo {
    std::string job_id;         ///< Unique 4-digit job identifier.
    std::string stage_name;     ///< Parent stage identifier.
    std::string job_name;       ///< Human-readable job name.
    std::string pid_or_grid_id; ///< Process PID or cluster job ID.
    double elapsed_sec{0.0};    ///< Runtime duration of this specific job in seconds.
};

/**
 * @struct StageSummaryInfo
 * @brief Aggregate metrics for an individual stage displayed in progress bars and status dumps.
 */
struct StageSummaryInfo {
    std::string stage_name; ///< Stage name.
    size_t passed{0};       ///< Count of passed jobs in this stage.
    size_t total{0};        ///< Total jobs configured in this stage.
    size_t running{0};      ///< Count of running jobs in this stage.
    size_t failed{0};       ///< Count of failed jobs in this stage.
    size_t skipped{0};      ///< Count of skipped jobs in this stage.
};

/**
 * @class ProgressDisplay
 * @brief Multi-mode progress renderer supporting interactive in-place ANSI HUD and non-interactive CI streaming.
 *
 * In interactive terminal mode, the display renders dynamic progress bars, stage completion fractions,
 * and active job tables with in-place ANSI line rewrites (`\033[A\033[2K`). In CI mode (`--no-interactive`),
 * it streams event logs and periodic tabular status updates without terminal escape codes.
 */
class ProgressDisplay {
public:
    /**
     * @brief Constructs a ProgressDisplay configured for interactive or non-interactive mode.
     * @param interactive If true, use ANSI cursor rewrites; if false, stream clean text lines.
     * @param status_interval_sec Periodic status dump frequency in seconds for non-interactive mode.
     */
    ProgressDisplay(bool interactive, int status_interval_sec = 30);

    /**
     * @brief Destructor ensuring terminal cursor and states are restored.
     */
    ~ProgressDisplay();

    /**
     * @brief Sets display pause status to indicate paused scheduler in HUD.
     * @param paused True if pipeline dispatching is paused.
     */
    void set_paused(bool paused);

    /**
     * @brief Checks if progress display is in paused state.
     * @return True if paused, false otherwise.
     */
    bool is_paused() const;

    /**
     * @brief Sets the terminal output verbosity filter.
     * @param mode OutputMode enum (ALL, FAILURES, or SUMMARY).
     */
    void set_output_mode(OutputMode mode) { output_mode_ = mode; }

    /**
     * @brief Gets current output verbosity filter.
     * @return Current OutputMode enum.
     */
    OutputMode output_mode() const { return output_mode_; }

    /**
     * @brief Updates the live progress display with latest pipeline metrics.
     * @param stats Overall pipeline statistics.
     * @param stages Per-stage summary metrics.
     * @param active_jobs List of currently running jobs.
     */
    void update(const ProgressStats& stats,
                const std::vector<StageSummaryInfo>& stages,
                const std::vector<ActiveJobInfo>& active_jobs);

    /**
     * @brief Emits a job-started event log line.
     * @param stage_name Parent stage identifier.
     * @param job_id Job identifier.
     * @param job_name Human-readable job name.
     */
    void on_job_started(std::string_view stage_name, std::string_view job_id, std::string_view job_name);

    /**
     * @brief Emits a job-finished event log line.
     * @param stage_name Parent stage identifier.
     * @param job_id Job identifier.
     * @param job_name Human-readable job name.
     * @param status Final job status.
     * @param exit_code Final exit code.
     * @param elapsed_sec Execution duration in seconds.
     */
    void on_job_finished(std::string_view stage_name, std::string_view job_id, std::string_view job_name,
                         JobStatus status, int exit_code, double elapsed_sec);

    /**
     * @brief Prints final pipeline summary banner upon execution completion.
     * @param stats Final pipeline statistics.
     * @param success Overall pipeline success flag.
     * @param html_report_path Filesystem path to generated HTML report file.
     */
    void print_final_summary(const ProgressStats& stats, bool success, const std::string& html_report_path = "");

    /**
     * @brief Formats seconds into human-readable duration (`HH:MM:SS` or `MM:SS.s`).
     * @param seconds Duration in fractional seconds.
     * @return Formatted time string.
     */
    static std::string format_duration(double seconds);

private:
    void render_interactive(const ProgressStats& stats,
                            const std::vector<StageSummaryInfo>& stages,
                            const std::vector<ActiveJobInfo>& active_jobs);
    void render_non_interactive(const ProgressStats& stats,
                                const std::vector<StageSummaryInfo>& stages);

    bool interactive_{false};
    int status_interval_sec_{30};
    OutputMode output_mode_{OutputMode::ALL};
    bool paused_{false};
    int last_lines_rendered_{0};
    std::chrono::steady_clock::time_point last_dump_time_;
    mutable std::mutex mutex_;
};

} // namespace condor
