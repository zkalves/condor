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

#include "condor/scheduler.hpp"
#include "condor/expression.hpp"
#include "condor/menu.hpp"
#include "condor/logging.hpp"
#include "condor/html_report.hpp"
#include <yaml-cpp/yaml.h>
#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <cstring>
#include <fstream>
#include <deque>

namespace condor {

Scheduler::Scheduler(const PipelineConfig& config, WorkspaceManager& workspace)
    : config_(config), workspace_(workspace) {
    max_slots_ = config_.settings.max_parallel_jobs;
    bool interactive = isatty(STDOUT_FILENO) && !config_.settings.no_interactive;
    progress_ = std::make_unique<ProgressDisplay>(interactive, config_.settings.status_interval_sec);
    progress_->set_output_mode(config_.settings.output_mode);
}

Scheduler::~Scheduler() {
    RuntimeMenu::restore_signal_handlers();
}

bool Scheduler::should_run_stage(std::string_view stage_name) const {
    if (config_.settings.stage_filter.empty()) {
        return true;
    }
    return config_.settings.stage_filter == stage_name;
}

void Scheduler::init_runtime_states() {
    pipeline_start_time_ = std::chrono::steady_clock::now();

    // 1. Initialize backends
    for (const auto& [bname, bcfg] : config_.backends) {
        backends_[bname] = BackendFactory::create(bcfg);
    }
    if (backends_.find("local") == backends_.end()) {
        BackendConfig local_cfg;
        local_cfg.name = "local";
        local_cfg.type = BackendType::LOCAL;
        backends_["local"] = BackendFactory::create(local_cfg);
    }

    // 2. Initialize stages and jobs
    for (const auto& stage_cfg : config_.stages) {
        if (!should_run_stage(stage_cfg.name)) {
            continue;
        }

        StageRuntimeState stage_state;
        stage_state.config = stage_cfg;

        // Evaluate stage enabled_if
        bool stage_enabled = ExpressionEvaluator::evaluate(stage_cfg.enabled_if, config_.variables);
        if (!stage_enabled) {
            stage_state.status = JobStatus::SKIPPED;
            stage_state.skipped = true;
            CONDOR_LOG_DEBUG("Stage '" + stage_cfg.name + "' skipped due to enabled_if evaluation.");
        } else {
            workspace_.create_stage_dir(stage_cfg.name);
        }

        for (const auto& job_cfg : stage_cfg.jobs) {
            JobRuntimeState job_state;
            job_state.config = job_cfg;

            // Determine backend (job-level -> stage-level -> settings backend -> default)
            std::string bname;
            if (!config_.settings.backend_override.empty()) {
                bname = config_.settings.backend_override;
            } else if (!job_cfg.backend.empty()) {
                bname = job_cfg.backend;
            } else if (!stage_cfg.backend.empty()) {
                bname = stage_cfg.backend;
            } else if (!config_.settings.backend.empty()) {
                bname = config_.settings.backend;
            } else {
                bname = "local";
            }
            if (bname.empty()) {
                bname = "local";
            }

            auto bit = backends_.find(bname);
            if (bit == backends_.end()) {
                auto loaded = BackendRegistry::load_backend(bname);
                if (loaded) {
                    backends_[bname] = BackendFactory::create(*loaded);
                    bit = backends_.find(bname);
                }
            }
            if (bit != backends_.end()) {
                job_state.backend = bit->second.get();
            } else {
                CONDOR_LOG_WARN("Backend '" + bname + "' not found; falling back to 'local'.");
                job_state.backend = backends_["local"].get();
            }

            StageExecutionMode smode = stage_cfg.mode;
            auto mode_it = config_.settings.stage_modes.find(stage_cfg.name);
            if (mode_it != config_.settings.stage_modes.end()) {
                smode = mode_it->second;
            }

            if (!stage_enabled) {
                job_state.status = JobStatus::SKIPPED;
            } else if (!config_.settings.rerun_base_dir.empty()) {
                // Rerun mode
                if (smode == StageExecutionMode::CACHED) {
                    job_state.status = JobStatus::PASSED;
                    CONDOR_LOG_DEBUG("Job '" + job_cfg.job_id + "' marked cached from base run.");
                } else if (smode == StageExecutionMode::FAILED_ONLY) {
                    std::filesystem::path prev_status = std::filesystem::path(config_.settings.rerun_base_dir) / "stages" / stage_cfg.name / job_cfg.job_id / "job_status.yaml";
                    bool prev_passed = false;
                    if (std::filesystem::exists(prev_status)) {
                        try {
                            YAML::Node s_node = YAML::LoadFile(prev_status.string());
                            if (s_node["status"]) {
                                std::string st = s_node["status"].as<std::string>();
                                if (st == "PASSED" || st == "FAILED (ALLOWED)" || st == "SKIPPED") {
                                    prev_passed = true;
                                }
                            }
                        } catch (...) {}
                    }
                    if (prev_passed) {
                        job_state.status = JobStatus::PASSED;
                        CONDOR_LOG_DEBUG("Job '" + job_cfg.job_id + "' passed in base run; marked cached.");
                    } else {
                        job_state.status = JobStatus::QUEUED;
                        workspace_.create_job_dir(stage_cfg.name, job_cfg.job_id);
                    }
                } else {
                    // FULL mode: re-run all jobs in stage
                    job_state.status = JobStatus::QUEUED;
                    workspace_.create_job_dir(stage_cfg.name, job_cfg.job_id);
                }
            } else {
                // Evaluate job enabled_if
                bool job_enabled = ExpressionEvaluator::evaluate(job_cfg.enabled_if, config_.variables);
                if (!job_enabled) {
                    job_state.status = JobStatus::SKIPPED;
                    CONDOR_LOG_DEBUG("Job '" + job_cfg.job_id + "' (" + job_cfg.name + ") skipped due to enabled_if.");
                } else {
                    workspace_.create_job_dir(stage_cfg.name, job_cfg.job_id);
                }
            }

            std::string job_key = stage_cfg.name + "/" + job_cfg.job_id;
            jobs_[job_key] = std::move(job_state);
            stage_state.job_ids.push_back(job_key);
        }

        stages_.push_back(std::move(stage_state));
        if (stage_enabled) {
            sync_stage_manifest(stage_cfg.name);
        }
    }
}

void Scheduler::sync_stage_manifest(std::string_view stage_name) {
    std::vector<StageManifestEntry> entries;
    for (const auto& stage : stages_) {
        if (stage.config.name == stage_name) {
            for (const auto& jid : stage.job_ids) {
                const auto& job = jobs_[jid];
                entries.push_back({job.config.job_id, job.config.name, job.config.command, job.status});
            }
            break;
        }
    }
    workspace_.update_stage_manifest(stage_name, entries);
}

int Scheduler::execute_hook(const HookConfig& hook, bool is_pre,
                            std::string_view stage_name,
                            std::string_view job_id,
                            int last_exit_code) {
    const std::string& script = is_pre ? hook.pre : hook.post;
    if (script.empty()) return 0;

    if (!is_pre && last_exit_code != 0 && !hook.always) {
        CONDOR_LOG_DEBUG("Skipping post hook because previous command failed and always=false");
        return 0;
    }

    auto hook_vars = config_.variables;
    std::filesystem::path current_cwd = std::filesystem::current_path();
    std::error_code ec;

    std::string run_dir_val = workspace_.run_dir().string();
    std::string stage_dir_val = stage_name.empty() ? "" : workspace_.stage_dir(stage_name).string();
    std::string job_dir_val = (stage_name.empty() || job_id.empty()) ? "" : workspace_.job_dir(stage_name, job_id).string();

    std::string build_dir_abs;
    for (const auto& s : stages_) {
        if (s.config.name == "build" || s.config.name == "build_rtl" || s.config.name.find("build") != std::string::npos) {
            if (!s.job_ids.empty()) {
                const auto& bj = jobs_.at(s.job_ids[0]);
                build_dir_abs = workspace_.job_dir(s.config.name, bj.config.job_id).string();
                break;
            }
        }
    }

    std::string build_dir_val = build_dir_abs;

    if (config_.settings.path_style == PathStyle::RELATIVE) {
        run_dir_val = std::filesystem::relative(workspace_.run_dir(), current_cwd, ec).string();
        if (!stage_dir_val.empty()) {
            stage_dir_val = std::filesystem::relative(workspace_.stage_dir(stage_name), current_cwd, ec).string();
        }
        if (!job_dir_val.empty()) {
            job_dir_val = std::filesystem::relative(workspace_.job_dir(stage_name, job_id), current_cwd, ec).string();
        }
        if (!build_dir_abs.empty()) {
            build_dir_val = std::filesystem::relative(std::filesystem::path(build_dir_abs), current_cwd, ec).string();
        }
    }

    hook_vars.set("CONDOR_RUN_DIR", run_dir_val);
    hook_vars.set("CONDOR_STAGE_NAME", std::string(stage_name));
    hook_vars.set("CONDOR_JOB_ID", std::string(job_id));
    hook_vars.set("CONDOR_EXIT_CODE", std::to_string(last_exit_code));
    hook_vars.set("CONDOR_OUTPUT_MODE", to_string(config_.settings.output_mode));
    hook_vars.set("CONDOR_PATH_STYLE", to_string(config_.settings.path_style));

    if (!stage_name.empty()) {
        hook_vars.set("CONDOR_STAGE_DIR", stage_dir_val);
        if (!job_id.empty()) {
            hook_vars.set("CONDOR_JOB_DIR", job_dir_val);
            hook_vars.set("TEST_DIR", job_dir_val);
            hook_vars.set("CONDOR_TEST_DIR", job_dir_val);
        }
    }

    if (!build_dir_val.empty()) {
        hook_vars.set("BUILD_DIR", build_dir_val);
        hook_vars.set("CONDOR_BUILD_DIR", build_dir_val);
    }

    std::string interpolated = hook_vars.interpolate(script);
    CONDOR_LOG_DEBUG("Executing hook: " + interpolated);

    std::filesystem::path hook_dir;
    if (!job_id.empty() && !stage_name.empty()) {
        hook_dir = workspace_.job_dir(stage_name, job_id);
    } else if (!stage_name.empty()) {
        hook_dir = workspace_.stage_dir(stage_name);
    } else {
        hook_dir = workspace_.pipeline_dir();
    }
    std::error_code dir_ec;
    std::filesystem::create_directories(hook_dir, dir_ec);
    std::filesystem::path log_path = hook_dir / (is_pre ? "pre_hook.log" : "post_hook.log");

    pid_t pid = fork();
    if (pid < 0) {
        CONDOR_LOG_ERROR("fork() failed for hook: " + std::string(std::strerror(errno)));
        return -1;
    }

    if (pid == 0) {
        if (chdir(hook_dir.c_str()) != 0) {
            // best effort
        }

        int log_fd = open(log_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (log_fd >= 0) {
            dup2(log_fd, STDOUT_FILENO);
            dup2(log_fd, STDERR_FILENO);
            close(log_fd);
        }

        setenv("CONDOR_RUN_DIR", run_dir_val.c_str(), 1);
        setenv("CONDOR_STAGE_NAME", std::string(stage_name).c_str(), 1);
        setenv("CONDOR_JOB_ID", std::string(job_id).c_str(), 1);
        setenv("CONDOR_EXIT_CODE", std::to_string(last_exit_code).c_str(), 1);
        setenv("CONDOR_OUTPUT_MODE", to_string(config_.settings.output_mode).c_str(), 1);
        setenv("CONDOR_PATH_STYLE", to_string(config_.settings.path_style).c_str(), 1);
        if (!stage_dir_val.empty()) {
            setenv("CONDOR_STAGE_DIR", stage_dir_val.c_str(), 1);
        }
        if (!build_dir_val.empty()) {
            setenv("BUILD_DIR", build_dir_val.c_str(), 1);
            setenv("CONDOR_BUILD_DIR", build_dir_val.c_str(), 1);
        }
        if (!job_dir_val.empty()) {
            setenv("CONDOR_JOB_DIR", job_dir_val.c_str(), 1);
            setenv("TEST_DIR", job_dir_val.c_str(), 1);
            setenv("CONDOR_TEST_DIR", job_dir_val.c_str(), 1);
        }

        for (const auto& [k, v] : hook_vars.get_env_pairs()) {
            setenv(k.c_str(), v.c_str(), 1);
        }

        char* const argv[] = {
            const_cast<char*>("bash"),
            const_cast<char*>("-e"),
            const_cast<char*>("-c"),
            const_cast<char*>(interpolated.c_str()),
            nullptr
        };
        execvp("bash", argv);
        _exit(127);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 1);
    if (exit_code != 0) {
        CONDOR_LOG_DEBUG("Hook finished with exit code " + std::to_string(exit_code));
    }
    return exit_code;
}

bool Scheduler::is_stage_ready(const StageRuntimeState& stage) const {
    if (stage.status != JobStatus::QUEUED) return false;

    // Check all dependencies
    for (const auto& dep_name : stage.config.depends_on) {
        bool found = false;
        for (const auto& other_stage : stages_) {
            if (other_stage.config.name == dep_name) {
                found = true;
                if (other_stage.status == JobStatus::QUEUED || other_stage.status == JobStatus::RUNNING) {
                    return false;
                }
                if (other_stage.status == JobStatus::FAILED) {
                    // Dependency failed and wasn't allowed
                    return false;
                }
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

bool Scheduler::is_job_ready(const JobRuntimeState& job) const {
    if (job.status != JobStatus::QUEUED) return false;
    if (std::chrono::steady_clock::now() < job.next_retry_time) return false;

    // Check job-level depends_on
    for (const auto& dep : job.config.depends_on) {
        // 1. Exact match in jobs_ (e.g. stage/0001)
        auto it = jobs_.find(dep);
        if (it != jobs_.end()) {
            if (it->second.status == JobStatus::QUEUED || it->second.status == JobStatus::RUNNING) {
                return false;
            }
            if (it->second.status == JobStatus::FAILED || it->second.status == JobStatus::ABORTED) {
                return false;
            }
            continue;
        }

        // 2. Job ID in same stage (e.g. 0001)
        std::string same_stage_key = job.config.stage_name + "/" + dep;
        it = jobs_.find(same_stage_key);
        if (it != jobs_.end()) {
            if (it->second.status == JobStatus::QUEUED || it->second.status == JobStatus::RUNNING) {
                return false;
            }
            if (it->second.status == JobStatus::FAILED || it->second.status == JobStatus::ABORTED) {
                return false;
            }
            continue;
        }

        // 3. Search across all jobs by config.name or within stage by config.job_id
        for (const auto& [k, other_job] : jobs_) {
            if (other_job.config.name == dep || (other_job.config.stage_name == job.config.stage_name && other_job.config.job_id == dep)) {
                if (other_job.status == JobStatus::QUEUED || other_job.status == JobStatus::RUNNING) {
                    return false;
                }
                if (other_job.status == JobStatus::FAILED || other_job.status == JobStatus::ABORTED) {
                    return false;
                }
            }
        }
    }
    return true;
}

void Scheduler::dispatch_ready_jobs() {
    if (dispatch_paused_ || aborted_) return;

    // Count currently running jobs and active resource utilization
    size_t running_count = 0;
    std::unordered_map<std::string, int> used_resources;
    for (const auto& [jid, job] : jobs_) {
        if (job.status == JobStatus::RUNNING) {
            running_count++;
            for (const auto& [rname, req] : job.config.resources) {
                used_resources[rname] += req;
            }
        }
    }

    if (running_count >= max_slots_) return;

    for (auto& stage : stages_) {
        if (stage.status == JobStatus::SKIPPED) continue;

        if (stage.status == JobStatus::QUEUED) {
            if (stage_barrier_triggered_) {
                // Halt before subsequent stages
                continue;
            }
            if (!is_stage_ready(stage)) {
                continue;
            }
            // Stage is starting
            stage.status = JobStatus::RUNNING;
            int pre_rc = execute_hook(stage.config.hooks, true, stage.config.name);
            if (pre_rc != 0 && !stage.config.hooks.allow_failure) {
                CONDOR_LOG_ERROR("Stage '" + stage.config.name + "' pre-hook failed with exit code " + std::to_string(pre_rc));
                stage.status = JobStatus::FAILED;
                for (const auto& jid : stage.job_ids) {
                    jobs_[jid].status = JobStatus::SKIPPED;
                }
                sync_stage_manifest(stage.config.name);

                if (!stage.config.allow_failure) {
                    if (config_.settings.on_failure_action == FailureAction::ABORT) {
                        aborted_ = true;
                        return;
                    } else if (config_.settings.on_failure_action == FailureAction::STAGE_BARRIER) {
                        stage_barrier_triggered_ = true;
                    }
                }
                continue;
            }
        }

        if (stage.status != JobStatus::RUNNING) continue;

        for (const auto& jid : stage.job_ids) {
            if (running_count >= max_slots_) break;

            auto& job = jobs_[jid];
            if (job.status == JobStatus::QUEUED && is_job_ready(job)) {
                // Check resource availability
                bool res_available = true;
                for (const auto& [rname, req] : job.config.resources) {
                    auto it = config_.resources.find(rname);
                    int cap = (it != config_.resources.end()) ? it->second : 0;
                    if (used_resources[rname] + req > cap) {
                        res_available = false;
                        break;
                    }
                }
                if (!res_available) {
                    continue;
                }

                // Dispatch job
                auto ctx = std::make_unique<JobExecutionContext>();
                ctx->job_id = job.config.job_id;
                ctx->job_name = job.config.name;
                ctx->stage_name = job.config.stage_name;

                auto job_p = workspace_.job_dir(stage.config.name, job.config.job_id);
                auto stage_p = workspace_.stage_dir(stage.config.name);
                auto run_p = workspace_.run_dir();
                std::error_code ec;

                ctx->physical_job_dir = job_p.string();

                // Discover build directory (from stage with "build" in name or first dependency)
                std::string build_dir_abs;
                for (const auto& s : stages_) {
                    if (s.config.name == "build" || s.config.name == "build_rtl" || s.config.name.find("build") != std::string::npos) {
                        if (!s.job_ids.empty()) {
                            const auto& bj = jobs_.at(s.job_ids[0]);
                            build_dir_abs = workspace_.job_dir(s.config.name, bj.config.job_id).string();
                            break;
                        }
                    }
                }
                if (build_dir_abs.empty() && !stage.config.depends_on.empty()) {
                    const std::string& dep_stage = stage.config.depends_on[0];
                    build_dir_abs = workspace_.job_dir(dep_stage, "0001").string();
                }

                if (config_.settings.path_style == PathStyle::RELATIVE) {
                    ctx->job_dir = ".";
                    ctx->stage_dir = std::filesystem::relative(stage_p, job_p, ec).string();
                    ctx->run_dir = std::filesystem::relative(run_p, job_p, ec).string();
                    ctx->test_dir = ".";
                    if (!build_dir_abs.empty()) {
                        ctx->build_dir = std::filesystem::relative(std::filesystem::path(build_dir_abs), job_p, ec).string();
                    }
                } else {
                    ctx->job_dir = job_p.string();
                    ctx->stage_dir = stage_p.string();
                    ctx->run_dir = run_p.string();
                    ctx->test_dir = ctx->job_dir;
                    ctx->build_dir = build_dir_abs;
                }
                ctx->output_mode = to_string(config_.settings.output_mode);

                // Merge built-in job context variables for interpolation
                auto job_vars = config_.variables;
                if (config_.settings.path_style == PathStyle::RELATIVE) {
                    if (config_.variables.has("CONDOR_PIPELINE_DIR")) {
                        std::string pipe_abs = config_.variables.get("CONDOR_PIPELINE_DIR");
                        std::string rel_pipe = std::filesystem::relative(std::filesystem::path(pipe_abs), job_p, ec).string();
                        job_vars.set("CONDOR_PIPELINE_DIR", rel_pipe);
                        job_vars.set("PIPELINE_DIR", rel_pipe);
                    }
                    if (config_.variables.has("CONDOR_CWD")) {
                        std::string cwd_abs = config_.variables.get("CONDOR_CWD");
                        std::string rel_cwd = std::filesystem::relative(std::filesystem::path(cwd_abs), job_p, ec).string();
                        job_vars.set("CONDOR_CWD", rel_cwd);
                    }
                }

                job_vars.set("CONDOR_RUN_DIR", ctx->run_dir);
                job_vars.set("CONDOR_STAGE_DIR", ctx->stage_dir);
                job_vars.set("CONDOR_JOB_DIR", ctx->job_dir);
                job_vars.set("CONDOR_STAGE_NAME", ctx->stage_name);
                job_vars.set("CONDOR_JOB_ID", ctx->job_id);
                job_vars.set("CONDOR_JOB_NAME", ctx->job_name);
                job_vars.set("CONDOR_OUTPUT_MODE", to_string(config_.settings.output_mode));
                job_vars.set("CONDOR_PATH_STYLE", to_string(config_.settings.path_style));

                if (!ctx->build_dir.empty()) {
                    job_vars.set("BUILD_DIR", ctx->build_dir);
                    job_vars.set("CONDOR_BUILD_DIR", ctx->build_dir);
                }
                job_vars.set("TEST_DIR", ctx->test_dir);
                job_vars.set("CONDOR_TEST_DIR", ctx->test_dir);

                // Inject job-specific variables (e.g. INDEX)
                for (const auto& [k, v] : job.config.variables) {
                    job_vars.set(k, v);
                }

                // Inject allocated resource variables
                std::string res_list;
                for (const auto& [rname, req] : job.config.resources) {
                    std::string env_key = "CONDOR_RESOURCE_" + rname;
                    for (char& c : env_key) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                    job_vars.set(env_key, std::to_string(req));
                    if (!res_list.empty()) res_list += ",";
                    res_list += rname + "=" + std::to_string(req);
                }
                if (!res_list.empty()) {
                    job_vars.set("CONDOR_RESOURCES", res_list);
                }

                if (!job.config.working_dir.empty()) {
                    ctx->working_dir = job_vars.interpolate(job.config.working_dir);
                }

                ctx->command = job_vars.interpolate(job.config.command);
                ctx->pre_hook = job_vars.interpolate(job.config.hooks.pre);
                ctx->post_hook = job_vars.interpolate(job.config.hooks.post);
                ctx->post_always = job.config.hooks.always;
                ctx->hook_allow_failure = job.config.hooks.allow_failure;
                ctx->env_vars = job_vars.get_env_pairs();
                for (const auto& [k, v] : job.config.variables) {
                    ctx->env_vars.emplace_back(VariableManager::to_env_key(k), v);
                    ctx->env_vars.emplace_back(k, v);
                }
                for (const auto& [rname, req] : job.config.resources) {
                    std::string env_key = "CONDOR_RESOURCE_" + rname;
                    for (char& c : env_key) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                    ctx->env_vars.emplace_back(env_key, std::to_string(req));
                }
                if (!res_list.empty()) {
                    ctx->env_vars.emplace_back("CONDOR_RESOURCES", res_list);
                }

                ctx->container = job.config.container;
                ctx->kill_grace_period_sec = config_.settings.kill_grace_period_sec;
                ctx->peak_rss_mb = job.peak_rss_mb;

                job_vars.set("CONDOR_RETRY_COUNT", std::to_string(job.retry_count));
                ctx->env_vars.emplace_back("CONDOR_RETRY_COUNT", std::to_string(job.retry_count));

                job.backend->submit(*ctx);
                job.status = JobStatus::RUNNING;
                job.pid_or_grid_id = ctx->grid_id;
                job.exec_ctx = std::move(ctx);

                for (const auto& [rname, req] : job.config.resources) {
                    used_resources[rname] += req;
                }

                running_count++;
                progress_->on_job_started(stage.config.name, job.config.job_id, job.config.name);
                sync_stage_manifest(stage.config.name);
            }
        }
    }
}

void Scheduler::poll_running_jobs() {
    auto now = std::chrono::steady_clock::now();
    int grid_interval = (config_.settings.grid_poll_interval_sec > 0) ? config_.settings.grid_poll_interval_sec : 10;
    bool can_poll_grid = (std::chrono::duration<double>(now - last_grid_poll_time_).count() >= grid_interval);
    bool did_poll_grid = false;

    for (auto& [jid, job] : jobs_) {
        if (job.status == JobStatus::RUNNING && job.exec_ctx) {
            // Check timeout enforcement
            double elapsed = std::chrono::duration<double>(now - job.exec_ctx->start_time).count();
            if (job.config.timeout_sec > 0 && elapsed >= job.config.timeout_sec) {
                CONDOR_LOG_WARN("Job " + jid + " (" + job.config.name + ") timed out after " +
                                std::to_string(job.config.timeout_sec) + "s! Terminating...");
                job.backend->cancel(*job.exec_ctx);
                job.status = JobStatus::TIMEOUT;
                job.exit_code = 124;
                job.elapsed_sec = elapsed;

                bool eligible_for_retry = (job.retry_count < job.config.retry);
                if (eligible_for_retry && !job.config.retry_on_exit_codes.empty()) {
                    eligible_for_retry = false;
                    for (int code : job.config.retry_on_exit_codes) {
                        if (code == 124) { eligible_for_retry = true; break; }
                    }
                }
                if (eligible_for_retry) {
                    job.retry_count++;
                    job.status = JobStatus::QUEUED;
                    job.exec_ctx.reset();
                    CONDOR_LOG_WARN("Job " + jid + " (" + job.config.name + ") timed out; scheduling retry " +
                                    std::to_string(job.retry_count) + "/" + std::to_string(job.config.retry) +
                                    (job.config.retry_delay_sec > 0 ? " in " + std::to_string(job.config.retry_delay_sec) + "s" : "") + "...");
                    if (job.config.retry_delay_sec > 0) {
                        job.next_retry_time = now + std::chrono::seconds(job.config.retry_delay_sec);
                    } else {
                        job.next_retry_time = now;
                    }
                    sync_stage_manifest(job.config.stage_name);
                    continue;
                }

                const auto* s = config_.find_stage(job.config.stage_name);
                bool allow = job.config.allow_failure || (s && s->allow_failure);
                if (allow) {
                    job.status = JobStatus::FAILED_ALLOWED;
                } else {
                    FailureAction action = config_.settings.on_failure_action;
                    if (action == FailureAction::ABORT) {
                        CONDOR_LOG_ERROR("Job " + jid + " timed out! Triggering ABORT policy...");
                        abort_all();
                    } else if (action == FailureAction::STAGE_BARRIER) {
                        CONDOR_LOG_WARN("Job " + jid + " timed out! Triggering STAGE_BARRIER policy...");
                        stage_barrier_triggered_ = true;
                    } else if (action == FailureAction::CONTINUE) {
                        CONDOR_LOG_WARN("Job " + jid + " timed out! Continuing independent branches...");
                    }
                }

                JobMetadata meta;
                meta.job_id = job.config.job_id;
                meta.job_name = job.config.name;
                meta.stage_name = job.config.stage_name;
                meta.pid_or_grid_id = job.pid_or_grid_id;
                meta.status = job.status;
                meta.exit_code = job.exit_code;
                meta.elapsed_sec = job.elapsed_sec;
                meta.peak_rss_mb = job.peak_rss_mb;
                meta.retries = job.retry_count;
                if (config_.settings.path_style == PathStyle::RELATIVE) {
                    meta.working_directory = ".";
                } else {
                    meta.working_directory = workspace_.job_dir(job.config.stage_name, job.config.job_id).string();
                }
                workspace_.write_job_status(job.config.stage_name, meta);

                sync_stage_manifest(job.config.stage_name);
                progress_->on_job_finished(job.config.stage_name, job.config.job_id, job.config.name,
                                           job.status, job.exit_code, job.elapsed_sec);
                continue;
            }

            // Grid polling throttling
            if (job.backend->type() == BackendType::TEMPLATE && !can_poll_grid) {
                continue;
            }
            if (job.backend->type() == BackendType::TEMPLATE) {
                did_poll_grid = true;
            }

            auto poll_res = job.backend->poll(*job.exec_ctx);
            if (poll_res.finished) {
                job.exit_code = poll_res.exit_code;
                job.elapsed_sec = std::chrono::duration<double>(now - job.exec_ctx->start_time).count();
                job.peak_rss_mb = std::max(job.peak_rss_mb, poll_res.peak_rss_mb);

                if (job.exit_code == 137) {
                    CONDOR_LOG_WARN("Job " + jid + " (" + job.config.name + ") exited with code 137 (OOM / SIGKILL)!");
                }

                if (poll_res.status == JobStatus::PASSED) {
                    job.status = JobStatus::PASSED;
                } else if (poll_res.status == JobStatus::ABORTED) {
                    job.status = JobStatus::ABORTED;
                } else {
                    // Failed: check retry policy
                    bool eligible_for_retry = (job.retry_count < job.config.retry);
                    if (eligible_for_retry && !job.config.retry_on_exit_codes.empty()) {
                        eligible_for_retry = false;
                        for (int code : job.config.retry_on_exit_codes) {
                            if (code == job.exit_code) { eligible_for_retry = true; break; }
                        }
                    }
                    if (eligible_for_retry) {
                        job.retry_count++;
                        job.status = JobStatus::QUEUED;
                        job.exec_ctx.reset();
                        CONDOR_LOG_WARN("Job " + jid + " (" + job.config.name + ") failed with exit code " +
                                        std::to_string(job.exit_code) + "; scheduling retry " +
                                        std::to_string(job.retry_count) + "/" + std::to_string(job.config.retry) +
                                        (job.config.retry_delay_sec > 0 ? " in " + std::to_string(job.config.retry_delay_sec) + "s" : "") + "...");
                        if (job.config.retry_delay_sec > 0) {
                            job.next_retry_time = now + std::chrono::seconds(job.config.retry_delay_sec);
                        } else {
                            job.next_retry_time = now;
                        }
                        sync_stage_manifest(job.config.stage_name);
                        continue;
                    }

                    // Failed without retries
                    const auto* s = config_.find_stage(job.config.stage_name);
                    bool allow = job.config.allow_failure || (s && s->allow_failure);
                    if (allow) {
                        job.status = JobStatus::FAILED_ALLOWED;
                    } else {
                        job.status = JobStatus::FAILED;

                        // Trigger failure policy
                        FailureAction action = config_.settings.on_failure_action;
                        if (action == FailureAction::ABORT) {
                            CONDOR_LOG_ERROR("Job " + jid + " failed! Triggering ABORT policy...");
                            abort_all();
                        } else if (action == FailureAction::STAGE_BARRIER) {
                            CONDOR_LOG_WARN("Job " + jid + " failed! Triggering STAGE_BARRIER policy...");
                            stage_barrier_triggered_ = true;
                        } else if (action == FailureAction::CONTINUE) {
                            CONDOR_LOG_WARN("Job " + jid + " failed! Continuing independent branches...");
                        }
                    }
                }

                // Write job metadata
                JobMetadata meta;
                meta.job_id = job.config.job_id;
                meta.job_name = job.config.name;
                meta.stage_name = job.config.stage_name;
                meta.pid_or_grid_id = job.pid_or_grid_id;
                meta.status = job.status;
                meta.exit_code = job.exit_code;
                meta.elapsed_sec = job.elapsed_sec;
                meta.peak_rss_mb = job.peak_rss_mb;
                meta.retries = job.retry_count;
                if (config_.settings.path_style == PathStyle::RELATIVE) {
                    meta.working_directory = ".";
                } else {
                    meta.working_directory = workspace_.job_dir(job.config.stage_name, job.config.job_id).string();
                }
                workspace_.write_job_status(job.config.stage_name, meta);

                sync_stage_manifest(job.config.stage_name);
                progress_->on_job_finished(job.config.stage_name, job.config.job_id, job.config.name,
                                           job.status, job.exit_code, job.elapsed_sec);
            }
        }
    }

    if (did_poll_grid) {
        last_grid_poll_time_ = now;
    }

    // Check if stages have finished
    for (auto& stage : stages_) {
        if (stage.status == JobStatus::RUNNING) {
            bool all_done = true;
            bool any_failed = false;
            for (const auto& jid : stage.job_ids) {
                const auto& j = jobs_[jid];
                if (j.status == JobStatus::QUEUED || j.status == JobStatus::RUNNING) {
                    all_done = false;
                    break;
                }
                if (j.status == JobStatus::FAILED || j.status == JobStatus::ABORTED || j.status == JobStatus::TIMEOUT) {
                    any_failed = true;
                }
            }
            if (all_done) {
                stage.status = any_failed ? JobStatus::FAILED : JobStatus::PASSED;
                int post_rc = execute_hook(stage.config.hooks, false, stage.config.name, "", any_failed ? 1 : 0);
                if (post_rc != 0 && !stage.config.hooks.allow_failure) {
                    CONDOR_LOG_ERROR("Stage '" + stage.config.name + "' post-hook failed with exit code " + std::to_string(post_rc));
                    stage.status = JobStatus::FAILED;
                }
            }
        }
    }
}

ProgressStats Scheduler::compute_progress_stats() const {
    ProgressStats stats;
    stats.total_jobs = jobs_.size();

    for (const auto& [jid, job] : jobs_) {
        switch (job.status) {
            case JobStatus::QUEUED:         stats.queued_jobs++; break;
            case JobStatus::RUNNING:        stats.running_jobs++; break;
            case JobStatus::PASSED:         stats.passed_jobs++; break;
            case JobStatus::FAILED:         stats.failed_jobs++; break;
            case JobStatus::FAILED_ALLOWED: stats.passed_jobs++; break; // Counts toward completed/allowed
            case JobStatus::SKIPPED:        stats.skipped_jobs++; break;
            case JobStatus::ABORTED:        stats.failed_jobs++; break;
            case JobStatus::TIMEOUT:        stats.failed_jobs++; break;
        }
    }

    auto now = std::chrono::steady_clock::now();
    stats.elapsed_sec = std::chrono::duration<double>(now - pipeline_start_time_).count();
    return stats;
}

std::vector<StageSummaryInfo> Scheduler::compute_stage_summaries() const {
    std::vector<StageSummaryInfo> summaries;
    for (const auto& stage : stages_) {
        StageSummaryInfo sinfo;
        sinfo.stage_name = stage.config.name;
        sinfo.total = stage.job_ids.size();
        for (const auto& jid : stage.job_ids) {
            auto it = jobs_.find(jid);
            if (it != jobs_.end()) {
                switch (it->second.status) {
                    case JobStatus::PASSED:
                    case JobStatus::FAILED_ALLOWED: sinfo.passed++; break;
                    case JobStatus::RUNNING:        sinfo.running++; break;
                    case JobStatus::FAILED:
                    case JobStatus::ABORTED:
                    case JobStatus::TIMEOUT:        sinfo.failed++; break;
                    case JobStatus::SKIPPED:        sinfo.skipped++; break;
                    default: break;
                }
            }
        }
        summaries.push_back(sinfo);
    }
    return summaries;
}

std::vector<ActiveJobInfo> Scheduler::get_active_job_infos() const {
    std::vector<ActiveJobInfo> list;
    auto now = std::chrono::steady_clock::now();
    for (const auto& [jid, job] : jobs_) {
        if (job.status == JobStatus::RUNNING && job.exec_ctx) {
            ActiveJobInfo a;
            a.job_id = job.config.job_id;
            a.stage_name = job.config.stage_name;
            a.job_name = job.config.name;
            a.pid_or_grid_id = job.pid_or_grid_id;
            a.elapsed_sec = std::chrono::duration<double>(now - job.exec_ctx->start_time).count();
            list.push_back(a);
        }
    }
    return list;
}

void Scheduler::print_status_table() {
    std::lock_guard<std::mutex> lock(mutex_);
    int cols = get_terminal_width(108);
    int div_len = std::max(cols, 108);
    std::string div(div_len, '=');

    std::cout << "\n"
              << std::left
              << std::setw(8)  << "JOB_ID"
              << std::setw(20) << "JOB_NAME"
              << std::setw(15) << "STAGE"
              << std::setw(16) << "STATUS"
              << std::setw(12) << "PID/GRID"
              << std::setw(10) << "ELAPSED"
              << std::setw(12) << "PEAK_RSS"
              << "DIRECTORY\n";
    std::cout << div << "\n";

    auto now = std::chrono::steady_clock::now();
    for (const auto& stage : stages_) {
        for (const auto& jid : stage.job_ids) {
            const auto& job = jobs_[jid];
            double elapsed = job.elapsed_sec;
            if (job.status == JobStatus::RUNNING && job.exec_ctx) {
                elapsed = std::chrono::duration<double>(now - job.exec_ctx->start_time).count();
            }

            std::string rss_str = "-";
            if (job.peak_rss_mb > 0.0) {
                std::ostringstream rss_ss;
                rss_ss << std::fixed << std::setprecision(1) << job.peak_rss_mb << " MB";
                rss_str = rss_ss.str();
            }

            std::cout << std::left
                      << std::setw(8)  << job.config.job_id
                      << std::setw(20) << job.config.name
                      << std::setw(15) << job.config.stage_name
                      << std::setw(16) << to_string(job.status)
                      << std::setw(12) << job.pid_or_grid_id
                      << std::setw(10) << ProgressDisplay::format_duration(elapsed)
                      << std::setw(12) << rss_str
                      << workspace_.job_dir(job.config.stage_name, job.config.job_id).string()
                      << "\n";
        }
    }
    std::cout << div << "\n";
    if (!config_.resources.empty()) {
        std::unordered_map<std::string, int> active_res;
        for (const auto& [jid, job] : jobs_) {
            if (job.status == JobStatus::RUNNING) {
                for (const auto& [rname, req] : job.config.resources) {
                    active_res[rname] += req;
                }
            }
        }
        std::cout << "Resources: ";
        size_t ri = 0;
        for (const auto& [rname, cap] : config_.resources) {
            int used = active_res[rname];
            std::cout << rname << ": " << used << "/" << cap << " used"
                      << (++ri < config_.resources.size() ? " | " : "");
        }
        std::cout << "\n" << div << "\n";
    }
    std::cout << std::endl;
}

void Scheduler::tail_job(std::string_view target, size_t lines) {
    std::lock_guard<std::mutex> lock(mutex_);
    const JobRuntimeState* found_job = nullptr;

    if (target.empty()) {
        for (const auto& [jid, job] : jobs_) {
            if (job.status == JobStatus::RUNNING) {
                found_job = &job;
                break;
            }
        }
        if (!found_job) {
            std::cout << "\n[SCHEDULER] No active running jobs to tail. Specify a job ID (e.g. 't 0001').\n";
            return;
        }
    } else {
        auto it = jobs_.find(std::string(target));
        if (it != jobs_.end()) {
            found_job = &it->second;
        } else {
            for (const auto& [jid, job] : jobs_) {
                if (job.config.job_id == target || job.config.name == target) {
                    found_job = &job;
                    break;
                }
            }
        }
        if (!found_job) {
            std::cout << "\n[SCHEDULER] Job ID or Name '" << target << "' not found.\n";
            return;
        }
    }

    auto job_dir = workspace_.job_dir(found_job->config.stage_name, found_job->config.job_id);
    auto stdout_path = job_dir / "stdout.log";
    auto stderr_path = job_dir / "stderr.err";

    auto tail_file = [](const std::filesystem::path& p, size_t n_lines) -> std::vector<std::string> {
        std::vector<std::string> res;
        if (!std::filesystem::exists(p)) return res;
        std::ifstream ifs(p);
        if (!ifs) return res;
        std::string line;
        std::deque<std::string> dq;
        while (std::getline(ifs, line)) {
            dq.push_back(line);
            if (dq.size() > n_lines) dq.pop_front();
        }
        return std::vector<std::string>(dq.begin(), dq.end());
    };

    int cols = get_terminal_width(80);
    std::string div(std::clamp(cols, 60, 100), '=');
    std::string sub_div(std::clamp(cols, 60, 100), '-');

    std::cout << "\n" << div << "\n"
              << "Tail: Job " << found_job->config.job_id << " (" << found_job->config.name << ")"
              << " | Stage: " << found_job->config.stage_name
              << " | Status: " << to_string(found_job->status)
              << "\n" << div << "\n";

    auto out_lines = tail_file(stdout_path, lines);
    std::cout << "--- Standard Output (" << stdout_path.string() << ") ---\n";
    if (out_lines.empty()) {
        std::cout << "(stdout is empty or log file not yet created)\n";
    } else {
        for (const auto& l : out_lines) {
            std::cout << l << "\n";
        }
    }

    if (std::filesystem::exists(stderr_path)) {
        auto err_lines = tail_file(stderr_path, lines);
        if (!err_lines.empty()) {
            std::cout << sub_div << "\n"
                      << "--- Standard Error (" << stderr_path.string() << ") ---\n";
            for (const auto& l : err_lines) {
                std::cout << l << "\n";
            }
        }
    }
    std::cout << div << "\n" << std::endl;
}

void Scheduler::toggle_pause() {
    dispatch_paused_ = !dispatch_paused_;
    std::cout << "\n[SCHEDULER] Job dispatching is now "
              << (dispatch_paused_ ? "PAUSED (active jobs will finish)" : "RESUMED")
              << std::endl;
}

void Scheduler::kill_job(std::string_view target) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = jobs_.find(std::string(target));
    if (it != jobs_.end()) {
        if (it->second.status == JobStatus::RUNNING && it->second.exec_ctx) {
            std::cout << "\n[SCHEDULER] Terminating job " << target << "..." << std::endl;
            it->second.backend->cancel(*it->second.exec_ctx);
            it->second.status = JobStatus::ABORTED;
            sync_stage_manifest(it->second.config.stage_name);
        } else {
            std::cout << "\n[SCHEDULER] Job " << target << " is not currently running.\n";
        }
        return;
    }

    bool found = false;
    for (auto& [k, job] : jobs_) {
        if (job.config.job_id == target || job.config.name == target) {
            found = true;
            if (job.status == JobStatus::RUNNING && job.exec_ctx) {
                std::cout << "\n[SCHEDULER] Terminating job " << k << " (" << job.config.name << ")..." << std::endl;
                job.backend->cancel(*job.exec_ctx);
                job.status = JobStatus::ABORTED;
                sync_stage_manifest(job.config.stage_name);
            } else {
                std::cout << "\n[SCHEDULER] Job " << k << " (" << job.config.name << ") is not currently running.\n";
            }
        }
    }
    if (!found) {
        std::cout << "\n[SCHEDULER] Job ID or Name '" << target << "' not found.\n";
    }
}

void Scheduler::scale_slots(size_t new_slots) {
    max_slots_ = new_slots;
    std::cout << "\n[SCHEDULER] Max parallel slots scaled to: " << new_slots << std::endl;
}

void Scheduler::abort_all() {
    aborted_ = true;
    for (auto& [jid, job] : jobs_) {
        if (job.status == JobStatus::RUNNING && job.exec_ctx) {
            job.backend->cancel(*job.exec_ctx);
            job.status = JobStatus::ABORTED;
            sync_stage_manifest(job.config.stage_name);
        } else if (job.status == JobStatus::QUEUED) {
            job.status = JobStatus::ABORTED;
            sync_stage_manifest(job.config.stage_name);
        }
    }
}

bool Scheduler::run() {
    init_runtime_states();
    RuntimeMenu::init_signal_handlers(this);

    // Global Pre-hook
    int global_pre_rc = execute_hook(config_.hooks, true);
    if (global_pre_rc != 0 && !config_.hooks.allow_failure) {
        CONDOR_LOG_ERROR("Global pipeline pre-hook failed with exit code " + std::to_string(global_pre_rc));
        return false;
    }

    while (true) {
        if (RuntimeMenu::is_hard_kill_triggered()) {
            abort_all();
            break;
        }

        if (RuntimeMenu::is_menu_requested()) {
            progress_->set_paused(true);
            RuntimeMenu::run_menu(*this);
            progress_->set_paused(false);
        }

        RuntimeMenu::poll_stdin_for_menu();

        poll_running_jobs();
        dispatch_ready_jobs();

        auto stats = compute_progress_stats();
        auto stages = compute_stage_summaries();
        auto active = get_active_job_infos();
        progress_->update(stats, stages, active);

        // Check if all jobs are done
        bool all_done = true;
        for (const auto& [jid, job] : jobs_) {
            if (job.status == JobStatus::QUEUED || job.status == JobStatus::RUNNING) {
                all_done = false;
                break;
            }
        }

        if (all_done) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }

    auto final_stats = compute_progress_stats();
    bool pipeline_success = (final_stats.failed_jobs == 0 && !aborted_);

    // Global Post-hook
    int global_post_rc = execute_hook(config_.hooks, false, "", "", pipeline_success ? 0 : 1);
    if (global_post_rc != 0 && !config_.hooks.allow_failure) {
        CONDOR_LOG_ERROR("Global pipeline post-hook failed with exit code " + std::to_string(global_post_rc));
        pipeline_success = false;
    }

    // Generate tool-agnostic HTML report (includes all stage and job hook logs)
    std::filesystem::path report_html_path = workspace_.artifacts_dir() / "report.html";
    HtmlReportGenerator::write_report(report_html_path, config_, workspace_, stages_, jobs_, final_stats, pipeline_success);

    // Purge successful job logs if enabled to conserve disk space
    if (config_.settings.purge_successful_logs) {
        for (const auto& [jid, job] : jobs_) {
            if (job.status == JobStatus::PASSED) {
                auto jdir = workspace_.job_dir(job.config.stage_name, job.config.job_id);
                std::error_code ec;
                std::filesystem::remove(jdir / "stdout.log", ec);
                std::filesystem::remove(jdir / "stderr.err", ec);
                std::filesystem::remove(jdir / "job_runner.sh", ec);
            }
        }
    }

    // Prune historical runs if retention limit set
    if (config_.settings.max_retained_runs > 0) {
        workspace_.prune_old_runs(config_.settings.max_retained_runs);
    }

    if (config_.settings.emit_junit_xml) {
        std::filesystem::path junit_path = !config_.settings.junit_xml_path.empty()
            ? std::filesystem::path(config_.settings.junit_xml_path)
            : (workspace_.artifacts_dir() / "junit_report.xml");
        HtmlReportGenerator::write_junit_xml(junit_path, config_, stages_, jobs_, final_stats, pipeline_success);
    }

    // Write internal pipeline operational summary into pipeline/
    {
        std::ofstream sum_out(workspace_.pipeline_dir() / "pipeline_summary.yaml", std::ios::trunc);
        if (sum_out) {
            sum_out << "pipeline: \"" << config_.name << "\"\n"
                    << "status: \"" << (pipeline_success ? "PASSED" : "FAILED") << "\"\n"
                    << "total_jobs: " << final_stats.total_jobs << "\n"
                    << "passed_jobs: " << final_stats.passed_jobs << "\n"
                    << "failed_jobs: " << final_stats.failed_jobs << "\n"
                    << "skipped_jobs: " << final_stats.skipped_jobs << "\n"
                    << std::fixed << std::setprecision(3)
                    << "elapsed_sec: " << final_stats.elapsed_sec << "\n";
        }
    }

    progress_->print_final_summary(final_stats, pipeline_success, report_html_path.string());

    return pipeline_success;
}

} // namespace condor
