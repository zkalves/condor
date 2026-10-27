/**
 * @file config.hpp
 * @brief Configuration data structures and pipeline models parsed from YAML specifications.
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
#include <unordered_map>
#include <optional>
#include "condor/common.hpp"
#include "condor/variable.hpp"

namespace condor {

/**
 * @enum BackendType
 * @brief Backend execution category: local POSIX process group or remote template-based grid batch.
 */
enum class BackendType {
    LOCAL,     ///< Direct local POSIX process execution using fork/execvp and dedicated process groups.
    TEMPLATE   ///< Remote cluster or grid execution via parameterized shell command templates (e.g. Slurm, SGE, LSF).
};

inline std::string to_string(BackendType type) {
    switch (type) {
        case BackendType::LOCAL: return "local";
        case BackendType::TEMPLATE: return "template";
    }
    return "unknown";
}

/**
 * @struct BackendConfig
 * @brief Definition of an execution backend, including command templates for submission, polling, and cancellation.
 */
struct BackendConfig {
    std::string name{"local"};             ///< Unique name of backend identifier.
    BackendType type{BackendType::LOCAL};   ///< Backend execution type.
    std::string description;                ///< Human-readable description of backend.
    std::string submit_command;             ///< Template string executed to submit a job to the cluster.
    std::string poll_command;               ///< Template string executed to poll a submitted job's status.
    std::string cancel_command;             ///< Template string executed to cancel or kill a running job.
    std::string job_id_regex;               ///< Optional regex to extract the grid_id from submit command output.
    std::string success_pattern;            ///< Regex pattern identifying finished/successful jobs.
    std::string failure_pattern;            ///< Regex pattern identifying failed/aborted jobs.
    std::string running_pattern;            ///< Regex pattern identifying active/queued jobs.
    bool empty_poll_means_done{true};       ///< If poll output is empty, treat as completed.
};

/**
 * @struct HookConfig
 * @brief Lifecycle pre- and post-execution hooks configured at the global, stage, or job level.
 */
struct HookConfig {
    std::string pre;      ///< Command executed before the stage or job starts.
    std::string post;     ///< Command executed after the stage or job finishes.
    bool always{false};   ///< If true, post hook executes even if the stage or job failed.
};

/**
 * @struct JobDefaults
 * @brief Baseline default properties inherited by stages and jobs to eliminate repetitive YAML configurations.
 */
struct JobDefaults {
    std::string command;                                     ///< Default command line to execute if unspecified in job.
    std::string backend;                                     ///< Default execution backend identifier.
    std::string args;                                        ///< Default extra arguments appended to command.
    HookConfig hooks;                                        ///< Default lifecycle hooks.
    std::unordered_map<std::string, std::string> variables;  ///< Default variables scoped to this hierarchy level.
};

/**
 * @struct JobConfig
 * @brief Complete static configuration for an individual executable job in the DAG.
 */
struct JobConfig {
    std::string job_id;                                     ///< Formatted 4-digit unique identifier (e.g. "0001").
    std::string name;                                       ///< Descriptive human-readable job name.
    std::string command;                                    ///< Shell command to execute.
    std::string stage_name;                                 ///< Parent stage containing this job.
    std::string backend;                                    ///< Optional backend override for this specific job.
    std::string enabled_if;                                 ///< Conditional evaluation expression (e.g. "${coverage} == 1").
    bool allow_failure{false};                              ///< If true, non-zero exit code does not trigger failure policies.
    std::vector<std::string> depends_on;                    ///< List of prerequisite job IDs or stage names.
    HookConfig hooks;                                       ///< Job-level lifecycle hooks.
    std::unordered_map<std::string, std::string> variables; ///< Job-level scoped variables.
    std::optional<int> count;                               ///< Multi-instance count for matrix test generation.
    std::optional<int> seed;                                ///< Explicit pseudo-random seed assigned to this job instance.
    std::vector<int> seeds;                                 ///< Explicit seed list for seed parameter sweeps.
};

/**
 * @struct StageConfig
 * @brief Logical grouping and sequence boundary containing a collection of parallel jobs.
 */
struct StageConfig {
    std::string name;                     ///< Unique alphanumeric stage identifier.
    std::string backend;                  ///< Stage-level backend override.
    std::string enabled_if;               ///< Stage-level conditional evaluation expression.
    bool allow_failure{false};             ///< If true, stage failure does not abort the entire pipeline.
    std::vector<std::string> depends_on;  ///< Prerequisite stage names that must pass before this stage starts.
    HookConfig hooks;                     ///< Stage-level lifecycle hooks.
    std::vector<JobConfig> jobs;          ///< List of jobs configured directly inside this stage.
    std::vector<std::string> includes;    ///< Nested YAML test suite files included into this stage.
    JobDefaults defaults;                 ///< Stage-level defaults overriding global pipeline defaults.
};

/**
 * @struct PipelineSettings
 * @brief Global runtime execution options and command-line flags.
 */
struct PipelineSettings {
    size_t max_parallel_jobs{0};                          ///< Max concurrent jobs (0 = automatic hardware concurrency).
    std::string base_output_dir{"./condor_runs"};          ///< Root directory where execution timestamped runs are stored.
    std::string run_dir;                                  ///< Explicit run folder override if specified via --run-dir.
    FailureAction on_failure_action{FailureAction::ABORT}; ///< Failure policy: abort, continue, or stage_barrier.
    bool exit_zero_on_failure{false};                     ///< If true, condor process exits with 0 even if jobs failed.
    int status_interval_sec{30};                          ///< Periodic status dump interval for CI / non-interactive mode.
    std::string default_backend{"local"};                 ///< Global default backend for jobs without explicit backend.
    bool dry_run{false};                                  ///< If true, validate pipeline and print plan without executing.
    bool no_interactive{false};                           ///< If true, disable raw terminal escape codes and interactive menu.
    bool verbose_logging{false};                          ///< If true, print verbose debug logs.
    OutputMode output_mode{OutputMode::ALL};              ///< Verbosity filter: all, failures, or summary.
    PathStyle path_style{PathStyle::ABSOLUTE};            ///< Path format for environment exports: absolute or relative.
    std::string stage_filter;                             ///< Run only the specified stage if passed via --stage.
    std::string backend_override;                         ///< Force all jobs to use this backend if specified via --backend-override.
};

/**
 * @struct PipelineConfig
 * @brief Complete parsed pipeline configuration model representing the entire DAG.
 */
struct PipelineConfig {
    std::string name{"pipeline"};                            ///< Pipeline project name.
    VariableManager variables;                               ///< Global variable manager.
    PipelineSettings settings;                               ///< Runtime execution settings.
    JobDefaults defaults;                                    ///< Global job defaults applied across all stages.
    std::unordered_map<std::string, BackendConfig> backends; ///< Registered backend configurations.
    HookConfig hooks;                                        ///< Pipeline-level global hooks.
    std::vector<StageConfig> stages;                         ///< List of stages in the DAG.
    std::vector<std::string> includes;                       ///< Global YAML configuration files included at root level.

    /**
     * @brief Finds a stage configuration by name.
     * @param stage_name Identifier of stage to look up.
     * @return Pointer to StageConfig if found, or nullptr if not found.
     */
    const StageConfig* find_stage(std::string_view stage_name) const {
        for (const auto& stage : stages) {
            if (stage.name == stage_name) return &stage;
        }
        return nullptr;
    }
};

} // namespace condor
