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

#include "hub/server.hpp"
#include "hub/crypto.hpp"
#include "hub/dashboard_html.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <vector>
#include <algorithm>

#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/eventfd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

namespace condor::hub {

static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static std::string get_header_value(const std::string& req, std::string_view header_name) {
    std::string target = "\n" + std::string(header_name) + ":";
    auto it = std::search(req.begin(), req.end(), target.begin(), target.end(),
                          [](char a, char b) { return std::tolower(a) == std::tolower(b); });
    if (it == req.end()) return "";

    size_t start = (it - req.begin()) + target.size();
    while (start < req.size() && (req[start] == ' ' || req[start] == '\t')) {
        start++;
    }
    size_t end = req.find("\r", start);
    if (end == std::string::npos) end = req.find("\n", start);
    if (end == std::string::npos) return "";

    return req.substr(start, end - start);
}

HubServer::HubServer(const HubConfig& config) : config_(config) {}

HubServer::~HubServer() {
    stop();
}

bool HubServer::start() {
    if (running_.load()) return true;

    // Ignore broken pipe
    signal(SIGPIPE, SIG_IGN);

    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        std::cerr << "[CONDOR-HUB] Failed to create socket: " << std::strerror(errno) << std::endl;
        return false;
    }

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config_.port);
    if (config_.host == "0.0.0.0" || config_.host.empty()) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_pton(AF_INET, config_.host.c_str(), &addr.sin_addr);
    }

    if (bind(listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[CONDOR-HUB] Bind error on " << config_.host << ":" << config_.port
                  << ": " << std::strerror(errno) << std::endl;
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    if (listen(listen_fd_, 1024) < 0) {
        std::cerr << "[CONDOR-HUB] Listen error: " << std::strerror(errno) << std::endl;
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    set_nonblocking(listen_fd_);

    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ < 0) {
        std::cerr << "[CONDOR-HUB] epoll_create1 failed: " << std::strerror(errno) << std::endl;
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = listen_fd_;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &ev) < 0) {
        close(listen_fd_);
        close(epoll_fd_);
        return false;
    }

    wakeup_fd_ = eventfd(0, EFD_NONBLOCK);
    if (wakeup_fd_ >= 0) {
        struct epoll_event ev_wake;
        ev_wake.events = EPOLLIN;
        ev_wake.data.fd = wakeup_fd_;
        epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, wakeup_fd_, &ev_wake);
    }

    running_.store(true);
    worker_thread_ = std::thread(&HubServer::event_loop, this);

    std::cout << "==================================================================\n"
              << "       CONDOR HUB - Fleet Orchestration & Remote Control Plane    \n"
              << "==================================================================\n"
              << " Server listening on: http://" << config_.host << ":" << config_.port << "\n"
              << " Web Dashboard URL:   http://localhost:" << config_.port << "/\n"
              << " WebSocket Endpoint:  ws://" << config_.host << ":" << config_.port << "/api/v1/ws\n"
              << "==================================================================\n" << std::endl;

    return true;
}

void HubServer::stop() {
    if (!running_.load()) return;

    running_.store(false);

    if (wakeup_fd_ >= 0) {
        uint64_t val = 1;
        write(wakeup_fd_, &val, sizeof(val));
    }

    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
    }

    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }

    if (wakeup_fd_ >= 0) {
        close(wakeup_fd_);
        wakeup_fd_ = -1;
    }

    if (epoll_fd_ >= 0) {
        close(epoll_fd_);
        epoll_fd_ = -1;
    }

    std::lock_guard<std::mutex> lock(state_mutex_);
    for (const auto& [fd, _] : connections_) {
        close(fd);
    }
    connections_.clear();
}

