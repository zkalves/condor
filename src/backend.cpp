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

#include "condor/backend.hpp"
#include "condor/logging.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <cstring>
#include <array>
#include <regex>
#include <climits>
#include <yaml-cpp/yaml.h>

namespace condor {

std::string LocalBackend::generate_runner_script(const JobExecutionContext& ctx) {
    const std::string& target_cd = ctx.physical_job_dir.empty() ? ctx.job_dir : ctx.physical_job_dir;
    std::ostringstream ss;
    ss << "#!/usr/bin/env bash\n"
       << "# ==============================================================================\n"
       << "# Condor Generated Job Runner Script\n"
       << "# Stage:         " << ctx.stage_name << "\n"
       << "# Job ID:        " << ctx.job_id << "\n"
       << "# Job Name:      " << ctx.job_name << "\n"
       << "# Run Directory: " << ctx.run_dir << "\n"
       << "# Job Directory: " << ctx.job_dir << "\n"
       << "#\n"
       << "# Reproduce this job standalone:\n"
       << "#   cd \"" << target_cd << "\"\n"
       << "#   ./job_runner.sh\n"
       << "# ==============================================================================\n"
       << "set -o pipefail\n\n"
       << "# Export Built-in Condor Variables\n"
       << "export CONDOR_RUN_DIR=\"" << ctx.run_dir << "\"\n"
       << "export CONDOR_STAGE_NAME=\"" << ctx.stage_name << "\"\n"
       << "export CONDOR_STAGE_DIR=\"" << ctx.stage_dir << "\"\n"
       << "export CONDOR_JOB_ID=\"" << ctx.job_id << "\"\n"
       << "export CONDOR_JOB_NAME=\"" << ctx.job_name << "\"\n"
       << "export CONDOR_JOB_DIR=\"" << ctx.job_dir << "\"\n"
       << "export CONDOR_OUTPUT_MODE=\"" << ctx.output_mode << "\"\n";

    if (!ctx.build_dir.empty()) {
        ss << "export BUILD_DIR=\"" << ctx.build_dir << "\"\n"
           << "export CONDOR_BUILD_DIR=\"" << ctx.build_dir << "\"\n";
    }
    if (!ctx.test_dir.empty()) {
        ss << "export TEST_DIR=\"" << ctx.test_dir << "\"\n"
           << "export CONDOR_TEST_DIR=\"" << ctx.test_dir << "\"\n";
    }

    ss << "\n# Export User & Environment Variables\n";
    for (const auto& [k, v] : ctx.env_vars) {
        ss << "export " << k << "=\"" << v << "\"\n";
    }

    ss << "\n# Navigate to Job Directory\n"
       << "cd \"$CONDOR_JOB_DIR\" || exit 1\n\n";

    if (!ctx.pre_hook.empty()) {
        ss << "# --- Pre-Hook ---\n"
           << "echo \"[CONDOR RUNNER] Executing pre-hook:\"\n"
           << "echo \"  " << ctx.pre_hook << "\"\n"
           << "(\n"
           << "  set -e\n"
           << ctx.pre_hook << "\n"
           << ") > pre_hook.log 2>&1\n"
           << "PRE_STATUS=$?\n"
           << "cat pre_hook.log\n"
           << "if [ $PRE_STATUS -ne 0 ]; then\n";
        if (ctx.hook_allow_failure) {
            ss << "    echo \"[CONDOR RUNNER] Pre-hook failed with exit code $PRE_STATUS (allow_failure=true; continuing)\" >&2\n";
        } else {
            ss << "    echo \"[CONDOR RUNNER] Pre-hook failed with exit code $PRE_STATUS\" >&2\n"
               << "    exit $PRE_STATUS\n";
        }
        ss << "fi\n\n";
    }

    ss << "# --- Main Command ---\n"
       << "echo \"[CONDOR RUNNER] Executing command:\"\n"
       << "echo \"  " << ctx.command << "\"\n"
       << "echo \"\"\n"
       << ctx.command << "\n"
       << "CMD_STATUS=$?\n\n";

    if (!ctx.post_hook.empty()) {
        ss << "# --- Post-Hook ---\n";
        auto emit_post_body = [&]() {
            ss << "(\n"
               << "  set -e\n"
               << ctx.post_hook << "\n"
               << ") > post_hook.log 2>&1\n"
               << "POST_STATUS=$?\n"
               << "cat post_hook.log\n"
               << "if [ $POST_STATUS -ne 0 ]; then\n";
            if (!ctx.hook_allow_failure) {
                ss << "    echo \"[CONDOR RUNNER] Post-hook failed with exit code $POST_STATUS\" >&2\n"
                   << "    if [ $CMD_STATUS -eq 0 ]; then\n"
                   << "        CMD_STATUS=$POST_STATUS\n"
                   << "    fi\n";
            } else {
                ss << "    echo \"[CONDOR RUNNER] Post-hook failed with exit code $POST_STATUS (allow_failure=true; ignored)\" >&2\n";
            }
            ss << "fi\n";
        };

        if (ctx.post_always) {
            ss << "echo \"[CONDOR RUNNER] Executing post-hook (always):\"\n"
               << "echo \"  " << ctx.post_hook << "\"\n";
            emit_post_body();
            ss << "\n";
        } else {
            ss << "if [ $CMD_STATUS -eq 0 ]; then\n"
               << "    echo \"[CONDOR RUNNER] Executing post-hook:\"\n"
               << "    echo \"  " << ctx.post_hook << "\"\n";
            emit_post_body();
            ss << "fi\n\n";
        }
    }

    ss << "exit $CMD_STATUS\n";
    return ss.str();
}

void LocalBackend::submit(JobExecutionContext& ctx) {
    const std::string& pdir = ctx.physical_job_dir.empty() ? ctx.job_dir : ctx.physical_job_dir;
    std::string script_path = pdir + "/job_runner.sh";
    {
        std::ofstream script_file(script_path, std::ios::trunc);
        if (!script_file) {
            throw ExecutionError("Failed to write runner script: " + script_path);
        }
        script_file << generate_runner_script(ctx);
    }
    chmod(script_path.c_str(), 0755);

    pid_t pid = fork();
    if (pid < 0) {
        throw ExecutionError("fork() failed for job " + ctx.job_id + ": " + std::strerror(errno));
    }

    if (pid == 0) {
        // In Child Process
        // Set new process group ID so child + descendants can be killed cleanly
        setpgid(0, 0);

        // Chdir into pdir
        if (chdir(pdir.c_str()) != 0) {
            std::cerr << "Child failed to chdir to " << pdir << ": " << std::strerror(errno) << std::endl;
            _exit(1);
        }

        // Redirect stdout to stdout.log
        std::string stdout_path = pdir + "/stdout.log";
        int out_fd = open(stdout_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (out_fd >= 0) {
            dup2(out_fd, STDOUT_FILENO);
            close(out_fd);
        }

        // Redirect stderr to stderr.err
        std::string stderr_path = pdir + "/stderr.err";
        int err_fd = open(stderr_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (err_fd >= 0) {
            dup2(err_fd, STDERR_FILENO);
            close(err_fd);
        }

        // Export built-in environment variables
        setenv("CONDOR_RUN_DIR", ctx.run_dir.c_str(), 1);
        setenv("CONDOR_STAGE_NAME", ctx.stage_name.c_str(), 1);
        setenv("CONDOR_STAGE_DIR", ctx.stage_dir.c_str(), 1);
        setenv("CONDOR_JOB_ID", ctx.job_id.c_str(), 1);
        setenv("CONDOR_JOB_NAME", ctx.job_name.c_str(), 1);
        setenv("CONDOR_JOB_DIR", ctx.job_dir.c_str(), 1);
        setenv("CONDOR_OUTPUT_MODE", ctx.output_mode.c_str(), 1);
        if (!ctx.build_dir.empty()) {
            setenv("BUILD_DIR", ctx.build_dir.c_str(), 1);
            setenv("CONDOR_BUILD_DIR", ctx.build_dir.c_str(), 1);
        }
        if (!ctx.test_dir.empty()) {
            setenv("TEST_DIR", ctx.test_dir.c_str(), 1);
            setenv("CONDOR_TEST_DIR", ctx.test_dir.c_str(), 1);
        }
        setenv("CONDOR_STATUS", "RUNNING", 1);
        setenv("CONDOR_COMMAND", ctx.command.c_str(), 1);
        setenv("CONDOR_PRE_HOOK", ctx.pre_hook.c_str(), 1);
        setenv("CONDOR_POST_HOOK", ctx.post_hook.c_str(), 1);
        setenv("CONDOR_POST_ALWAYS", ctx.post_always ? "1" : "0", 1);

        // Export user variables (CONDOR_VAR_<KEY>=<VAL>)
        for (const auto& [key, val] : ctx.env_vars) {
            setenv(key.c_str(), val.c_str(), 1);
        }

        // Exec runner script
        char* const argv[] = {
            const_cast<char*>("bash"),
            const_cast<char*>("job_runner.sh"),
            nullptr
        };
        execvp("bash", argv);

        std::cerr << "execvp failed: " << std::strerror(errno) << std::endl;
        _exit(127);
    }

    // In Parent Process
    ctx.pid = pid;
    ctx.grid_id = std::to_string(pid);
    ctx.status = JobStatus::RUNNING;
    ctx.start_time = std::chrono::steady_clock::now();
}

PollResult LocalBackend::poll(JobExecutionContext& ctx) {
    if (ctx.pid <= 0) {
        return {true, JobStatus::FAILED, -1};
    }

    int status = 0;
    pid_t ret = waitpid(ctx.pid, &status, WNOHANG);

    if (ret == 0) {
        // Still running
        return {false, JobStatus::RUNNING, 0};
    } else if (ret == ctx.pid) {
        // Child exited
        ctx.end_time = std::chrono::steady_clock::now();
        int exit_code = 0;
        JobStatus js = JobStatus::PASSED;

        if (WIFEXITED(status)) {
            exit_code = WEXITSTATUS(status);
            js = (exit_code == 0) ? JobStatus::PASSED : JobStatus::FAILED;
        } else if (WIFSIGNALED(status)) {
            int sig = WTERMSIG(status);
            exit_code = 128 + sig;
            js = (sig == SIGINT || sig == SIGTERM || sig == SIGKILL) ? JobStatus::ABORTED : JobStatus::FAILED;
        }

        ctx.exit_code = exit_code;
        ctx.status = js;
        return {true, js, exit_code};
    } else if (ret < 0) {
        if (errno == ECHILD) {
            // Already reaped
            ctx.end_time = std::chrono::steady_clock::now();
            return {true, ctx.status, ctx.exit_code};
        }
        return {true, JobStatus::FAILED, -1};
    }

    return {false, JobStatus::RUNNING, 0};
}

void LocalBackend::cancel(JobExecutionContext& ctx) {
    if (ctx.pid > 0) {
        CONDOR_LOG_DEBUG("Cancelling job " + ctx.job_id + " (PGID: " + std::to_string(ctx.pid) + ")");
        // Send SIGTERM to entire process group (-pid)
        kill(-ctx.pid, SIGTERM);
        usleep(50000); // 50ms grace period
        kill(-ctx.pid, SIGKILL);

        int status = 0;
        waitpid(ctx.pid, &status, WNOHANG);

        ctx.status = JobStatus::ABORTED;
        ctx.exit_code = 137;
        ctx.end_time = std::chrono::steady_clock::now();
    }
}

// --- TemplateBackend Implementation ---

std::string TemplateBackend::substitute(std::string_view template_str, const JobExecutionContext& ctx) const {
    std::string res(template_str);

    auto replace_all = [&](std::string_view placeholder, const std::string& replacement) {
        std::string target = std::string(placeholder);
        size_t pos = 0;
        while ((pos = res.find(target, pos)) != std::string::npos) {
            res.replace(pos, target.length(), replacement);
            pos += replacement.length();
        }
    };

    const std::string& pdir = ctx.physical_job_dir.empty() ? ctx.job_dir : ctx.physical_job_dir;
    replace_all("{job_id}", ctx.job_id);
    replace_all("{job_name}", ctx.job_name);
    replace_all("{job_dir}", ctx.job_dir);
    replace_all("{stage_dir}", ctx.stage_dir);
    replace_all("{run_dir}", ctx.run_dir);
    replace_all("{script}", pdir + "/job_runner.sh");
    replace_all("{grid_id}", ctx.grid_id);

    // Support ${VAR} and $VAR variable interpolation from execution context
    for (const auto& [k, v] : ctx.env_vars) {
        replace_all("${" + k + "}", v);
        replace_all("$" + k, v);
        if (k.rfind("CONDOR_VAR_", 0) == 0) {
            std::string raw_k = k.substr(11);
            replace_all("${" + raw_k + "}", v);
            replace_all("$" + raw_k, v);
        }
    }

    return res;
}

std::string TemplateBackend::execute_capture(const std::string& cmd) {
    std::array<char, 256> buffer;
    std::string result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return "";
    }
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }
    pclose(pipe);

    // Trim trailing whitespace
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' ')) {
        result.pop_back();
    }
    return result;
}

