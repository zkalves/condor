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

#include "condor/menu.hpp"
#include "condor/scheduler.hpp"
#include "condor/logging.hpp"
#include <iostream>
#include <chrono>
#include <signal.h>
#include <unistd.h>
#include <poll.h>
#include <sstream>
#include <cstdlib>

namespace condor {

std::atomic<int64_t> RuntimeMenu::last_sigint_time_ms_{0};
std::atomic<bool> RuntimeMenu::hard_kill_triggered_{false};
std::atomic<bool> RuntimeMenu::menu_requested_{false};
Scheduler* RuntimeMenu::active_scheduler_{nullptr};
struct termios RuntimeMenu::orig_termios_{};
std::atomic<bool> RuntimeMenu::raw_mode_active_{false};
std::atomic<bool> RuntimeMenu::atexit_registered_{false};

TerminalModeGuard::TerminalModeGuard(bool enable) : active_(enable) {
    if (active_) {
        RuntimeMenu::enable_raw_mode();
    }
}

TerminalModeGuard::~TerminalModeGuard() {
    if (active_) {
        RuntimeMenu::disable_raw_mode();
    }
}

TerminalModeGuard::TerminalModeGuard(TerminalModeGuard&& other) noexcept
    : active_(other.active_) {
    other.active_ = false;
}

TerminalModeGuard& TerminalModeGuard::operator=(TerminalModeGuard&& other) noexcept {
    if (this != &other) {
        if (active_) {
            RuntimeMenu::disable_raw_mode();
        }
        active_ = other.active_;
        other.active_ = false;
    }
    return *this;
}

static int64_t get_time_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool RuntimeMenu::is_terminal_interactive() {
    if (!isatty(STDIN_FILENO)) return false;
    pid_t fg_pgrp = tcgetpgrp(STDIN_FILENO);
    if (fg_pgrp != -1 && fg_pgrp != getpgrp()) {
        return false;
    }
    return true;
}

void RuntimeMenu::enable_raw_mode() {
    if (!is_terminal_interactive()) return;

    if (raw_mode_active_.load()) return;

    if (!atexit_registered_.exchange(true)) {
        std::atexit(&RuntimeMenu::disable_raw_mode);
    }

    if (tcgetattr(STDIN_FILENO, &orig_termios_) != 0) {
        return;
    }

    struct termios raw = orig_termios_;
    // Suppress echo, newline echo, canonical line buffering, and control char echo (^C)
    raw.c_lflag &= ~(ECHO | ECHONL | ICANON | ECHOCTL);
    // Disable software flow control (Ctrl+S / Ctrl+Q)
    raw.c_iflag &= ~(IXON);
    // Non-blocking single-byte reads
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;

    // Temporarily ignore SIGTTOU to avoid process stoppage when backgrounded
    struct sigaction sa_ign, sa_old;
    sa_ign.sa_handler = SIG_IGN;
    sigemptyset(&sa_ign.sa_mask);
    sa_ign.sa_flags = 0;
    sigaction(SIGTTOU, &sa_ign, &sa_old);

    int ret = tcsetattr(STDIN_FILENO, TCSANOW, &raw);

    sigaction(SIGTTOU, &sa_old, nullptr);

    if (ret == 0) {
        raw_mode_active_.store(true);
        // Hide terminal cursor during live display
        std::cout << "\033[?25l" << std::flush;
    }
}

void RuntimeMenu::disable_raw_mode() {
    bool expected = true;
    if (!raw_mode_active_.compare_exchange_strong(expected, false)) {
        return;
    }

    if (!isatty(STDIN_FILENO)) return;

    // Temporarily ignore SIGTTOU while resetting terminal attributes
    struct sigaction sa_ign, sa_old;
    sa_ign.sa_handler = SIG_IGN;
    sigemptyset(&sa_ign.sa_mask);
    sa_ign.sa_flags = 0;
    sigaction(SIGTTOU, &sa_ign, &sa_old);

    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios_);

    sigaction(SIGTTOU, &sa_old, nullptr);

