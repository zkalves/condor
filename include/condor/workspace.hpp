/**
 * @file workspace.hpp
 * @brief Run directory management, stage/job directory creation, manifest generation, and symlink maintenance.
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
#include <filesystem>
#include <vector>
#include <mutex>
#include "condor/common.hpp"

namespace condor {

/**
 * @struct JobMetadata
 * @brief Metadata written to `job_status.txt` in a job's execution directory upon status transitions.
 */
struct JobMetadata {
    std::string job_id;            ///< Unique 4-digit job identifier (e.g. "0001").
    std::string job_name;          ///< Human-readable job name.
    std::string stage_name;        ///< Parent stage identifier.
    std::string pid_or_grid_id;    ///< Process PID or cluster job ID.
    JobStatus status{JobStatus::QUEUED}; ///< Current job status.
    int exit_code{0};              ///< Final exit code recorded upon completion.
    double elapsed_sec{0.0};       ///< Job duration in seconds.
    std::string working_directory; ///< Filesystem working directory where job was executed.
};

/**
 * @struct StageManifestEntry
 * @brief Individual job summary entry recorded in `stage_manifest.txt` within each stage directory.
 */
struct StageManifestEntry {
    std::string job_id;                  ///< Unique 4-digit job identifier.
    std::string job_name;                ///< Human-readable job name.
    std::string command;                 ///< Command executed.
    JobStatus status{JobStatus::QUEUED}; ///< Final status (PASSED, FAILED, SKIPPED).
};

/**
 * @class WorkspaceManager
 * @brief Manages pipeline execution directories, runner scripts, manifest files, and the `latest` pointer symlink.
 *
 * Enforces a strict, reproducible execution folder layout:
 * @code
 * condor_runs/
 * ├── latest -> 20261026_143022_948271
 * └── 20261026_143022_948271/
 *     ├── pipeline_report.html
 *     ├── compile/
 *     │   ├── stage_manifest.txt
 *     │   └── 0001/
 *     │       ├── runner.sh
 *     │       ├── job.log
 *     │       └── job_status.txt
 *     └── sim/
 * @endcode
 */
class WorkspaceManager {
public:
    /**
     * @brief Constructs a WorkspaceManager configured with base directory and optional fixed run directory.
     * @param base_output_dir Root directory where run directories are created (default: "./condor_runs").
     * @param fixed_run_dir If non-empty, use this exact folder path instead of timestamped random folder name.
     */
    explicit WorkspaceManager(std::string_view base_output_dir = "./condor_runs",
                              std::string_view fixed_run_dir = "");

    /**
     * @brief Initializes execution directories and creates or updates the `latest` symlink.
     */
    void initialize();

    /**
     * @brief Gets the base output directory path.
     * @return Reference to base directory path.
     */
    const std::filesystem::path& base_dir() const { return base_dir_; }

    /**
     * @brief Gets the active run directory path.
     * @return Reference to active run directory path.
     */
    const std::filesystem::path& run_dir() const { return run_dir_; }

    /**
     * @brief Gets the filesystem path for a specific stage directory.
     * @param stage_name Identifier of stage.
     * @return Path to `<run_dir>/<stage_name>`.
     */
    std::filesystem::path stage_dir(std::string_view stage_name) const;

    /**
     * @brief Gets the filesystem path for a specific job directory.
     * @param stage_name Identifier of parent stage.
     * @param job_id Unique 4-digit job ID.
     * @return Path to `<run_dir>/<stage_name>/<job_id>`.
     */
    std::filesystem::path job_dir(std::string_view stage_name, std::string_view job_id) const;

    /**
     * @brief Creates a stage directory on the filesystem if it does not already exist.
     * @param stage_name Identifier of stage to create.
     */
    void create_stage_dir(std::string_view stage_name);

    /**
     * @brief Creates a job directory and initializes empty log files.
     * @param stage_name Identifier of parent stage.
     * @param job_id Unique 4-digit job ID.
     */
    void create_job_dir(std::string_view stage_name, std::string_view job_id);

    /**
     * @brief Writes or updates `stage_manifest.txt` in `<run_dir>/<stage_name>/`.
     * @param stage_name Identifier of stage.
     * @param entries List of stage manifest entries.
     */
    void update_stage_manifest(std::string_view stage_name,
                               const std::vector<StageManifestEntry>& entries);

    /**
     * @brief Writes `job_status.txt` in `<run_dir>/<stage_name>/<job_id>/`.
     * @param stage_name Identifier of stage.
     * @param meta Job metadata record.
     */
    void write_job_status(std::string_view stage_name, const JobMetadata& meta);

    /**
     * @brief Atomically creates or updates the `latest` symlink in base_dir pointing to run_dir.
     */
    void update_latest_symlink();

    /**
     * @brief Generates a timestamped execution folder name formatted as `YYYYMMDD_HHMMSS_<random6>`.
     * @return Generated folder name string.
     */
    static std::string generate_run_folder_name();

private:
    std::filesystem::path base_dir_;
    std::filesystem::path run_dir_;
    bool is_fixed_{false};
    mutable std::mutex manifest_mutex_;
};

} // namespace condor