void TemplateBackend::submit(JobExecutionContext& ctx) {
    const std::string& pdir = ctx.physical_job_dir.empty() ? ctx.job_dir : ctx.physical_job_dir;
    std::string script_path = pdir + "/job_runner.sh";
    {
        std::ofstream script_file(script_path, std::ios::trunc);
        if (!script_file) {
            throw ExecutionError("Failed to write runner script: " + script_path);
        }
        script_file << LocalBackend::generate_runner_script(ctx);
    }
    chmod(script_path.c_str(), 0755);

    std::string submit_cmd = substitute(config_.submit_command, ctx);
    CONDOR_LOG_DEBUG("Template submit command: " + submit_cmd);
    std::string output = execute_capture(submit_cmd);

    std::string id;
    if (!config_.job_id_regex.empty()) {
        try {
            std::regex re(config_.job_id_regex);
            std::smatch match;
            if (std::regex_search(output, match, re)) {
                if (match.size() > 1) {
                    id = match[1].str();
                } else {
                    id = match[0].str();
                }
            }
        } catch (const std::exception& e) {
            CONDOR_LOG_WARN("Failed regex matching for job_id: " + std::string(e.what()));
        }
    }

    if (id.empty()) {
        std::istringstream iss(output);
        iss >> id;
    }
    if (id.empty()) {
        id = "UNKNOWN";
    }

    ctx.grid_id = id;
    ctx.status = JobStatus::RUNNING;
    ctx.start_time = std::chrono::steady_clock::now();
}