    // Restore terminal cursor to visible (async-signal-safe)
    ssize_t written = write(STDOUT_FILENO, "\033[?25h", 6);
    (void)written;
}

bool RuntimeMenu::is_raw_mode_active() {
    return raw_mode_active_.load();
}

void RuntimeMenu::handle_sigint() {
    int64_t now = get_time_ms();
    int64_t prev = last_sigint_time_ms_.exchange(now);

    if (now - prev < 2000) {
        // Double CTRL+C within 2 seconds
        trigger_hard_kill();
        const char msg[] = "\n[ABORT] Double CTRL+C detected! Immediately terminating all jobs...\n";
        ssize_t written = write(STDERR_FILENO, msg, sizeof(msg) - 1);
        (void)written;
        if (active_scheduler_) {
            active_scheduler_->abort_all();
        }
    } else {
        // First CTRL+C
        menu_requested_ = true;
        const char msg[] = "\n(Press CTRL+C again within 2 seconds to abort all jobs, or use the runtime menu)\n";
        ssize_t written = write(STDERR_FILENO, msg, sizeof(msg) - 1);
        (void)written;
    }
}

static void sigint_handler(int sig) {
    (void)sig;
    RuntimeMenu::handle_sigint();
}

static void fatal_signal_handler(int sig) {
    RuntimeMenu::disable_raw_mode();
    RuntimeMenu::restore_signal_handlers();
    raise(sig);
}

void RuntimeMenu::init_signal_handlers(Scheduler* scheduler) {
    active_scheduler_ = scheduler;

    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);

    struct sigaction fatal_sa;
    fatal_sa.sa_handler = fatal_signal_handler;
    sigemptyset(&fatal_sa.sa_mask);
    fatal_sa.sa_flags = 0;
    sigaction(SIGTERM, &fatal_sa, nullptr);
    sigaction(SIGHUP, &fatal_sa, nullptr);
    sigaction(SIGQUIT, &fatal_sa, nullptr);
}

void RuntimeMenu::restore_signal_handlers() {
    struct sigaction sa;
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);
    sigaction(SIGQUIT, &sa, nullptr);
    active_scheduler_ = nullptr;
}

bool RuntimeMenu::is_hard_kill_triggered() {
    return hard_kill_triggered_;
}

bool RuntimeMenu::is_menu_requested() {
    return menu_requested_;
}

void RuntimeMenu::trigger_hard_kill() {
    hard_kill_triggered_ = true;
}

void RuntimeMenu::clear_menu_request() {
    menu_requested_ = false;
}

void RuntimeMenu::poll_stdin_for_menu() {
    if (!is_terminal_interactive()) return;

    struct pollfd pfd;
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;
    pfd.revents = 0;

    // Drain available keystrokes non-blockingly so stray inputs don't corrupt the HUD
    while (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
        char ch = 0;
        if (read(STDIN_FILENO, &ch, 1) <= 0) break;
        if (ch == 'm' || ch == 'M' || ch == 'h' || ch == 'H' || ch == '?') {
            menu_requested_ = true;
        }
    }
}

void RuntimeMenu::print_notice(std::ostream& os) {
    os << "condor  Copyright (C) 2026  Ezequiel Alves\n"
       << "This program comes with ABSOLUTELY NO WARRANTY; for details type `condor --warranty'.\n"
       << "This is free software, and you are welcome to redistribute it\n"
       << "under certain conditions; type `condor --license' for details.\n";
}

