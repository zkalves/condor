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

#include "condor/workspace.hpp"
#include "condor/logging.hpp"
#include <random>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unistd.h>
#include <signal.h>
#include <cstdlib>

namespace condor {

std::string WorkspaceManager::generate_run_folder_name() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
    localtime_r(&tt, &tm_buf);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dis(100000, 999999);
    int rand_digits = dis(gen);

    std::ostringstream ss;
    ss << std::put_time(&tm_buf, "%Y%m%d_%H%M%S") << "_" << rand_digits;
    return ss.str();
}

WorkspaceManager::WorkspaceManager(std::string_view base_output_dir,
                                   std::string_view fixed_run_dir) {
    if (!fixed_run_dir.empty()) {
        run_dir_ = std::filesystem::absolute(fixed_run_dir);
        base_dir_ = run_dir_.parent_path();
        is_fixed_ = true;
    } else {
        base_dir_ = std::filesystem::absolute(base_output_dir);
        run_dir_ = base_dir_ / generate_run_folder_name();
        is_fixed_ = false;
    }
}

void WorkspaceManager::initialize() {
    std::error_code ec;
    std::filesystem::create_directories(run_dir_, ec);
    if (ec) {
        throw ExecutionError("Failed to create run directory: " + run_dir_.string() + " (" + ec.message() + ")");
    }

    std::filesystem::create_directories(artifacts_dir(), ec);
    std::filesystem::create_directories(pipeline_dir(), ec);
    std::filesystem::create_directories(stages_dir(), ec);

    create_kill_script();

    if (!is_fixed_) {
        update_latest_symlink();
    }
}

std::filesystem::path WorkspaceManager::artifacts_dir() const {
    return run_dir_ / "artifacts";
}

std::filesystem::path WorkspaceManager::pipeline_dir() const {
    return run_dir_ / "pipeline";
}

std::filesystem::path WorkspaceManager::stages_dir() const {
    return run_dir_ / "stages";
}

std::filesystem::path WorkspaceManager::stage_dir(std::string_view stage_name) const {
    return run_dir_ / "stages" / stage_name;
}

std::filesystem::path WorkspaceManager::job_dir(std::string_view stage_name, std::string_view job_id) const {
    return run_dir_ / "stages" / stage_name / job_id;
}

void WorkspaceManager::create_stage_dir(std::string_view stage_name) {
    std::error_code ec;
    auto sdir = stage_dir(stage_name);
    std::filesystem::create_directories(sdir, ec);
    if (ec) {
        throw ExecutionError("Failed to create stage directory: " + sdir.string() + " (" + ec.message() + ")");
    }
}

void WorkspaceManager::create_job_dir(std::string_view stage_name, std::string_view job_id) {
    std::error_code ec;
    auto jdir = job_dir(stage_name, job_id);
    std::filesystem::create_directories(jdir, ec);
    if (ec) {
        throw ExecutionError("Failed to create job directory: " + jdir.string() + " (" + ec.message() + ")");
    }
}

void WorkspaceManager::update_stage_manifest(std::string_view stage_name,
                                             const std::vector<StageManifestEntry>& entries) {
    std::lock_guard<std::mutex> lock(manifest_mutex_);
    auto manifest_path = stage_dir(stage_name) / "stage_manifest.txt";
    std::ofstream out(manifest_path, std::ios::trunc);
    if (!out) {
        CONDOR_LOG_WARN("Could not open stage manifest for writing: " + manifest_path.string());
        return;
    }

    out << std::left
        << std::setw(10) << "JOB_ID"
        << std::setw(20) << "STATUS"
        << std::setw(25) << "JOB_NAME"
        << "COMMAND\n";
    out << std::string(75, '-') << "\n";

    for (const auto& entry : entries) {
        out << std::left
            << std::setw(10) << entry.job_id
            << std::setw(20) << to_string(entry.status)
            << std::setw(25) << entry.job_name
            << entry.command << "\n";
    }
}

