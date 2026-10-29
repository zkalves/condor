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

#include "fleet_manager.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>

namespace condor::hub {

static int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

static int64_t parse_run_id_epoch_ms(const std::string& run_id) {
    if (run_id.size() >= 15 && run_id[8] == '_') {
        std::tm tm{};
        std::string s = run_id.substr(0, 15);
        if (strptime(s.c_str(), "%Y%m%d_%H%M%S", &tm)) {
            time_t t = mktime(&tm);
            if (t != static_cast<time_t>(-1)) {
                return static_cast<int64_t>(t) * 1000;
            }
        }
    }
    return 0;
}

static bool send_ws_text_frame(int fd, const std::string& text) {
    if (fd < 0) return false;

    std::vector<uint8_t> frame;
    frame.push_back(0x81); // FIN + text opcode

    size_t len = text.size();
    if (len <= 125) {
        frame.push_back(static_cast<uint8_t>(len)); // Server frames are NOT masked
    } else if (len <= 65535) {
        frame.push_back(126);
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<uint8_t>((len >> (i * 8)) & 0xFF));
        }
    }
    frame.insert(frame.end(), text.begin(), text.end());

    ssize_t sent = send(fd, frame.data(), frame.size(), MSG_NOSIGNAL);
    return sent == static_cast<ssize_t>(frame.size());
}

FleetManager::FleetManager() = default;

void FleetManager::register_client(const std::string& run_id,
                                   const std::string& pipeline,
                                   const std::string& project,
                                   const std::string& owner,
                                   const std::vector<std::string>& tags,
                                   int client_fd) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto& run = runs_[run_id];
    run.run_id = run_id;
    run.pipeline = pipeline;
    run.project = project;
    run.owner = owner.empty() ? "anonymous" : owner;
    run.tags = tags;
    run.client_socket_fd = client_fd;
    if (run.status == "UNKNOWN") {
        run.status = "CONNECTED";
    }
    run.last_seen_ms = now_ms();
    if (run.start_time_ms == 0) {
        int64_t parsed = parse_run_id_epoch_ms(run_id);
        run.start_time_ms = (parsed > 0) ? parsed : run.last_seen_ms;
    }

    if (client_fd >= 0) {
        fd_to_run_id_[client_fd] = run_id;
    }

    nlohmann::json notif;
    notif["type"] = "client_registered";
    notif["run"] = run.to_json();
    broadcast_to_dashboards(notif);
}

void FleetManager::update_telemetry(const std::string& run_id, const nlohmann::json& telemetry, int client_fd) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto& run = runs_[run_id];
    run.run_id = run_id;
    run.last_seen_ms = now_ms();
    if (run.start_time_ms == 0) {
        int64_t parsed = parse_run_id_epoch_ms(run_id);
        run.start_time_ms = (parsed > 0) ? parsed : run.last_seen_ms;
    }
    if (client_fd >= 0) {
        run.client_socket_fd = client_fd;
        fd_to_run_id_[client_fd] = run_id;
    }

    if (telemetry.contains("status") && telemetry["status"].is_string()) {
        run.status = telemetry["status"].get<std::string>();
    }

    if (telemetry.contains("progress") && telemetry["progress"].is_object()) {
        const auto& p = telemetry["progress"];
        run.total_jobs = p.value("total_jobs", run.total_jobs);
        run.running_jobs = p.value("running_jobs", run.running_jobs);
        run.passed_jobs = p.value("passed_jobs", run.passed_jobs);
        run.failed_jobs = p.value("failed_jobs", run.failed_jobs);
        run.skipped_jobs = p.value("skipped_jobs", run.skipped_jobs);
        run.waiting_jobs = p.value("queued_jobs", p.value("waiting_jobs", 0));
        run.other_jobs = p.value("other_jobs", run.other_jobs);
        run.completed_jobs = p.value("completed_jobs", run.passed_jobs + run.failed_jobs + run.skipped_jobs + run.other_jobs);
        run.elapsed_sec = p.value("elapsed_sec", run.elapsed_sec);
    }

    if (telemetry.contains("active_jobs") && telemetry["active_jobs"].is_array()) {
        run.active_jobs.clear();
        for (const auto& a : telemetry["active_jobs"]) {
            ActiveJobRecord rec;
            rec.job_id = a.value("job_id", "");
            rec.stage = a.value("stage", "");
            rec.name = a.value("name", "");
            rec.pid = a.value("pid", "");
            rec.elapsed_sec = a.value("elapsed_sec", 0.0);
            rec.peak_rss_mb = a.value("peak_rss_mb", 0.0);
            run.active_jobs.push_back(std::move(rec));
        }
    }

    nlohmann::json update;
    update["type"] = "telemetry";
    update["run"] = run.to_json();
    broadcast_to_dashboards(update);
}