PollResult TemplateBackend::poll(JobExecutionContext& ctx) {
    std::string poll_cmd = substitute(config_.poll_command, ctx);
    std::string output = execute_capture(poll_cmd);

    if (output.empty()) {
        if (config_.empty_poll_means_done) {
            ctx.status = JobStatus::PASSED;
            ctx.exit_code = 0;
            ctx.end_time = std::chrono::steady_clock::now();
            return {true, JobStatus::PASSED, 0};
        }
        return {false, JobStatus::RUNNING, 0};
    }

    // Check failure patterns first
    std::string fail_pat = !config_.failure_pattern.empty()
        ? config_.failure_pattern
        : "FAILED|CANCELLED|TIMEOUT|NODE_FAIL|EXIT|KILLED|ERROR|LOST";
    try {
        std::regex fail_re(fail_pat, std::regex::icase);
        if (std::regex_search(output, fail_re)) {
            ctx.status = JobStatus::FAILED;
            ctx.exit_code = 1;
            ctx.end_time = std::chrono::steady_clock::now();
            return {true, JobStatus::FAILED, 1};
        }
    } catch (...) {}

    // Check success patterns
    std::string succ_pat = !config_.success_pattern.empty()
        ? config_.success_pattern
        : "COMPLETED|DONE|FINISHED|PASSED|SUCCESS";
    try {
        std::regex succ_re(succ_pat, std::regex::icase);
        if (std::regex_search(output, succ_re)) {
            ctx.status = JobStatus::PASSED;
            ctx.exit_code = 0;
            ctx.end_time = std::chrono::steady_clock::now();
            return {true, JobStatus::PASSED, 0};
        }
    } catch (...) {}

    // Check running patterns
    std::string run_pat = !config_.running_pattern.empty()
        ? config_.running_pattern
        : "RUNNING|PENDING|PEND|RUN|SUBMITTED|QUEUED|WAITING|CONFIGURING";
    try {
        std::regex run_re(run_pat, std::regex::icase);
        if (std::regex_search(output, run_re)) {
            return {false, JobStatus::RUNNING, 0};
        }
    } catch (...) {}

    // Still running / queued in grid
    return {false, JobStatus::RUNNING, 0};
}