void WorkspaceManager::write_job_status(std::string_view stage_name, const JobMetadata& meta) {
    auto jdir = job_dir(stage_name, meta.job_id);

    // 1. Write human-readable job_status.txt
    auto status_path = jdir / "job_status.txt";
    std::ofstream out(status_path, std::ios::trunc);
    if (!out) {
        CONDOR_LOG_WARN("Could not open job status file for writing: " + status_path.string());
        return;
    }

    out << "job_id: " << meta.job_id << "\n"
        << "job_name: " << meta.job_name << "\n"
        << "stage_name: " << meta.stage_name << "\n"
        << "pid_or_grid_id: " << meta.pid_or_grid_id << "\n"
        << "status: " << to_string(meta.status) << "\n"
        << "exit_code: " << meta.exit_code << "\n"
        << std::fixed << std::setprecision(3)
        << "elapsed_sec: " << meta.elapsed_sec << "\n";
    if (meta.peak_rss_mb > 0.0) {
        out << std::fixed << std::setprecision(2) << "peak_rss_mb: " << meta.peak_rss_mb << "\n";
    }
    if (meta.retries > 0) {
        out << "retries: " << meta.retries << "\n";
    }
    out.close();

    // 2. Write structured machine/human-readable job_status.yaml
    auto yaml_path = jdir / "job_status.yaml";
    std::ofstream y_out(yaml_path, std::ios::trunc);
    if (y_out) {
        std::string wd = meta.working_directory.empty() ? jdir.string() : meta.working_directory;
        y_out << "job_id: \"" << meta.job_id << "\"\n"
              << "job_name: \"" << meta.job_name << "\"\n"
              << "stage_name: \"" << meta.stage_name << "\"\n"
              << "status: \"" << to_string(meta.status) << "\"\n"
              << "exit_code: " << meta.exit_code << "\n"
              << std::fixed << std::setprecision(3)
              << "elapsed_sec: " << meta.elapsed_sec << "\n";
        if (meta.peak_rss_mb > 0.0) {
            y_out << std::fixed << std::setprecision(2)
                  << "peak_rss_mb: " << meta.peak_rss_mb << "\n";
        }
        if (meta.retries > 0) {
            y_out << "retries: " << meta.retries << "\n";
        }
        y_out << "pid_or_grid_id: \"" << meta.pid_or_grid_id << "\"\n"
              << "working_directory: \"" << wd << "\"\n"
              << "artifacts:\n"
              << "  stdout: \"stdout.log\"\n"
              << "  stderr: \"stderr.err\"\n"
              << "  runner: \"job_runner.sh\"\n";
    }
}

void WorkspaceManager::update_latest_symlink() {
    std::error_code ec;
    auto latest_link = base_dir_ / "latest";
    auto temp_link = base_dir_ / ("latest.tmp." + std::to_string(getpid()));

    // Create temp symlink pointing to run_dir_
    std::filesystem::remove(temp_link, ec);
    std::filesystem::create_directory_symlink(run_dir_, temp_link, ec);
    if (!ec) {
        // Atomically rename temp_link to latest_link
        std::filesystem::rename(temp_link, latest_link, ec);
        if (ec) {
            CONDOR_LOG_DEBUG("Could not atomically update latest symlink: " + ec.message());
            std::filesystem::remove(temp_link, ec);
        }
    }
}

void WorkspaceManager::prune_old_runs(size_t max_retained) {
    if (max_retained == 0) return;
    std::error_code ec;
    if (!std::filesystem::exists(base_dir_, ec)) return;

    std::vector<std::filesystem::path> run_dirs;
    for (const auto& entry : std::filesystem::directory_iterator(base_dir_, ec)) {
        if (entry.is_directory() && !std::filesystem::is_symlink(entry.path(), ec)) {
            std::string name = entry.path().filename().string();
            if (name != "latest") {
                run_dirs.push_back(entry.path());
            }
        }
    }

    if (run_dirs.size() <= max_retained) return;

    std::sort(run_dirs.begin(), run_dirs.end());

    while (run_dirs.size() > max_retained) {
        auto oldest = run_dirs.front();
        run_dirs.erase(run_dirs.begin());
        if (oldest == run_dir_) {
            break;
        }
        CONDOR_LOG_DEBUG("Pruning old run directory: " + oldest.string());
        std::filesystem::remove_all(oldest, ec);
    }
}