void FleetManager::record_event(const std::string& run_id, const std::string& event_type, const nlohmann::json& payload) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = runs_.find(run_id);
    if (it != runs_.end()) {
        it->second.last_seen_ms = now_ms();
        if (it->second.start_time_ms == 0) {
            int64_t parsed = parse_run_id_epoch_ms(run_id);
            it->second.start_time_ms = (parsed > 0) ? parsed : it->second.last_seen_ms;
        }
        if (event_type == "pipeline_started") {
            it->second.status = "RUNNING";
            it->second.total_jobs = payload.value("total_jobs", it->second.total_jobs);
        } else if (event_type == "job_started") {
            it->second.running_jobs++;
            ActiveJobRecord rec;
            rec.job_id = payload.value("job_id", "");
            rec.stage = payload.value("stage", "");
            rec.name = payload.value("name", "");
            rec.pid = payload.value("pid", "");
            it->second.active_jobs.push_back(std::move(rec));
        } else if (event_type == "job_finished") {
            if (it->second.running_jobs > 0) it->second.running_jobs--;
            std::string st = payload.value("status", "");
            if (st == "PASSED") {
                it->second.passed_jobs++;
            } else if (st == "FAILED (ALLOWED)") {
                it->second.other_jobs++;
            } else if (st == "SKIPPED") {
                it->second.skipped_jobs++;
            } else {
                it->second.failed_jobs++;
            }
            it->second.completed_jobs = it->second.passed_jobs + it->second.failed_jobs + it->second.skipped_jobs + it->second.other_jobs;
            std::string jid = payload.value("job_id", "");
            std::string stg = payload.value("stage", "");
            it->second.active_jobs.erase(
                std::remove_if(it->second.active_jobs.begin(), it->second.active_jobs.end(),
                               [&](const ActiveJobRecord& r) { return r.job_id == jid && r.stage == stg; }),
                it->second.active_jobs.end());
        } else if (event_type == "pipeline_aborted") {
            it->second.status = "ABORTED";
            it->second.active_jobs.clear();
            it->second.running_jobs = 0;
            it->second.waiting_jobs = 0;
        } else if (event_type == "pipeline_finished") {
            bool success = payload.value("success", false);
            it->second.status = success ? "PASSED" : "FAILED";
            it->second.passed_jobs = payload.value("passed_jobs", it->second.passed_jobs);
            it->second.failed_jobs = payload.value("failed_jobs", it->second.failed_jobs);
            it->second.skipped_jobs = payload.value("skipped_jobs", it->second.skipped_jobs);
            it->second.total_jobs = payload.value("total_jobs", it->second.total_jobs);
            it->second.completed_jobs = it->second.passed_jobs + it->second.failed_jobs + it->second.skipped_jobs + it->second.other_jobs;
            it->second.elapsed_sec = payload.value("elapsed_sec", it->second.elapsed_sec);
            it->second.active_jobs.clear();
            it->second.running_jobs = 0;
            it->second.waiting_jobs = 0;
        } else if (event_type == "scheduler_paused") {
            it->second.status = "PAUSED";
        } else if (event_type == "scheduler_resumed") {
            it->second.status = "RUNNING";
        }

        std::string log_entry = "[" + event_type + "] " + payload.dump();
        it->second.event_log.push_back(log_entry);
        if (it->second.event_log.size() > 50) {
            it->second.event_log.erase(it->second.event_log.begin());
        }

        nlohmann::json notif;
        notif["type"] = "event";
        notif["event"] = event_type;
        notif["run_id"] = run_id;
        notif["payload"] = payload;
        notif["run"] = it->second.to_json();
        broadcast_to_dashboards(notif);
    }
}