void HubServer::wait() {
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

void HubServer::event_loop() {
    constexpr int kMaxEvents = 64;
    struct epoll_event events[kMaxEvents];

    while (running_.load()) {
        int nfds = epoll_wait(epoll_fd_, events, kMaxEvents, 100);
        if (nfds < 0) {
            if (errno == EINTR) continue;
            break;
        }

        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;
            uint32_t evs = events[i].events;

            if (fd == wakeup_fd_) {
                uint64_t val = 0;
                read(wakeup_fd_, &val, sizeof(val));
                if (!running_.load()) break;
                continue;
            }

            if (fd == listen_fd_) {
                handle_new_connection(listen_fd_, epoll_fd_);
            } else if (evs & (EPOLLHUP | EPOLLERR | EPOLLRDHUP)) {
                // Connection closed or error
                epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
                {
                    std::lock_guard<std::mutex> lock(state_mutex_);
                    connections_.erase(fd);
                }
                fleet_manager_.remove_client_fd(fd);
                fleet_manager_.remove_dashboard_socket(fd);
                close(fd);
            } else if (evs & EPOLLIN) {
                handle_socket_read(fd, epoll_fd_);
            }
        }
    }
}

void HubServer::handle_new_connection(int listen_fd, int epoll_fd) {
    while (true) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            break;
        }

        set_nonblocking(client_fd);
        int flag = 1;
        setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
        ev.data.fd = client_fd;
        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev);

        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            connections_[client_fd] = ConnectionState{};
        }
    }
}

void HubServer::close_connection(int fd) {
    if (fd < 0) return;
    if (epoll_fd_ >= 0) {
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    }
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        connections_.erase(fd);
    }
    fleet_manager_.remove_client_fd(fd);
    fleet_manager_.remove_dashboard_socket(fd);
    close(fd);
}

void HubServer::handle_socket_read(int fd, int epoll_fd) {
    char buf[4096];
    std::string read_bytes;
    bool client_closed = false;

    while (true) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n > 0) {
            read_bytes.append(buf, n);
        } else if (n == 0) {
            client_closed = true;
            break;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            client_closed = true;
            break;
        }
    }

    bool is_ws = false;
    if (!read_bytes.empty()) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        auto it = connections_.find(fd);
        if (it != connections_.end()) {
            is_ws = it->second.is_websocket;
            it->second.buffer.append(read_bytes);
        }
    } else {
        std::lock_guard<std::mutex> lock(state_mutex_);
        auto it = connections_.find(fd);
        if (it != connections_.end()) {
            is_ws = it->second.is_websocket;
        }
    }

    if (is_ws) {
        std::string full_buf;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            auto it = connections_.find(fd);
            if (it != connections_.end()) {
                full_buf = std::move(it->second.buffer);
                it->second.buffer.clear();
            }
        }
        if (!full_buf.empty()) {
            size_t consumed = handle_websocket_data(fd, reinterpret_cast<const uint8_t*>(full_buf.data()), full_buf.size());
            if (consumed < full_buf.size()) {
                std::lock_guard<std::mutex> lock(state_mutex_);
                auto it = connections_.find(fd);
                if (it != connections_.end()) {
                    it->second.buffer.append(full_buf.substr(consumed));
                }
            }
        }
    } else {
        std::string raw_req;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            auto it = connections_.find(fd);
            if (it != connections_.end()) {
                raw_req = it->second.buffer;
            }
        }
        if (!raw_req.empty()) {
            size_t header_end = raw_req.find("\r\n\r\n");
            if (header_end != std::string::npos) {
                handle_http_request(fd, raw_req);
            }
        }
    }

    if (client_closed) {
        close_connection(fd);
    }
}