void WorkspaceManager::create_kill_script() {
    std::error_code ec;
    auto script_path = kill_script_path();
    std::ofstream out(script_path, std::ios::trunc);
    if (!out) {
        CONDOR_LOG_WARN("Could not create emergency kill script: " + script_path.string());
        return;
    }

    out << "#!/usr/bin/env bash\n"
        << "# ==============================================================================\n"
        << "# Condor Emergency Job Cleanup Script\n"
        << "# Run Directory: " << run_dir_.string() << "\n"
        << "# ==============================================================================\n"
        << "set -uo pipefail\n\n"
        << "SCRIPT_DIR=\"$(cd \"$(dirname \"${BASH_SOURCE[0]}\")\" && pwd)\"\n"
        << "PID_FILE=\"${SCRIPT_DIR}/active_pids.txt\"\n\n"
        << "if [ ! -f \"${PID_FILE}\" ]; then\n"
        << "    echo \"[CONDOR CLEANUP] No active_pids.txt found in ${SCRIPT_DIR}.\"\n"
        << "    exit 0\n"
        << "fi\n\n"
        << "PIDS=$(cat \"${PID_FILE}\" 2>/dev/null | tr '\\n' ' ' || true)\n"
        << "if [ -z \"${PIDS// /}\" ]; then\n"
        << "    echo \"[CONDOR CLEANUP] No active job PIDs recorded for this run.\"\n"
        << "    exit 0\n"
        << "fi\n\n"
        << "echo \"[CONDOR CLEANUP] Terminating active jobs for run in ${SCRIPT_DIR}...\"\n\n"
        << "# 1. Send SIGTERM to each process group (-PID) and individual PID\n"
        << "for PID in ${PIDS}; do\n"
        << "    if [ -n \"${PID}\" ] && kill -0 \"${PID}\" 2>/dev/null; then\n"
        << "        echo \"[CONDOR CLEANUP] Sending SIGTERM to process group -${PID}...\"\n"
        << "        kill -TERM -\"${PID}\" 2>/dev/null || kill -TERM \"${PID}\" 2>/dev/null || true\n"
        << "    fi\n"
        << "done\n\n"
        << "# 2. Wait up to 3 seconds grace period\n"
        << "for i in 1 2 3; do\n"
        << "    STILL_RUNNING=0\n"
        << "    for PID in ${PIDS}; do\n"
        << "        if [ -n \"${PID}\" ] && kill -0 \"${PID}\" 2>/dev/null; then\n"
        << "            STILL_RUNNING=1\n"
        << "            break\n"
        << "        fi\n"
        << "    done\n"
        << "    if [ \"${STILL_RUNNING}\" -eq 0 ]; then\n"
        << "        break\n"
        << "    fi\n"
        << "    sleep 1\n"
        << "done\n\n"
        << "# 3. Escalate to SIGKILL for any remaining stubborn processes\n"
        << "for PID in ${PIDS}; do\n"
        << "    if [ -n \"${PID}\" ] && kill -0 \"${PID}\" 2>/dev/null; then\n"
        << "        echo \"[CONDOR CLEANUP] Escalating to SIGKILL for process group -${PID}...\"\n"
        << "        kill -KILL -\"${PID}\" 2>/dev/null || kill -KILL \"${PID}\" 2>/dev/null || true\n"
        << "    fi\n"
        << "done\n\n"
        << "# Also clean up any lingering processes matching this run directory path\n"
        << "pkill -f \"${SCRIPT_DIR}/stages\" 2>/dev/null || true\n\n"
        << "> \"${PID_FILE}\"\n"
        << "echo \"[CONDOR CLEANUP] All active jobs terminated successfully.\"\n";

    out.close();

    // Make script executable (chmod 0755)
    std::filesystem::permissions(script_path,
        std::filesystem::perms::owner_all |
        std::filesystem::perms::group_read | std::filesystem::perms::group_exec |
        std::filesystem::perms::others_read | std::filesystem::perms::others_exec,
        std::filesystem::perm_options::replace, ec);

    // Initialize empty active_pids.txt
    std::ofstream pid_out(active_pids_path(), std::ios::trunc);
}

void WorkspaceManager::record_active_pid(int pid) {
    if (pid <= 0) return;
    std::lock_guard<std::mutex> lock(pid_mutex_);
    std::ofstream out(active_pids_path(), std::ios::app);
    if (out) {
        out << pid << "\n";
    }
}

void WorkspaceManager::remove_active_pid(int pid) {
    if (pid <= 0) return;
    std::lock_guard<std::mutex> lock(pid_mutex_);
    auto p = active_pids_path();
    std::ifstream in(p);
    if (!in) return;
    std::vector<int> pids;
    int curr = 0;
    while (in >> curr) {
        if (curr != pid && curr > 0) {
            pids.push_back(curr);
        }
    }
    in.close();
    std::ofstream out(p, std::ios::trunc);
    if (out) {
        for (int id : pids) {
            out << id << "\n";
        }
    }
}

std::vector<int> WorkspaceManager::get_active_pids() const {
    std::lock_guard<std::mutex> lock(pid_mutex_);
    auto p = active_pids_path();
    std::ifstream in(p);
    if (!in) return {};
    std::vector<int> pids;
    int curr = 0;
    while (in >> curr) {
        if (curr > 0) pids.push_back(curr);
    }
    return pids;
}

void WorkspaceManager::kill_run(const std::filesystem::path& target_dir) {
    std::error_code ec;
    auto script = target_dir / "kill_jobs.sh";
    if (std::filesystem::exists(script, ec)) {
        std::string cmd = "/bin/bash \"" + script.string() + "\"";
        int rc = std::system(cmd.c_str());
        (void)rc;
        return;
    }

    auto pfile = target_dir / "active_pids.txt";
    if (std::filesystem::exists(pfile, ec)) {
        std::ifstream in(pfile);
        int pid = 0;
        std::vector<int> pids;
        while (in >> pid) {
            if (pid > 0) pids.push_back(pid);
        }
        for (int p : pids) {
            kill(-p, SIGTERM);
            kill(p, SIGTERM);
        }
        usleep(500000);
        for (int p : pids) {
            kill(-p, SIGKILL);
            kill(p, SIGKILL);
        }
    }

    std::string pkill_cmd = "pkill -f \"" + (target_dir / "stages").string() + "\" 2>/dev/null || true";
    int p_rc = std::system(pkill_cmd.c_str());
    (void)p_rc;
}

} // namespace condor
