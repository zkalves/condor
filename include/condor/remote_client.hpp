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
#include <string_view>
#include <vector>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <chrono>
#include <nlohmann/json.hpp>

#include "condor/config.hpp"

namespace condor {

/**
 * @class RemoteServerClient
 * @brief Thread-safe client providing real-time telemetry streaming and remote control capabilities.
 *
 * RemoteServerClient establishes a persistent background connection (WebSocket or HTTP REST)
 * to a central fleet server when explicitly enabled. It streams real-time progress, peak RSS,
 * and stage/job events, while concurrently polling or receiving remote control commands
 * (e.g., pause, resume, abort_all, kill_job).
 *
 * When not enabled, the client operates in a completely inert state with zero network activity
 * and zero worker threads, ensuring 100% offline execution.
 */
class RemoteServerClient {
public:
    using CommandCallback = std::function<void(const std::string& action, const nlohmann::json& payload)>;

    /**
     * @brief Constructs a remote telemetry client instance.
     * @param config Server configuration parameters.
     * @param run_id Unique identifier of the active pipeline execution.
     * @param pipeline_name Name of the pipeline being orchestrated.
     */
    RemoteServerClient(const ServerConfig& config, std::string_view run_id, std::string_view pipeline_name);

    /**
     * @brief Destructor ensuring background threads and network sockets are cleanly released.
     */
    ~RemoteServerClient();

    /**
     * @brief Checks if remote reporting is enabled and configured with a valid URL.
     * @return True if remote reporting is active, false if operating offline.
     */
    bool is_enabled() const { return config_.enabled && !config_.url.empty(); }

    /**
     * @brief Checks if client is currently connected to the server.
     * @return True if socket connection is established and healthy.
     */
    bool is_connected() const { return connected_.load(); }

    /**
     * @brief Starts the background worker thread for telemetry streaming and control listening.
     */
    void start();

    /**
     * @brief Stops the background worker thread and closes active network connections.
     */
    void stop();

    /**
     * @brief Queues a lifecycle event notification to be pushed to the server.
     * @param event_type Event identifier (e.g. "pipeline_started", "job_finished").
     * @param payload Additional event metadata and attributes.
     */
    void send_event(const std::string& event_type, const nlohmann::json& payload);

    /**
     * @brief Queues a periodic real-time telemetry status frame to be streamed to the server.
     * @param telemetry JSON object containing current progress stats, active jobs, and peak memory.
     */
    void send_telemetry(const nlohmann::json& telemetry);

    /**
     * @brief Registers callback function invoked when control commands are received from the server.
     * @param cb Callback function accepting action name and parameter payload.
     */
    void set_command_handler(CommandCallback cb) {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        command_handler_ = std::move(cb);
    }

    /**
     * @brief Parses a URL into protocol, host, port, and path components.
     * @param url_str Full URL string.
     * @param protocol Output protocol ("ws", "wss", "http", "https").
     * @param host Output hostname or IP.
     * @param port Output TCP port.
     * @param path Output HTTP/WebSocket endpoint path.
     * @return True if URL syntax is valid, false otherwise.
     */
    static bool parse_url(const std::string& url_str,
                          std::string& protocol,
                          std::string& host,
                          int& port,
                          std::string& path);

private:
    void worker_loop();
    bool connect_socket();
    void disconnect_socket();
    bool do_websocket_handshake();
    bool send_websocket_frame(const std::string& text);
    void poll_incoming_websocket();
    void do_http_post(const std::string& json_str);
    void process_received_command(const std::string& raw_json);

    ServerConfig config_;
    std::string run_id_;
    std::string pipeline_name_;

    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    int socket_fd_{-1};

    std::thread worker_thread_;
    mutable std::mutex queue_mutex_;
    std::queue<std::string> outbound_queue_;
    CommandCallback command_handler_;

    std::string protocol_{"ws"};
    std::string host_{"localhost"};
    int port_{8080};
    std::string path_{"/api/v1/ws"};
};

} // namespace condor
