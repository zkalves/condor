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
#include <unordered_map>
#include <mutex>
#include <memory>
#include <set>
#include <nlohmann/json.hpp>

#include "models.hpp"

namespace condor::hub {

/**
 * @class FleetManager
 * @brief Thread-safe registry coordinating active Condor runner clients and dashboard subscribers.
 */
class FleetManager {
public:
    FleetManager();
    ~FleetManager() = default;

    /**
     * @brief Registers or updates a client runner connection.
     * @param run_id Unique run folder or identifier.
     * @param pipeline Pipeline name.
     * @param project Project tag.
     * @param tags Categorization tags.
     * @param client_fd Active client socket descriptor.
     */
    void register_client(const std::string& run_id,
                         const std::string& pipeline,
                         const std::string& project,
                         const std::string& owner,
                         const std::vector<std::string>& tags,
                         int client_fd);

    /**
     * @brief Updates telemetry progress metrics for a run and broadcasts update to dashboards.
     * @param run_id Identifier of target run.
     * @param telemetry Telemetry data JSON object.
     * @param client_fd Socket descriptor of sender.
     */
    void update_telemetry(const std::string& run_id, const nlohmann::json& telemetry, int client_fd);

    /**
     * @brief Records a lifecycle event (job_started, job_finished, etc.) for a run.
     * @param run_id Identifier of target run.
     * @param event_type Event identifier.
     * @param payload Event metadata payload.
     */
    void record_event(const std::string& run_id, const std::string& event_type, const nlohmann::json& payload);

    /**
     * @brief Disconnects a client socket and marks run disconnected.
     * @param client_fd Socket descriptor to unbind.
     */
    void remove_client_fd(int client_fd);

    /**
     * @brief Registers an active dashboard browser WebSocket subscriber.
     * @param fd Dashboard socket descriptor.
     */
    void register_dashboard_socket(int fd);

    /**
     * @brief Removes a dashboard browser WebSocket subscriber.
     * @param fd Dashboard socket descriptor.
     */
    void remove_dashboard_socket(int fd);

    /**
     * @brief Sends a remote control command to a specific runner.
     * @param run_id Target pipeline execution ID.
     * @param action Command action (pause, resume, abort, kill_job, scale_slots).
     * @param params Additional parameters.
     * @return True if dispatched to connected socket, false if run not connected.
     */
    bool send_command(const std::string& run_id, const std::string& action, const nlohmann::json& params = {});

    /**
     * @brief Sends emergency stop command to all currently connected runners.
     * @return Number of runners dispatched to.
     */
    size_t emergency_stop_all();

    /**
     * @brief Gets all registered runs as a JSON array.
     */
    nlohmann::json get_all_runs_json() const;

    /**
     * @brief Gets details for a single run by ID.
     */
    nlohmann::json get_run_json(const std::string& run_id) const;

    /**
     * @brief Saves an uploaded HTML execution report for a run.
     * @param run_id Identifier of target run.
     * @param html_content Raw HTML report string.
     * @param artifacts_dir Base directory configured on server for storing artifacts.
     * @return True if saved successfully, false otherwise.
     */
    bool save_report(const std::string& run_id, const std::string& html_content, const std::string& artifacts_dir);

    /**
     * @brief Resolves the filesystem path to a stored report for a run if present.
     * @param run_id Identifier of target run.
     * @param artifacts_dir Base directory configured on server for storing artifacts.
     * @return Full path to report.html, or empty string if not found.
     */
    std::string get_report_path(const std::string& run_id, const std::string& artifacts_dir) const;

private:
    void broadcast_to_dashboards(const nlohmann::json& msg);

    mutable std::mutex mutex_;
    std::unordered_map<std::string, RunRecord> runs_;
    std::unordered_map<int, std::string> fd_to_run_id_;
    std::set<int> dashboard_fds_;
};

} // namespace condor::hub