void HubServer::handle_http_request(int fd, const std::string& raw_request) {
    std::istringstream stream(raw_request);
    std::string method, path, version;
    stream >> method >> path >> version;

    std::string upgrade = get_header_value(raw_request, "Upgrade");
    std::transform(upgrade.begin(), upgrade.end(), upgrade.begin(), ::tolower);

    // WebSocket Upgrade Handshake
    if (upgrade == "websocket") {
        std::string ws_key = get_header_value(raw_request, "Sec-WebSocket-Key");
        if (!ws_key.empty()) {
            std::string accept_val = compute_websocket_accept(ws_key);

            std::ostringstream resp;
            resp << "HTTP/1.1 101 Switching Protocols\r\n"
                 << "Upgrade: websocket\r\n"
                 << "Connection: Upgrade\r\n"
                 << "Sec-WebSocket-Accept: " << accept_val << "\r\n\r\n";

            std::string resp_str = resp.str();
            send(fd, resp_str.c_str(), resp_str.size(), MSG_NOSIGNAL);

            bool is_dash = (path.find("/ws/dashboard") != std::string::npos);
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                connections_[fd].is_websocket = true;
                connections_[fd].is_dashboard = is_dash;
                connections_[fd].buffer.clear();
            }

            if (is_dash) {
                fleet_manager_.register_dashboard_socket(fd);
            } else {
                std::string run_id = get_header_value(raw_request, "X-Condor-Run-Id");
                std::string pipeline = get_header_value(raw_request, "X-Condor-Pipeline");
                if (!run_id.empty()) {
                    fleet_manager_.register_client(run_id, pipeline, "default", {}, fd);
                }
            }
            return;
        }
    }

    // CORS pre-flight
    if (method == "OPTIONS") {
        std::string resp =
            "HTTP/1.1 204 No Content\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type, Authorization, X-Condor-Run-Id, X-Condor-Pipeline\r\n"
            "\r\n";
        send(fd, resp.c_str(), resp.size(), MSG_NOSIGNAL);
        return;
    }

    // Serve Embedded Dashboard
    if (method == "GET" && (path == "/" || path == "/dashboard" || path == "/index.html")) {
        std::ostringstream resp;
        resp << "HTTP/1.1 200 OK\r\n"
             << "Content-Type: text/html; charset=utf-8\r\n"
             << "Content-Length: " << kDashboardHtml.size() << "\r\n"
             << "Connection: close\r\n\r\n"
             << kDashboardHtml;
        std::string s = resp.str();
        send(fd, s.c_str(), s.size(), MSG_NOSIGNAL);
        close_connection(fd);
        return;
    }

    // REST: List all runs
    if (method == "GET" && path == "/api/v1/runs") {
        auto json_runs = fleet_manager_.get_all_runs_json();
        nlohmann::json payload;
        payload["runs"] = json_runs;
        std::string body = payload.dump();

        std::ostringstream resp;
        resp << "HTTP/1.1 200 OK\r\n"
             << "Content-Type: application/json\r\n"
             << "Access-Control-Allow-Origin: *\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n\r\n"
             << body;
        std::string s = resp.str();
        send(fd, s.c_str(), s.size(), MSG_NOSIGNAL);
        close_connection(fd);
        return;
    }

    // Extract body for POST requests
    std::string post_body;
    size_t body_pos = raw_request.find("\r\n\r\n");
    if (body_pos != std::string::npos) {
        post_body = raw_request.substr(body_pos + 4);
    }

    // REST: Emergency Stop All
    if (method == "POST" && path == "/api/v1/emergency-stop") {
        size_t count = fleet_manager_.emergency_stop_all();
        nlohmann::json res = {{"status", "aborted"}, {"count", count}};
        std::string body = res.dump();
        std::ostringstream resp;
        resp << "HTTP/1.1 200 OK\r\n"
             << "Content-Type: application/json\r\n"
             << "Access-Control-Allow-Origin: *\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n\r\n"
             << body;
        std::string s = resp.str();
        send(fd, s.c_str(), s.size(), MSG_NOSIGNAL);
        close_connection(fd);
        return;
    }

    // REST: Control single run /api/v1/runs/{id}/control
    if (method == "POST" && path.rfind("/api/v1/runs/", 0) == 0 && path.find("/control") != std::string::npos) {
        size_t prefix_len = std::string("/api/v1/runs/").size();
        size_t ctrl_pos = path.find("/control");
        std::string target_run_id = path.substr(prefix_len, ctrl_pos - prefix_len);

        try {
            auto j = nlohmann::json::parse(post_body);
            std::string action = j.value("action", "");
            bool dispatched = fleet_manager_.send_command(target_run_id, action, j);

            nlohmann::json res = {{"status", dispatched ? "dispatched" : "failed"},
                                  {"run_id", target_run_id},
                                  {"action", action}};
            std::string body = res.dump();
            std::ostringstream resp;
            resp << "HTTP/1.1 " << (dispatched ? "200 OK" : "404 Not Found") << "\r\n"
                 << "Content-Type: application/json\r\n"
                 << "Access-Control-Allow-Origin: *\r\n"
                 << "Content-Length: " << body.size() << "\r\n"
                 << "Connection: close\r\n\r\n"
                 << body;
            std::string s = resp.str();
            send(fd, s.c_str(), s.size(), MSG_NOSIGNAL);
        } catch (...) {
            std::string resp = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send(fd, resp.c_str(), resp.size(), MSG_NOSIGNAL);
        }
        close_connection(fd);
        return;
    }

    // REST: Upload HTML report /api/v1/runs/{id}/report
    if (method == "POST" && path.rfind("/api/v1/runs/", 0) == 0 && path.find("/report") != std::string::npos) {
        size_t prefix_len = std::string("/api/v1/runs/").size();
        size_t rep_pos = path.find("/report");
        std::string target_run_id = path.substr(prefix_len, rep_pos - prefix_len);
        bool saved = fleet_manager_.save_report(target_run_id, post_body, config_.artifacts_dir);

        nlohmann::json res = {{"status", saved ? "saved" : "error"},
                              {"run_id", target_run_id},
                              {"report_url", "/reports/" + target_run_id}};
        std::string body = res.dump();
        std::ostringstream resp;
        resp << "HTTP/1.1 " << (saved ? "200 OK" : "400 Bad Request") << "\r\n"
             << "Content-Type: application/json\r\n"
             << "Access-Control-Allow-Origin: *\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n\r\n"
             << body;
        std::string s = resp.str();
        send(fd, s.c_str(), s.size(), MSG_NOSIGNAL);
        close_connection(fd);
        return;
    }

    // Serve HTML execution reports /reports/{id}
    if (method == "GET" && path.rfind("/reports/", 0) == 0) {
        std::string target_run_id = path.substr(std::string("/reports/").size());
        if (target_run_id.ends_with("/report.html")) {
            target_run_id = target_run_id.substr(0, target_run_id.size() - std::string("/report.html").size());
        } else if (target_run_id.ends_with("/")) {
            target_run_id.pop_back();
        }

        std::string report_file = fleet_manager_.get_report_path(target_run_id, config_.artifacts_dir);
        if (!report_file.empty()) {
            std::ifstream ifs(report_file, std::ios::binary);
            if (ifs) {
                std::string content((std::istreambuf_iterator<char>(ifs)),
                                     std::istreambuf_iterator<char>());
                std::ostringstream resp;
                resp << "HTTP/1.1 200 OK\r\n"
                     << "Content-Type: text/html; charset=utf-8\r\n"
                     << "Access-Control-Allow-Origin: *\r\n"
                     << "Content-Length: " << content.size() << "\r\n"
                     << "Connection: close\r\n\r\n"
                     << content;
                std::string s = resp.str();
                send(fd, s.c_str(), s.size(), MSG_NOSIGNAL);
                close_connection(fd);
                return;
            }
        }

        std::string not_found_html = "<!DOCTYPE html><html><head><title>Report Not Found</title>"
            "<style>body{background:#0f172a;color:#f8fafc;font-family:sans-serif;padding:40px;text-align:center;}"
            "h1{color:#ef4444;}a{color:#38bdf8;text-decoration:none;}</style></head><body>"
            "<h1>404 - HTML Report Not Found</h1>"
            "<p>No HTML report uploaded yet for run: <code>" + target_run_id + "</code></p>"
            "<p style=\"margin-top:20px;\"><a href=\"/\">&larr; Return to Dashboard</a></p></body></html>";

        std::ostringstream resp;
        resp << "HTTP/1.1 404 Not Found\r\n"
             << "Content-Type: text/html; charset=utf-8\r\n"
             << "Content-Length: " << not_found_html.size() << "\r\n"
             << "Connection: close\r\n\r\n"
             << not_found_html;
        std::string s = resp.str();
        send(fd, s.c_str(), s.size(), MSG_NOSIGNAL);
        close_connection(fd);
        return;
    }

    // Default 404
    std::string not_found = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    send(fd, not_found.c_str(), not_found.size(), MSG_NOSIGNAL);
    close_connection(fd);
}

