/**
 * @file backend.hpp
 * @brief Execution backends for launching, polling, and canceling jobs (Local POSIX and Template Grid).
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
#include <memory>
#include <chrono>
#include <optional>
#include <unordered_map>
#include <filesystem>
#include <sys/types.h>
#include "condor/common.hpp"
#include "condor/config.hpp"

namespace condor {

/**
 * @struct JobExecutionContext
 * @brief Dynamic execution context and filesystem coordinates passed to backends for job invocation.
 */
struct JobExecutionContext {
    std::string job_id;                                           ///< Unique 4-digit job identifier (e.g. "0001").
    std::string job_name;                                         ///< Descriptive human-readable job name.
    std::string stage_name;                                       ///< Parent stage identifier.
    std::string command;                                          ///< Target shell command to run.
    std::string pre_hook;                                         ///< Optional pre-execution hook script.
    std::string post_hook;                                        ///< Optional post-execution hook script.
    bool post_always{false};                                      ///< Whether post-hook runs regardless of job exit code.
    bool hook_allow_failure{false};                               ///< Whether hook failure should be ignored.

    std::string job_dir;                                          ///< Job directory path exposed in environment.
    std::string stage_dir;                                        ///< Stage directory path exposed in environment.
    std::string run_dir;                                          ///< Active run root directory exposed in environment.
    std::string output_mode;                                      ///< Formatted terminal output mode.
    std::string build_dir;                                        ///< Pipeline build output directory.
    std::string test_dir;                                         ///< Pipeline test output directory.
    std::string physical_job_dir;                                 ///< Actual canonical host filesystem path for runner script & chdir.
    std::string working_dir;                                      ///< Optional target execution directory override.
    std::optional<ContainerConfig> container;                     ///< Optional container virtualization execution wrapper.
    int kill_grace_period_sec{3};                                 ///< Grace period in seconds between SIGTERM and SIGKILL.
    double peak_rss_mb{0.0};                                      ///< Peak resident set size (memory) observed in MB.

    std::vector<std::pair<std::string, std::string>> env_vars;    ///< Scoped environment variable key-value pairs.

    // Runtime state tracked by backend
    pid_t pid{0};                                                 ///< Host process ID (for local backend).
    std::string grid_id;                                          ///< Remote cluster batch job ID (for template backend).
    JobStatus status{JobStatus::QUEUED};                          ///< Current execution status.
    int exit_code{0};                                             ///< Final exit code recorded upon job termination.
    std::chrono::steady_clock::time_point start_time;             ///< Timestamp when job was launched.
    std::chrono::steady_clock::time_point end_time;               ///< Timestamp when job completed.
};

/**
 * @struct PollResult
 * @brief Status outcome returned by ExecutionBackend::poll.
 */
struct PollResult {
    bool finished{false};                 ///< True if the job has finished executing.
    JobStatus status{JobStatus::RUNNING}; ///< Current or final status (RUNNING, PASSED, FAILED, ABORTED).
    int exit_code{0};                     ///< Process or batch exit code if finished.
    double peak_rss_mb{0.0};              ///< Peak resident set size observed in MB.
};

/**
 * @class ExecutionBackend
 * @brief Abstract base class defining the execution interface for dispatching and tracking jobs.
 */
class ExecutionBackend {
public:
    virtual ~ExecutionBackend() = default;

    /**
     * @brief Gets the identifier name of this backend.
     * @return String view representing backend name (e.g. "local", "slurm").
     */
    virtual std::string_view name() const = 0;

    /**
     * @brief Gets the execution type of this backend (LOCAL or TEMPLATE).
     * @return BackendType enum value.
     */
    virtual BackendType type() const { return BackendType::LOCAL; }

    /**
     * @brief Submits or spawns a job asynchronously.
     * @param ctx Execution context containing paths, scripts, and environment.
     */
    virtual void submit(JobExecutionContext& ctx) = 0;

