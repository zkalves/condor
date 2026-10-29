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

#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <nlohmann/json.hpp>

namespace condor::hub {

/**
 * @struct ActiveJobRecord
 * @brief Real-time status snippet for an active job running on a runner client.
 */
struct ActiveJobRecord {
    std::string job_id;
    std::string stage;
    std::string name;
    std::string pid;
    double elapsed_sec{0.0};
    double peak_rss_mb{0.0};

    nlohmann::json to_json() const {
        return {
            {"job_id", job_id},
            {"stage", stage},
            {"name", name},
            {"pid", pid},
            {"elapsed_sec", elapsed_sec},
            {"peak_rss_mb", peak_rss_mb}
        };
    }
};

/**
 * @struct StageRecord
 * @brief Stage summary progress record.
 */
struct StageRecord {
    std::string name;
    size_t passed{0};
    size_t total{0};
    size_t running{0};

    nlohmann::json to_json() const {
        return {
            {"name", name},
            {"passed", passed},
            {"total", total},
            {"running", running}
        };
    }
};

/**
 * @struct RunRecord
 * @brief In-memory and persisted representation of a Condor pipeline execution.
 */
struct RunRecord {
    std::string run_id;
    std::string pipeline;
    std::string project;
    std::string owner{"anonymous"};
    std::vector<std::string> tags;
    std::string status{"UNKNOWN"}; // RUNNING, PAUSED, PASSED, FAILED, ABORTED, DISCONNECTED
    int total_jobs{0};
    int completed_jobs{0};
    int running_jobs{0};
    int passed_jobs{0};
    int failed_jobs{0};
    int waiting_jobs{0};
    int other_jobs{0};
    int skipped_jobs{0};
    double elapsed_sec{0.0};
    double peak_rss_mb{0.0};

    int client_socket_fd{-1};
    int64_t start_time_ms{0};
    int64_t last_seen_ms{0};

    std::vector<ActiveJobRecord> active_jobs;
    std::vector<StageRecord> stages;
    std::vector<std::string> event_log;

    bool has_report{false};
    std::string report_url;

    nlohmann::json to_json() const {
        nlohmann::json act_arr = nlohmann::json::array();
        for (const auto& a : active_jobs) {
            act_arr.push_back(a.to_json());
        }
        nlohmann::json stg_arr = nlohmann::json::array();
        for (const auto& s : stages) {
            stg_arr.push_back(s.to_json());
        }

        double percent = (total_jobs > 0)
            ? (static_cast<double>(passed_jobs + failed_jobs + skipped_jobs + other_jobs) / total_jobs * 100.0)
            : 0.0;

        int waiting = waiting_jobs;
        if (waiting == 0 && (status == "RUNNING" || status == "PAUSED")) {
            int accounted = passed_jobs + failed_jobs + running_jobs + skipped_jobs + other_jobs;
            if (total_jobs > accounted) {
                waiting = total_jobs - accounted;
            }
        }

        std::string start_time_str;
        if (start_time_ms > 0) {
            std::time_t t = static_cast<std::time_t>(start_time_ms / 1000);
            std::tm tm_buf{};
            localtime_r(&t, &tm_buf);
            char buf[64];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
            start_time_str = buf;
        }

        return {
            {"run_id", run_id},
            {"pipeline", pipeline},
            {"name", pipeline},
            {"project", project},
            {"owner", owner.empty() ? "anonymous" : owner},
            {"tags", tags},
            {"status", status},
            {"has_report", has_report},
            {"report_url", report_url},
            {"total_runs", total_jobs},
            {"total_jobs", total_jobs},
            {"passed", passed_jobs},
            {"failed", failed_jobs},
            {"running", running_jobs},
            {"waiting", waiting},
            {"other", other_jobs},
            {"skipped", skipped_jobs},
            {"progress", {
                {"total_jobs", total_jobs},
                {"completed_jobs", passed_jobs + failed_jobs + skipped_jobs + other_jobs},
                {"running_jobs", running_jobs},
                {"waiting_jobs", waiting},
                {"passed_jobs", passed_jobs},
                {"failed_jobs", failed_jobs},
                {"other_jobs", other_jobs},
                {"skipped_jobs", skipped_jobs},
                {"percent", percent},
                {"elapsed_sec", elapsed_sec}
            }},
            {"peak_rss_mb", peak_rss_mb},
            {"start_time", start_time_str},
            {"start_time_ms", start_time_ms},
            {"last_seen_ms", last_seen_ms},
            {"is_connected", client_socket_fd >= 0},
            {"active_jobs", act_arr},
            {"stages", stg_arr},
            {"recent_events", event_log}
        };
    }
};

} // namespace condor::hub
