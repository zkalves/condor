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

/**
 * @file menu.hpp
 * @brief Interactive keyboard runtime menu and double SIGINT handler.
 * @author Ezequiel Alves
 * @copyright Copyright (C) 2026 Ezequiel Alves. All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <string>
#include <atomic>
#include <functional>
#include <vector>
#include <iostream>
#include <termios.h>
#include "condor/common.hpp"

namespace condor {

class Scheduler; // Forward declaration

/**
 * @class TerminalModeGuard
 * @brief RAII guard that configures terminal into non-canonical no-echo mode
 *        to prevent user keystrokes from disrupting ANSI presentation.
 */
class TerminalModeGuard {
public:
    explicit TerminalModeGuard(bool enable = true);
    ~TerminalModeGuard();
    TerminalModeGuard(const TerminalModeGuard&) = delete;
    TerminalModeGuard& operator=(const TerminalModeGuard&) = delete;
    TerminalModeGuard(TerminalModeGuard&& other) noexcept;
    TerminalModeGuard& operator=(TerminalModeGuard&& other) noexcept;

private:
    bool active_{false};
};

/**
 * @class RuntimeMenu
 * @brief Interactive terminal control interface and signal handling facility.
 *
 * Catches SIGINT (Ctrl+C) to prevent dirty pipeline termination. A single Ctrl+C pauses the scheduler
 * and presents an interactive menu (`[p]ause`, `[k]ill <id>`, `[s]cale <slots>`, `[q]uit`).
 * A double Ctrl+C within 2000ms triggers immediate emergency hard termination.
 */
class RuntimeMenu {
public:
    /**
     * @brief Installs signal handlers for SIGINT, SIGTERM, SIGHUP, and SIGQUIT.
     * @param scheduler Pointer to active Scheduler instance.
     */
    static void init_signal_handlers(Scheduler* scheduler);

    /**
     * @brief Restores default OS signal handlers before process exit.
     */
    static void restore_signal_handlers();

    /**
     * @brief Checks if a double-SIGINT hard kill was triggered.
     * @return True if hard kill was triggered.
     */
    static bool is_hard_kill_triggered();

    /**
     * @brief Checks if the interactive menu was requested via 'm' key or single Ctrl+C.
     * @return True if menu was requested.
     */
    static bool is_menu_requested();

    /**
     * @brief Manually triggers hard kill flag.
     */
    static void trigger_hard_kill();

    /**
     * @brief Clears the menu request flag.
     */
    static void clear_menu_request();

    /**
     * @brief Signal callback invoked upon receiving SIGINT.
     */
    static void handle_sigint();

    /**
     * @brief Polls standard input non-blockingly to detect 'm' key press.
     * Drains any stray keystrokes so they do not disrupt the live presentation.
     */
    static void poll_stdin_for_menu();

    /**
     * @brief Executes the interactive console menu loop.
     * @param scheduler Reference to active Scheduler.
     */
    static void run_menu(Scheduler& scheduler);

    /**
     * @brief Configures terminal into non-canonical, no-echo mode during live progress rendering.
     * Suppresses keystroke echoing so user typing doesn't disrupt the ANSI display.
     */
    static void enable_raw_mode();

    /**
     * @brief Restores original terminal attributes (echo, canonical mode, cursor visible).
     */
    static void disable_raw_mode();

    /**
     * @brief Checks if raw/no-echo mode is currently active.
     * @return True if raw mode active.
     */
    static bool is_raw_mode_active();

    /**
     * @brief Checks if terminal is attached to an interactive foreground TTY.
     * @return True if interactive foreground TTY.
     */
    static bool is_terminal_interactive();

    /**
     * @brief Outputs the GNU General Public License interactive startup notice.
     * @param os Output stream (defaults to std::cout).
     */
    static void print_notice(std::ostream& os = std::cout);

    /**
     * @brief Outputs the GNU General Public License warranty disclaimer (Sections 15-17).
     * @param os Output stream (defaults to std::cout).
     */
    static void print_warranty(std::ostream& os = std::cout);

    /**
     * @brief Outputs the GNU General Public License redistribution conditions (Sections 4-5).
     * @param os Output stream (defaults to std::cout).
     */
    static void print_conditions(std::ostream& os = std::cout);

    static std::atomic<int64_t> last_sigint_time_ms_; ///< Timestamp of last SIGINT signal in milliseconds.
    static std::atomic<bool> hard_kill_triggered_;     ///< Flag indicating emergency hard kill request.
    static std::atomic<bool> menu_requested_;          ///< Flag indicating runtime menu request.
    static Scheduler* active_scheduler_;               ///< Pointer to active running scheduler.
    static struct termios orig_termios_;               ///< Original terminal settings before entering raw mode.
    static std::atomic<bool> raw_mode_active_;          ///< Flag indicating whether raw mode is active.
    static std::atomic<bool> atexit_registered_;       ///< Flag indicating atexit cleanup registration.

private:
    static void print_help();
};

} // namespace condor
