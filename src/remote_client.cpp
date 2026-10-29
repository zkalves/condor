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

#include "condor/remote_client.hpp"
#include "condor/logging.hpp"

#include <iostream>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <cerrno>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>
#include <signal.h>

namespace condor {

RemoteServerClient::RemoteServerClient(const ServerConfig& config,
                                       std::string_view run_id,
                                       std::string_view pipeline_name)
    : config_(config), run_id_(run_id), pipeline_name_(pipeline_name) {
    if (config_.enabled && !config_.url.empty()) {
        parse_url(config_.url, protocol_, host_, port_, path_);
    }
}

RemoteServerClient::~RemoteServerClient() {
    stop();
}

bool RemoteServerClient::parse_url(const std::string& url_str,
                                   std::string& protocol,
                                   std::string& host,
                                   int& port,
                                   std::string& path) {
    if (url_str.empty()) return false;

    std::string s = url_str;
    size_t proto_end = s.find("://");
    if (proto_end != std::string::npos) {
        protocol = s.substr(0, proto_end);
        s = s.substr(proto_end + 3);
    } else {
        protocol = "ws";
    }

    // Default ports
    if (protocol == "http" || protocol == "ws") {
        port = 80;
    } else if (protocol == "https" || protocol == "wss") {
        port = 443;
    } else {
        port = 80;
    }

    size_t path_start = s.find('/');
    std::string host_port_part;
    if (path_start != std::string::npos) {
        host_port_part = s.substr(0, path_start);
        path = s.substr(path_start);
    } else {
        host_port_part = s;
        path = "/";
    }

    size_t colon = host_port_part.find(':');
    if (colon != std::string::npos) {
        host = host_port_part.substr(0, colon);
        try {
            port = std::stoi(host_port_part.substr(colon + 1));
        } catch (...) {
            return false;
        }
    } else {
        host = host_port_part;
    }

    return !host.empty();
}

void RemoteServerClient::start() {
    if (!is_enabled()) return;
    if (running_.load()) return;

    running_.store(true);
    worker_thread_ = std::thread(&RemoteServerClient::worker_loop, this);
}

void RemoteServerClient::stop() {
    if (!running_.load()) return;

    running_.store(false);
    disconnect_socket();

    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

void RemoteServerClient::send_event(const std::string& event_type, const nlohmann::json& payload) {
    if (!is_enabled()) return;

    nlohmann::json msg;
    msg["type"] = "event";
    msg["event"] = event_type;
    msg["run_id"] = run_id_;
    msg["pipeline"] = pipeline_name_;
    msg["project"] = config_.project;
    msg["tags"] = config_.tags;
    msg["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    msg["payload"] = payload;

    std::lock_guard<std::mutex> lock(queue_mutex_);
    outbound_queue_.push(msg.dump());
}

void RemoteServerClient::send_telemetry(const nlohmann::json& telemetry) {
    if (!is_enabled()) return;

    nlohmann::json msg;
    msg["type"] = "telemetry";
    msg["run_id"] = run_id_;
    msg["pipeline"] = pipeline_name_;
    msg["project"] = config_.project;
    msg["tags"] = config_.tags;
    msg["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    msg["data"] = telemetry;

    std::lock_guard<std::mutex> lock(queue_mutex_);
    outbound_queue_.push(msg.dump());
}

bool RemoteServerClient::connect_socket() {
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }

    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    std::string port_str = std::to_string(port_);
    struct addrinfo* res = nullptr;
    int rc = getaddrinfo(host_.c_str(), port_str.c_str(), &hints, &res);
    if (rc != 0 || !res) {
        return false;
    }

    int fd = -1;
    for (struct addrinfo* p = res; p != nullptr; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;

        // Set non-blocking for connect timeout
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);

        int connect_res = connect(fd, p->ai_addr, p->ai_addrlen);
        if (connect_res < 0 && errno == EINPROGRESS) {
            struct pollfd pfd;
            pfd.fd = fd;
            pfd.events = POLLOUT;
            int poll_rc = poll(&pfd, 1, 2000); // 2 second connection timeout
            if (poll_rc > 0 && (pfd.revents & POLLOUT)) {
                int err = 0;
                socklen_t len = sizeof(err);
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
                if (err == 0) {
                    // Connected successfully
                    fcntl(fd, F_SETFL, flags); // restore blocking
                    break;
                }
            }
            close(fd);
            fd = -1;
        } else if (connect_res == 0) {
            fcntl(fd, F_SETFL, flags);
            break;
        } else {
            close(fd);
            fd = -1;
        }
    }
    freeaddrinfo(res);

    if (fd < 0) return false;

    // Set 2s receive and send timeouts
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    int flag = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    socket_fd_ = fd;
    return true;
}

void RemoteServerClient::disconnect_socket() {
    connected_.store(false);
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
}

bool RemoteServerClient::do_websocket_handshake() {
    std::ostringstream req;
    req << "GET " << path_ << " HTTP/1.1\r\n"
        << "Host: " << host_ << ":" << port_ << "\r\n"
        << "Upgrade: websocket\r\n"
        << "Connection: Upgrade\r\n"
        << "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        << "Sec-WebSocket-Version: 13\r\n"
        << "X-Condor-Run-Id: " << run_id_ << "\r\n"
        << "X-Condor-Pipeline: " << pipeline_name_ << "\r\n";

    if (!config_.token.empty()) {
        req << "Authorization: Bearer " << config_.token << "\r\n";
    }
    req << "\r\n";

    std::string req_str = req.str();
    ssize_t sent = send(socket_fd_, req_str.c_str(), req_str.size(), MSG_NOSIGNAL);
    if (sent != static_cast<ssize_t>(req_str.size())) {
        return false;
    }

    char buffer[2048];
    std::memset(buffer, 0, sizeof(buffer));
    ssize_t r = recv(socket_fd_, buffer, sizeof(buffer) - 1, 0);
    if (r <= 0) return false;

    std::string resp(buffer, r);
    return resp.find("101 Switching Protocols") != std::string::npos ||
           resp.find("101") != std::string::npos;
}

bool RemoteServerClient::send_websocket_frame(const std::string& text) {
    if (socket_fd_ < 0) return false;

    std::vector<uint8_t> frame;
    frame.push_back(0x81); // FIN + text opcode

    size_t len = text.size();
    if (len <= 125) {
        frame.push_back(0x80 | static_cast<uint8_t>(len)); // MASK bit set
    } else if (len <= 65535) {
        frame.push_back(0x80 | 126);
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        frame.push_back(0x80 | 127);
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<uint8_t>((len >> (i * 8)) & 0xFF));
        }
    }