void FleetManager::remove_client_fd(int client_fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = fd_to_run_id_.find(client_fd);
    if (it != fd_to_run_id_.end()) {
        std::string run_id = it->second;
        fd_to_run_id_.erase(it);

        auto rit = runs_.find(run_id);
        if (rit != runs_.end() && rit->second.client_socket_fd == client_fd) {
            rit->second.client_socket_fd = -1;
            if (rit->second.status == "RUNNING" || rit->second.status == "PAUSED" || rit->second.status == "CONNECTED") {
                rit->second.status = "DISCONNECTED";
            }
            nlohmann::json notif;
            notif["type"] = "client_disconnected";
            notif["run_id"] = run_id;
            notif["run"] = rit->second.to_json();
            broadcast_to_dashboards(notif);
        }
    }
}

void FleetManager::register_dashboard_socket(int fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    dashboard_fds_.insert(fd);

    // Immediately push full fleet state to newly connected dashboard
    nlohmann::json snapshot;
    snapshot["type"] = "fleet_update";
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& [_, run] : runs_) {
        arr.push_back(run.to_json());
    }
    snapshot["runs"] = arr;
    send_ws_text_frame(fd, snapshot.dump());
}

void FleetManager::remove_dashboard_socket(int fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    dashboard_fds_.erase(fd);
}

bool FleetManager::send_command(const std::string& run_id, const std::string& action, const nlohmann::json& params) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = runs_.find(run_id);
    if (it == runs_.end() || it->second.client_socket_fd < 0) {
        return false;
    }

    nlohmann::json cmd;
    cmd["type"] = "command";
    cmd["action"] = action;
    if (params.is_object()) {
        for (auto el = params.begin(); el != params.end(); ++el) {
            cmd[el.key()] = el.value();
        }
    }

    bool sent = send_ws_text_frame(it->second.client_socket_fd, cmd.dump());
    return sent;
}

size_t FleetManager::emergency_stop_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t count = 0;
    nlohmann::json cmd;
    cmd["type"] = "command";
    cmd["action"] = "abort";

    std::string serialized = cmd.dump();
    for (auto& [_, run] : runs_) {
        if (run.client_socket_fd >= 0) {
            send_ws_text_frame(run.client_socket_fd, serialized);
            count++;
        }
    }
    return count;
}

nlohmann::json FleetManager::get_all_runs_json() const {
    std::lock_guard<std::mutex> lock(mutex_);
    nlohmann::json out = nlohmann::json::array();
    for (const auto& [_, r] : runs_) {
        out.push_back(r.to_json());
    }
    return out;
}

nlohmann::json FleetManager::get_run_json(const std::string& run_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = runs_.find(run_id);
    if (it != runs_.end()) {
        return it->second.to_json();
    }
    return nullptr;
}

bool FleetManager::save_report(const std::string& run_id, const std::string& html_content, const std::string& artifacts_dir) {
    if (run_id.empty() || html_content.empty()) return false;

    std::filesystem::path dir = std::filesystem::path(artifacts_dir) / run_id;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        std::cerr << "[CONDOR-HUB] Error creating directory " << dir << ": " << ec.message() << std::endl;
        return false;
    }

    std::filesystem::path file_path = dir / "report.html";
    std::ofstream ofs(file_path, std::ios::binary);
    if (!ofs) {
        std::cerr << "[CONDOR-HUB] Error opening file for write: " << file_path << std::endl;
        return false;
    }
    ofs.write(html_content.data(), html_content.size());
    ofs.close();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = runs_.find(run_id);
        if (it != runs_.end()) {
            it->second.has_report = true;
            it->second.report_url = "/reports/" + run_id;
            nlohmann::json notif;
            notif["type"] = "report_received";
            notif["run_id"] = run_id;
            notif["run"] = it->second.to_json();
            broadcast_to_dashboards(notif);
        }
    }
    return true;
}

std::string FleetManager::get_report_path(const std::string& run_id, const std::string& artifacts_dir) const {
    std::filesystem::path file_path = std::filesystem::path(artifacts_dir) / run_id / "report.html";
    std::error_code ec;
    if (std::filesystem::exists(file_path, ec)) {
        return file_path.string();
    }
    return "";
}

void FleetManager::broadcast_to_dashboards(const nlohmann::json& msg) {
    std::string text = msg.dump();
    std::vector<int> dead_fds;
    for (int fd : dashboard_fds_) {
        if (!send_ws_text_frame(fd, text)) {
            dead_fds.push_back(fd);
        }
    }
    for (int dfd : dead_fds) {
        dashboard_fds_.erase(dfd);
    }
}

} // namespace condor::hub