void TemplateBackend::cancel(JobExecutionContext& ctx) {
    if (!ctx.grid_id.empty() && ctx.grid_id != "UNKNOWN") {
        std::string cancel_cmd = substitute(config_.cancel_command, ctx);
        CONDOR_LOG_DEBUG("Template cancel command: " + cancel_cmd);
        execute_capture(cancel_cmd);
    }
    ctx.status = JobStatus::ABORTED;
    ctx.exit_code = 137;
    ctx.end_time = std::chrono::steady_clock::now();
}

std::unique_ptr<ExecutionBackend> BackendFactory::create(const BackendConfig& config) {
    if (config.type == BackendType::LOCAL) {
        return std::make_unique<LocalBackend>();
    }
    return std::make_unique<TemplateBackend>(config);
}

std::vector<std::filesystem::path> BackendRegistry::get_search_paths(const std::string& pipeline_dir) {
    std::vector<std::filesystem::path> paths;

    // 1. CONDOR_BACKEND_DIR / CONDOR_BACKEND_PATH environment variables
    const char* env_dir = std::getenv("CONDOR_BACKEND_DIR");
    if (env_dir && *env_dir) {
        paths.emplace_back(env_dir);
    }
    const char* env_path = std::getenv("CONDOR_BACKEND_PATH");
    if (env_path && *env_path) {
        std::string pstr(env_path);
        std::stringstream ss(pstr);
        std::string item;
        while (std::getline(ss, item, ':')) {
            if (!item.empty()) paths.emplace_back(item);
        }
    }

    // 2. Pipeline directory
    if (!pipeline_dir.empty()) {
        paths.emplace_back(std::filesystem::path(pipeline_dir) / "backends");
        paths.emplace_back(pipeline_dir);
    }

    // 3. Current working directory
    paths.emplace_back("./backends");
    paths.emplace_back("./res/backends");
    paths.emplace_back(".");

    // 4. User configuration directory ($HOME/.config/condor/backends)
    const char* home = std::getenv("HOME");
    if (home && *home) {
        paths.emplace_back(std::filesystem::path(home) / ".config" / "condor" / "backends");
    }

    // 5. Binary relative paths (/proc/self/exe on Linux)
    char exe_buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_buf, sizeof(exe_buf) - 1);
    if (len > 0) {
        exe_buf[len] = '\0';
        std::filesystem::path exe_path(exe_buf);
        std::filesystem::path exe_dir = exe_path.parent_path();
        paths.emplace_back(exe_dir.parent_path() / "share" / "condor" / "backends");
        paths.emplace_back(exe_dir.parent_path() / "res" / "backends");
        paths.emplace_back(exe_dir / "res" / "backends");
        paths.emplace_back(exe_dir / "backends");
    }

    // 6. System standard paths
    paths.emplace_back("/usr/local/share/condor/backends");
    paths.emplace_back("/usr/share/condor/backends");
    paths.emplace_back("/etc/condor/backends");

    return paths;
}