    // 4-byte masking key
    uint8_t mask[4] = {0x37, 0xfa, 0x21, 0x3d};
    for (int i = 0; i < 4; ++i) {
        frame.push_back(mask[i]);
    }

    // Masked payload
    for (size_t i = 0; i < len; ++i) {
        frame.push_back(static_cast<uint8_t>(text[i]) ^ mask[i % 4]);
    }

    ssize_t res = send(socket_fd_, frame.data(), frame.size(), MSG_NOSIGNAL);
    return res == static_cast<ssize_t>(frame.size());
}

void RemoteServerClient::poll_incoming_websocket() {
    if (socket_fd_ < 0) return;

    struct pollfd pfd;
    pfd.fd = socket_fd_;
    pfd.events = POLLIN;

    int rc = poll(&pfd, 1, 20); // 20ms poll
    if (rc <= 0 || !(pfd.revents & POLLIN)) return;

    uint8_t header[2];
    ssize_t r = recv(socket_fd_, header, 2, 0);
    if (r <= 0) {
        disconnect_socket();
        return;
    }

    uint8_t opcode = header[0] & 0x0F;
    bool masked = (header[1] & 0x80) != 0;
    uint64_t payload_len = header[1] & 0x7F;

    if (payload_len == 126) {
        uint8_t len_bytes[2];
        if (recv(socket_fd_, len_bytes, 2, 0) != 2) return;
        payload_len = (static_cast<uint64_t>(len_bytes[0]) << 8) | len_bytes[1];
    } else if (payload_len == 127) {
        uint8_t len_bytes[8];
        if (recv(socket_fd_, len_bytes, 8, 0) != 8) return;
        payload_len = 0;
        for (int i = 0; i < 8; ++i) {
            payload_len = (payload_len << 8) | len_bytes[i];
        }
    }

    uint8_t mask[4] = {0};
    if (masked) {
        if (recv(socket_fd_, mask, 4, 0) != 4) return;
    }

    std::vector<uint8_t> payload(payload_len);
    size_t total_read = 0;
    while (total_read < payload_len) {
        ssize_t n = recv(socket_fd_, payload.data() + total_read, payload_len - total_read, 0);
        if (n <= 0) break;
        total_read += n;
    }

    if (masked) {
        for (size_t i = 0; i < total_read; ++i) {
            payload[i] ^= mask[i % 4];
        }
    }

    if (opcode == 0x1) { // Text frame
        std::string json_text(payload.begin(), payload.end());
        process_received_command(json_text);
    } else if (opcode == 0x8) { // Close frame
        disconnect_socket();
    } else if (opcode == 0x9) { // Ping frame -> respond with Pong
        uint8_t pong[2] = {0x8A, 0x00};
        send(socket_fd_, pong, 2, MSG_NOSIGNAL);
    }
}