void RuntimeMenu::print_warranty(std::ostream& os) {
    os << "\n"
       << "================================================================================\n"
       << "                         NO WARRANTY (GNU GPLv3 §15-17)                         \n"
       << "================================================================================\n"
       << "\n"
       << "  15. Disclaimer of Warranty.\n"
       << "\n"
       << "  THERE IS NO WARRANTY FOR THE PROGRAM, TO THE EXTENT PERMITTED BY\n"
       << "APPLICABLE LAW.  EXCEPT WHEN OTHERWISE STATED IN WRITING THE COPYRIGHT\n"
       << "HOLDERS AND/OR OTHER PARTIES PROVIDE THE PROGRAM \"AS IS\" WITHOUT WARRANTY\n"
       << "OF ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING, BUT NOT LIMITED TO,\n"
       << "THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR\n"
       << "PURPOSE.  THE ENTIRE RISK AS TO THE QUALITY AND PERFORMANCE OF THE PROGRAM\n"
       << "IS WITH YOU.  SHOULD THE PROGRAM PROVE DEFECTIVE, YOU ASSUME THE COST OF\n"
       << "ALL NECESSARY SERVICING, REPAIR OR CORRECTION.\n"
       << "\n"
       << "  16. Limitation of Liability.\n"
       << "\n"
       << "  IN NO EVENT UNLESS REQUIRED BY APPLICABLE LAW OR AGREED TO IN WRITING\n"
       << "WILL ANY COPYRIGHT HOLDER, OR ANY OTHER PARTY WHO MODIFIES AND/OR CONVEYS\n"
       << "THE PROGRAM AS PERMITTED ABOVE, BE LIABLE TO YOU FOR DAMAGES, INCLUDING ANY\n"
       << "GENERAL, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES ARISING OUT OF THE\n"
       << "USE OR INABILITY TO USE THE PROGRAM (INCLUDING BUT NOT LIMITED TO LOSS OF\n"
       << "DATA OR DATA BEING RENDERED INACCURATE OR LOSSES SUSTAINED BY YOU OR THIRD\n"
       << "PARTIES OR A FAILURE OF THE PROGRAM TO OPERATE WITH ANY OTHER PROGRAMS),\n"
       << "EVEN IF SUCH HOLDER OR OTHER PARTY HAS BEEN ADVISED OF THE POSSIBILITY OF\n"
       << "SUCH DAMAGES.\n"
       << "\n"
       << "  17. Interpretation of Sections 15 and 16.\n"
       << "\n"
       << "  If the disclaimer of warranty and limitation of liability provided\n"
       << "above cannot be given local legal effect according to their terms,\n"
       << "reviewing courts shall apply local law that most closely approximates\n"
       << "an absolute waiver of all civil liability in connection with the\n"
       << "Program, unless a warranty or assumption of liability accompanies a\n"
       << "copy of the Program in return for a fee.\n"
       << "================================================================================\n\n";
}

void RuntimeMenu::print_conditions(std::ostream& os) {
    os << "\n"
       << "================================================================================\n"
       << "                 CONDITIONS FOR REDISTRIBUTION (GNU GPLv3 §4-5)                 \n"
       << "================================================================================\n"
       << "\n"
       << "This program is free software: you can redistribute it and/or modify\n"
       << "it under the terms of the GNU General Public License as published by\n"
       << "the Free Software Foundation, either version 3 of the License, or\n"
       << "(at your option) any later version.\n"
       << "\n"
       << "Key terms for redistributing Condor:\n"
       << "\n"
       << "  4. Conveying Verbatim Copies.\n"
       << "  You may convey verbatim copies of the Program's source code as you\n"
       << "  receive it, in any medium, provided that you conspicuously and\n"
       << "  appropriately publish on each copy an appropriate copyright notice;\n"
       << "  keep intact all notices stating that this License applies; keep intact\n"
       << "  all notices of the absence of any warranty; give all recipients a copy\n"
       << "  of this License along with the Program; and do not charge fees beyond\n"
       << "  physically performing the conveyance or optional support.\n"
       << "\n"
       << "  5. Conveying Modified Source Versions.\n"
       << "  You may convey a work based on the Program, or the modifications to\n"
       << "  produce it from the Program, in the form of source code under the\n"
       << "  terms of section 4, provided that you also meet all of these conditions:\n"
       << "    a) The work must carry prominent notices stating that you modified it,\n"
       << "       and giving a relevant date.\n"
       << "    b) The work must carry prominent notices stating that it is released\n"
       << "       under this License and any conditions added under section 7.\n"
       << "    c) You must license the entire work, as a whole, under this License\n"
       << "       to anyone who comes into possession of a copy.\n"
       << "\n"
       << "For the complete license text, consult the LICENSE file included with\n"
       << "Condor, or see <https://www.gnu.org/licenses/gpl-3.0.html>.\n"
       << "================================================================================\n\n";
}