BackendConfig BackendRegistry::parse_backend_file(const std::filesystem::path& filepath) {
    YAML::Node root;
    try {
        root = YAML::LoadFile(filepath.string());
    } catch (const std::exception& e) {
        throw ConfigurationError("Failed to parse backend configuration file '" + filepath.string() + "': " + e.what());
    }

    BackendConfig cfg;
    std::string stem = filepath.stem().string();
    cfg.name = stem;
    cfg.type = BackendType::TEMPLATE;

    YAML::Node node = root;
    // Check if nested under backends: { <name>: { ... } }
    if (root["backends"] && root["backends"].IsMap()) {
        for (auto it = root["backends"].begin(); it != root["backends"].end(); ++it) {
            cfg.name = it->first.as<std::string>();
            node = it->second;
            break;
        }
    } else if (root["name"]) {
        cfg.name = root["name"].as<std::string>();
    }

    if (node["description"]) cfg.description = node["description"].as<std::string>();
    if (node["type"]) {
        std::string t = node["type"].as<std::string>();
        cfg.type = (t == "local") ? BackendType::LOCAL : BackendType::TEMPLATE;
    }
    if (node["submit_command"]) cfg.submit_command = node["submit_command"].as<std::string>();
    if (node["poll_command"])   cfg.poll_command   = node["poll_command"].as<std::string>();
    if (node["cancel_command"]) cfg.cancel_command = node["cancel_command"].as<std::string>();
    if (node["job_id_regex"])   cfg.job_id_regex   = node["job_id_regex"].as<std::string>();
    if (node["success_pattern"]) cfg.success_pattern = node["success_pattern"].as<std::string>();
    if (node["failure_pattern"]) cfg.failure_pattern = node["failure_pattern"].as<std::string>();
    if (node["running_pattern"]) cfg.running_pattern = node["running_pattern"].as<std::string>();
    if (node["empty_poll_means_done"]) cfg.empty_poll_means_done = node["empty_poll_means_done"].as<bool>();

    return cfg;
}