size_t HubServer::handle_websocket_data(int fd, const uint8_t* data, size_t len) {
    size_t offset = 0;
    while (offset + 2 <= len) {
        uint8_t b0 = data[offset];
        uint8_t b1 = data[offset + 1];
        uint8_t opcode = b0 & 0x0F;
        bool masked = (b1 & 0x80) != 0;
        uint64_t payload_len = b1 & 0x7F;
        size_t header_len = 2;

        if (payload_len == 126) {
            if (offset + 4 > len) break;
            payload_len = (static_cast<uint64_t>(data[offset + 2]) << 8) | data[offset + 3];
            header_len = 4;
        } else if (payload_len == 127) {
            if (offset + 10 > len) break;
            payload_len = 0;
            for (int i = 0; i < 8; ++i) {
                payload_len = (payload_len << 8) | data[offset + 2 + i];
            }
            header_len = 10;
        }

        uint8_t mask[4] = {0};
        if (masked) {
            if (offset + header_len + 4 > len) break;
            std::memcpy(mask, data + offset + header_len, 4);
            header_len += 4;
        }

        if (offset + header_len + payload_len > len) break;

        const uint8_t* payload_ptr = data + offset + header_len;
        std::vector<uint8_t> unmasked_payload(payload_len);
        for (size_t i = 0; i < payload_len; ++i) {
            unmasked_payload[i] = masked ? (payload_ptr[i] ^ mask[i % 4]) : payload_ptr[i];
        }

        offset += header_len + payload_len;

        if (opcode == 0x1) { // Text frame
            std::string json_text(unmasked_payload.begin(), unmasked_payload.end());
            try {
                auto j = nlohmann::json::parse(json_text);
                std::string type = j.value("type", "");

                if (type == "client_hello") {
                    std::string run_id = j.value("run_id", "");
                    std::string pipe = j.value("pipeline", "");
                    std::string proj = j.value("project", "default");
                    std::vector<std::string> tags = j.value("tags", std::vector<std::string>{});
                    fleet_manager_.register_client(run_id, pipe, proj, tags, fd);
                } else if (type == "telemetry") {
                    std::string run_id = j.value("run_id", "");
                    if (j.contains("data") && j["data"].is_object()) {
                        fleet_manager_.update_telemetry(run_id, j["data"], fd);
                    }
                } else if (type == "event") {
                    std::string run_id = j.value("run_id", "");
                    std::string event = j.value("event", "");
                    fleet_manager_.record_event(run_id, event, j.value("payload", nlohmann::json::object()));
                } else if (type == "report_upload") {
                    std::string run_id = j.value("run_id", "");
                    std::string html = j.value("html", "");
                    if (!run_id.empty() && !html.empty()) {
                        fleet_manager_.save_report(run_id, html, config_.artifacts_dir);
                    }
                }
            } catch (...) {
                // Ignore parse errors on malformed frames
            }
        } else if (opcode == 0x8) { // Close frame
            close_connection(fd);
            return len;
        } else if (opcode == 0x9) { // Ping -> send Pong
            uint8_t pong[2] = {0x8A, 0x00};
            send(fd, pong, 2, MSG_NOSIGNAL);
        }
    }
    return offset;
}

} // namespace condor::hub
