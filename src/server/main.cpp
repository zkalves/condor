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

#include <iostream>
#include <csignal>
#include <CLI/CLI.hpp>

#include "server.hpp"

static std::atomic<bool> g_shutdown_requested{false};

static void signal_handler(int sig) {
    (void)sig;
    g_shutdown_requested.store(true);
}

int main(int argc, char** argv) {
    CLI::App app{"condor-hub - High-Concurrency Distributed Fleet Hub and Remote Control Server"};

    condor::hub::HubConfig config;

    app.add_option("-p,--port", config.port, "Listening TCP port (default: 8080)");
    app.add_option("-b,--bind,--host", config.host, "Network host address to bind (default: 0.0.0.0)");
    app.add_option("-t,--token", config.token, "Optional Bearer authentication token");
    app.add_option("-w,--workers", config.worker_threads, "Worker threads (default: 2)");
    app.add_option("-a,--artifacts-dir", config.artifacts_dir, "Directory to store client artifacts and HTML reports (default: ./hub_artifacts)");

    CLI11_PARSE(app, argc, argv);

    condor::hub::HubServer server(config);

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    if (!server.start()) {
        std::cerr << "[CONDOR-HUB] Fatal: Failed to start server." << std::endl;
        return 1;
    }

    while (!g_shutdown_requested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::cout << "\n[CONDOR-HUB] Shutdown signal received. Stopping server...\n" << std::flush;
    server.stop();
    std::cout << "[CONDOR-HUB] Server shutdown complete." << std::endl;
    return 0;
}
