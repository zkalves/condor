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
#include <thread>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <vector>

#include "fleet_manager.hpp"

namespace condor::hub {

/**
 * @struct ServerConfig
 * @brief Configuration parameters for Condor Hub.
 */
struct HubConfig {
    std::string host{"0.0.0.0"};
    int port{8080};
    std::string token;
    int worker_threads{2};
    std::string artifacts_dir{"./hub_artifacts"};
};

/**
 * @class HubServer
 * @brief High-concurrency Linux epoll-driven HTTP/1.1 and RFC 6455 WebSocket server.
 */
class HubServer {
public:
    explicit HubServer(const HubConfig& config);
    ~HubServer();

    /**
     * @brief Starts the listening socket and background epoll worker thread.
     * @return True if bound and listening successfully.
     */
    bool start();

    /**
     * @brief Shuts down server and disconnects all clients.
     */
    void stop();

    /**
     * @brief Blocks current thread until server is stopped or terminated.
     */
    void wait();

    /**
     * @brief Gets reference to the internal FleetManager instance.
     */
    FleetManager& fleet_manager() { return fleet_manager_; }

private:
    void event_loop();
    void handle_new_connection(int listen_fd, int epoll_fd);
    void handle_socket_read(int fd, int epoll_fd);
    void handle_http_request(int fd, const std::string& raw_request);
    size_t handle_websocket_data(int fd, const uint8_t* data, size_t len);
    void close_connection(int fd);

    HubConfig config_;
    FleetManager fleet_manager_;

    std::atomic<bool> running_{false};
    int listen_fd_{-1};
    int epoll_fd_{-1};
    int wakeup_fd_{-1};
    std::thread worker_thread_;

    // Per-connection state
    struct ConnectionState {
        bool is_websocket{false};
        bool is_dashboard{false};
        std::string buffer;
    };
    std::mutex state_mutex_;
    std::unordered_map<int, ConnectionState> connections_;
};

} // namespace condor::hub