void RuntimeMenu::print_help() {
    int cols = get_terminal_width(80);
    int banner_width = std::clamp(cols, 54, 80);
    std::string div(banner_width, '=');

    std::string title = " CONDOR RUNTIME MENU ";
    int pad_left = std::max(2, (banner_width - static_cast<int>(title.length())) / 2);
    int pad_right = std::max(2, banner_width - static_cast<int>(title.length()) - pad_left);
    std::string header = std::string(pad_left, '=') + title + std::string(pad_right, '=');

    std::cout << "\n" << header << "\n";
    print_notice();
    std::cout << div << "\n"
              << " [s]        Status: Print detailed table of stages & jobs\n"
              << " [p]        Pause / Resume: Toggle dispatching new jobs\n"
              << " [t <id>]   Tail Log: View recent output (stdout/stderr) for a job\n"
              << " [k <id>]   Kill Job: Terminate active job by job_id\n"
              << " [j <N>]    Scale Slots: Adjust maximum concurrent jobs\n"
              << " [w]        Warranty: Display GNU GPL warranty disclaimer\n"
              << " [l]        License: Display GNU GPL redistribution conditions\n"
              << " [c]        Continue: Resume live progress display\n"
              << " [q]        Quit / Abort All: Terminate all jobs & exit\n"
              << div << "\n";
    if (active_scheduler_) {
        std::cout << " Emergency kill command (if CLI crashes):\n"
                  << "   " << active_scheduler_->workspace().kill_script_path().string() << "\n"
                  << "   (or: pkill -f '" << active_scheduler_->workspace().run_dir().string() << "')\n"
                  << div << "\n";
    }
    std::cout << "Enter command: " << std::flush;
}

void RuntimeMenu::run_menu(Scheduler& scheduler) {
    clear_menu_request();
    bool was_raw = is_raw_mode_active();
    if (was_raw) {
        disable_raw_mode();
    }

    print_help();

    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "w" || cmd == "warranty" || (cmd == "show" && line.find('w') != std::string::npos)) {
            print_warranty();
            std::cout << "Enter command (or 'c' to continue): " << std::flush;
            continue;
        } else if (cmd == "l" || cmd == "license" || cmd == "copying" || (cmd == "show" && line.find('c') != std::string::npos)) {
            print_conditions();
            std::cout << "Enter command (or 'c' to continue): " << std::flush;
            continue;
        } else if (cmd == "s" || cmd == "status") {
            scheduler.print_status_table();
            break;
        } else if (cmd == "p" || cmd == "pause") {
            scheduler.toggle_pause();
            break;
        } else if (cmd == "t" || cmd == "tail") {
            std::string job_id;
            iss >> job_id;
            scheduler.tail_job(job_id);
            std::cout << "Enter command (or 'c' to continue): " << std::flush;
            continue;
        } else if (cmd == "k" || cmd == "kill") {
            std::string job_id;
            iss >> job_id;
            if (!job_id.empty()) {
                scheduler.kill_job(job_id);
            } else {
                std::cout << "Usage: k <job_id> (e.g. k 0001)\n";
            }
            break;
        } else if (cmd == "j" || cmd == "slots") {
            size_t slots = 0;
            if (iss >> slots && slots > 0) {
                scheduler.scale_slots(slots);
            } else {
                std::cout << "Usage: j <N> (e.g. j 8)\n";
            }
            break;
        } else if (cmd == "q" || cmd == "quit") {
            std::cout << "Aborting all active jobs...\n";
            scheduler.abort_all();
            break;
        } else if (cmd == "c" || cmd == "continue" || cmd.empty()) {
            std::cout << "Resuming live display...\n";
            break;
        } else {
            std::cout << "Unknown command: " << cmd << "\n";
            std::cout << "Enter command (or 'c' to continue): " << std::flush;
            continue;
        }
    }

    if (was_raw && !scheduler.is_aborted()) {
        enable_raw_mode();
    }
}

} // namespace condor