void RemoteServerClient::do_http_post(const std::string& json_str) {
    if (!connect_socket()) {
        disconnect_socket();
        return;
    }

    std::ostringstream req;
    req << "POST " << path_ << " HTTP/1.1\r\n"
        << "Host: " << host_ << ":" << port_ << "\r\n"
        << "Content-Type: application/json\r\n"
        << "Content-Length: " << json_str.size() << "\r\n"
        << "User-Agent: Condor/1.0\r\n"
        << "Connection: close\r\n";

    if (!config_.token.empty()) {
        req << "Authorization: Bearer " << config_.token << "\r\n";
    }
    req << "\r\n" << json_str;

    std::string req_str = req.str();
    send(socket_fd_, req_str.c_str(), req_str.size(), MSG_NOSIGNAL);

    char buffer[4096];
    std::memset(buffer, 0, sizeof(buffer));
    ssize_t r = recv(socket_fd_, buffer, sizeof(buffer) - 1, 0);
    disconnect_socket();

    if (r > 0) {
        std::string resp(buffer, r);
        size_t body_pos = resp.find("\r\n\r\n");
        if (body_pos != std::string::npos) {
            std::string body = resp.substr(body_pos + 4);
            if (!body.empty() && body[0] == '{') {
                process_received_command(body);
            }
        }
    }
}

void RemoteServerClient::process_received_command(const std::string& raw_json) {
    try {
        auto j = nlohmann::json::parse(raw_json);
        CommandCallback cb;
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            cb = command_handler_;
        }
        if (!cb) return;

        // Check single command: { "type": "command", "action": "pause", ... }
        if (j.contains("action") && j["action"].is_string()) {
            cb(j["action"].get<std::string>(), j);
        }
        // Check command list: { "commands": [ { "action": "pause" } ] }
        else if (j.contains("commands") && j["commands"].is_array()) {
            for (const auto& item : j["commands"]) {
                if (item.contains("action") && item["action"].is_string()) {
                    cb(item["action"].get<std::string>(), item);
                }
            }
        }
    } catch (...) {
        // Silently ignore malformed frames
    }
}

void RemoteServerClient::worker_loop() {
    bool is_ws = (protocol_ == "ws" || protocol_ == "wss");
    auto last_connect_attempt = std::chrono::steady_clock::now() - std::chrono::seconds(10);

    while (running_.load()) {
        if (is_ws) {
            if (!connected_.load()) {
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::seconds>(now - last_connect_attempt).count() >= 3) {
                    last_connect_attempt = now;
                    if (connect_socket() && do_websocket_handshake()) {
                        connected_.store(true);
                        // Send connection announcement
                        nlohmann::json hello;
                        hello["type"] = "client_hello";
                        hello["run_id"] = run_id_;
                        hello["pipeline"] = pipeline_name_;
                        hello["project"] = config_.project;
                        hello["tags"] = config_.tags;
                        send_websocket_frame(hello.dump());
                    } else {
                        disconnect_socket();
                    }
                }
            }

            if (connected_.load()) {
                // Send queued messages
                std::string msg_to_send;
                {
                    std::lock_guard<std::mutex> lock(queue_mutex_);
                    if (!outbound_queue_.empty()) {
                        msg_to_send = std::move(outbound_queue_.front());
                        outbound_queue_.pop();
                    }
                }

                if (!msg_to_send.empty()) {
                    if (!send_websocket_frame(msg_to_send)) {
                        disconnect_socket();
                    }
                }

                // Poll incoming frames
                poll_incoming_websocket();
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        } else {
            // HTTP POST mode
            std::string msg_to_send;
            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                if (!outbound_queue_.empty()) {
                    msg_to_send = std::move(outbound_queue_.front());
                    outbound_queue_.pop();
                }
            }

            if (!msg_to_send.empty()) {
                do_http_post(msg_to_send);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(config_.poll_interval_ms));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

} // namespace condor