    /**
     * @brief Polls the status of a previously submitted job without blocking.
     * @param ctx Execution context for the job.
     * @return PollResult containing finished flag, status, and exit code.
     */
    virtual PollResult poll(JobExecutionContext& ctx) = 0;

    /**
     * @brief Cancels or terminates a running job.
     * @param ctx Execution context for the job to cancel.
     */
    virtual void cancel(JobExecutionContext& ctx) = 0;
};

/**
 * @class LocalBackend
 * @brief POSIX execution backend using fork, execvp, dedicated process groups, and SIGTERM/SIGKILL signal trees.
 */
class LocalBackend : public ExecutionBackend {
public:
    std::string_view name() const override { return "local"; }
    BackendType type() const override { return BackendType::LOCAL; }

    void submit(JobExecutionContext& ctx) override;
    PollResult poll(JobExecutionContext& ctx) override;
    void cancel(JobExecutionContext& ctx) override;

    /**
     * @brief Generates self-contained POSIX shell runner script (`runner.sh`) for execution inside the job folder.
     * @param ctx Job execution context containing commands, hooks, and environment.
     * @return Complete bash script content.
     */
    static std::string generate_runner_script(const JobExecutionContext& ctx);
};

/**
 * @class TemplateBackend
 * @brief Generic grid backend driven by user-defined command templates (e.g. Slurm sbatch, SGE qsub, LSF bsub).
 */
class TemplateBackend : public ExecutionBackend {
public:
    explicit TemplateBackend(const BackendConfig& config) : config_(config) {}

    std::string_view name() const override { return config_.name; }
    BackendType type() const override { return BackendType::TEMPLATE; }

    void submit(JobExecutionContext& ctx) override;
    PollResult poll(JobExecutionContext& ctx) override;
    void cancel(JobExecutionContext& ctx) override;

private:
    std::string substitute(std::string_view template_str, const JobExecutionContext& ctx) const;
    static std::string execute_capture(const std::string& cmd);

    BackendConfig config_;
};

/**
 * @class BackendFactory
 * @brief Factory class for instantiating execution backend instances from configuration specifications.
 */
class BackendFactory {
public:
    /**
     * @brief Instantiates a backend implementation matching the provided configuration.
     * @param config Backend configuration specification.
     * @return Unique pointer to the constructed ExecutionBackend instance.
     */
    static std::unique_ptr<ExecutionBackend> create(const BackendConfig& config);
};

/**
 * @class BackendRegistry
 * @brief Dynamic backend discovery and configuration loader facility.
 *
 * Discovers and loads external YAML backend definitions (Slurm, SGE/Grid Engine,
 * IBM Spectrum LSF, PBS Pro / TORQUE, Apache Spark) without hardcoding them into C++ sources.
 */
class BackendRegistry {
public:
    /**
     * @brief Returns candidate filesystem directories where backend definition files are located.
     * @param pipeline_dir Optional directory of the active pipeline file.
     * @return Ordered list of directories to query.
     */
    static std::vector<std::filesystem::path> get_search_paths(const std::string& pipeline_dir = "");

    /**
     * @brief Discovers all backend configuration files available across the search paths.
     * @param pipeline_dir Optional directory of the active pipeline file.
     * @return Map of backend names to parsed BackendConfig structs.
     */
    static std::unordered_map<std::string, BackendConfig> discover_backends(const std::string& pipeline_dir = "");

    /**
     * @brief Attempts to load a specific backend configuration by name from search paths.
     * @param backend_name Name of backend (e.g. "slurm", "sge", "lsf", "pbs", "torque", "spark").
     * @param pipeline_dir Optional directory of the active pipeline file.
     * @return Loaded BackendConfig if found, or std::nullopt.
     */
    static std::optional<BackendConfig> load_backend(const std::string& backend_name, const std::string& pipeline_dir = "");

    /**
     * @brief Parses an individual backend YAML file into a BackendConfig struct.
     * @param filepath Path to the YAML backend definition file.
     * @return Parsed BackendConfig struct.
     */
    static BackendConfig parse_backend_file(const std::filesystem::path& filepath);
};

} // namespace condor