std::optional<BackendConfig> BackendRegistry::load_backend(const std::string& backend_name, const std::string& pipeline_dir) {
    auto search_paths = get_search_paths(pipeline_dir);
    for (const auto& p : search_paths) {
        std::error_code ec;
        if (!std::filesystem::exists(p, ec) || !std::filesystem::is_directory(p, ec)) {
            continue;
        }
        std::filesystem::path candidate_yaml = p / (backend_name + ".yaml");
        if (std::filesystem::exists(candidate_yaml, ec) && std::filesystem::is_regular_file(candidate_yaml, ec)) {
            return parse_backend_file(candidate_yaml);
        }
        std::filesystem::path candidate_yml = p / (backend_name + ".yml");
        if (std::filesystem::exists(candidate_yml, ec) && std::filesystem::is_regular_file(candidate_yml, ec)) {
            return parse_backend_file(candidate_yml);
        }
    }
    return std::nullopt;
}

std::unordered_map<std::string, BackendConfig> BackendRegistry::discover_backends(const std::string& pipeline_dir) {
    std::unordered_map<std::string, BackendConfig> backends;
    auto search_paths = get_search_paths(pipeline_dir);
    for (const auto& p : search_paths) {
        std::error_code ec;
        if (!std::filesystem::exists(p, ec) || !std::filesystem::is_directory(p, ec)) {
            continue;
        }
        for (const auto& entry : std::filesystem::directory_iterator(p, ec)) {
            if (!entry.is_regular_file()) continue;
            std::string ext = entry.path().extension().string();
            if (ext == ".yaml" || ext == ".yml") {
                try {
                    BackendConfig cfg = parse_backend_file(entry.path());
                    if (!cfg.name.empty() && backends.find(cfg.name) == backends.end()) {
                        backends[cfg.name] = cfg;
                    }
                } catch (const std::exception& e) {
                    CONDOR_LOG_DEBUG("Failed parsing candidate backend file " + entry.path().string() + ": " + e.what());
                }
            }
        }
    }
    return backends;
}

} // namespace condor
