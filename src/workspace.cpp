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
              << "elapsed_sec: " << meta.elapsed_sec << "\n"
              << "pid_or_grid_id: \"" << meta.pid_or_grid_id << "\"\n"
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

} // namespace condor
