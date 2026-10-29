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

#include "condor/progress.hpp"
#include "condor/logging.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <unistd.h>

namespace condor {

ProgressDisplay::ProgressDisplay(bool interactive, int status_interval_sec)
    : interactive_(interactive),
      status_interval_sec_(status_interval_sec > 0 ? status_interval_sec : 30),
      last_dump_time_(std::chrono::steady_clock::now()) {}

ProgressDisplay::~ProgressDisplay() {
    // Show cursor if interactive
    if (interactive_) {
        std::cout << "\033[?25h" << std::flush;
    }
}

void ProgressDisplay::set_paused(bool paused) {
    std::lock_guard<std::mutex> lock(mutex_);
    paused_ = paused;
    if (paused_ && interactive_) {
        // Clear active display lines so menu can render cleanly
        if (last_lines_rendered_ > 0) {
            for (int i = 0; i < last_lines_rendered_; ++i) {
                std::cout << "\033[F\033[2K";
            }
            std::cout << std::flush;
            last_lines_rendered_ = 0;
        }
        std::cout << "\033[?25h" << std::flush; // Show cursor
    } else if (!paused_ && interactive_) {
        std::cout << "\033[?25l" << std::flush; // Hide cursor during active HUD
    }
}

bool ProgressDisplay::is_paused() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return paused_;
}

std::string ProgressDisplay::format_duration(double seconds) {
    int total_sec = static_cast<int>(std::round(seconds));
    int hrs = total_sec / 3600;
    int mins = (total_sec % 3600) / 60;
    int secs = total_sec % 60;

    std::ostringstream ss;
    ss << std::setfill('0') << std::setw(2) << hrs << ":"
       << std::setfill('0') << std::setw(2) << mins << ":"
       << std::setfill('0') << std::setw(2) << secs;
    return ss.str();
}

void ProgressDisplay::update(const ProgressStats& stats,
                            const std::vector<StageSummaryInfo>& stages,
                            const std::vector<ActiveJobInfo>& active_jobs) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (paused_) return;

    if (interactive_) {
        render_interactive(stats, stages, active_jobs);
    } else {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_dump_time_).count();
        if (elapsed >= status_interval_sec_) {
            render_non_interactive(stats, stages);
            last_dump_time_ = now;
        }
    }
}

void ProgressDisplay::render_interactive(const ProgressStats& stats,
                                        const std::vector<StageSummaryInfo>& stages,
                                        const std::vector<ActiveJobInfo>& active_jobs) {
    int cols = get_terminal_width(80);

    // Clear previous render
    if (last_lines_rendered_ > 0) {
        for (int i = 0; i < last_lines_rendered_; ++i) {
            std::cout << "\033[F\033[2K";
        }
    }

    int lines = 0;

    // Spinner animation frames to indicate active execution during long-running tasks
    static constexpr std::array<char, 4> SPINNER = {'|', '/', '-', '\\'};
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    char spinner_char = SPINNER[(now_ms / 120) % SPINNER.size()];

    // Calculate stage column width and prefix width to align all progress bars
    size_t max_stage_len = 16;
    for (const auto& s : stages) {
        if (s.stage_name.length() > max_stage_len) {
            max_stage_len = s.stage_name.length();
        }
    }
    int stage_col_width = std::clamp(static_cast<int>(max_stage_len) + 1, 16, 26);
    int prefix_width = 9 + stage_col_width + 1; // "  Stage: " (9) + stage_col_width + " " (1)

    int bar_avail = cols - prefix_width - 29 - 2;
    int bar_len = std::clamp(bar_avail, 10, 50);

    // 1. Global DAG Progress Bar (aligned with stage progress bars)
    size_t completed = stats.passed_jobs + stats.failed_jobs + stats.skipped_jobs;
    int percent = (stats.total_jobs > 0) ? static_cast<int>((completed * 100) / stats.total_jobs) : 100;

    bool pipeline_running = (stats.running_jobs > 0 || stats.queued_jobs > 0);
    std::string dag_label = pipeline_running ? ("[DAG Pipeline " + std::string(1, spinner_char) + "]") : "[DAG Pipeline]";
    std::ostringstream dag_prefix_ss;
    dag_prefix_ss << std::left << std::setw(prefix_width) << dag_label;
    std::string dag_prefix = dag_prefix_ss.str();

    std::ostringstream dag_suffix;
    dag_suffix << " " << std::right << std::setw(3) << percent << "% | "
               << "Jobs: " << stats.total_jobs << " | "
               << "Running: " << stats.running_jobs << " | "
               << "Queued: " << stats.queued_jobs << " | "
               << "Passed: " << stats.passed_jobs << " | "
               << "Elapsed: " << format_duration(stats.elapsed_sec);
    std::string dag_suffix_str = dag_suffix.str();

    int filled = (percent * bar_len) / 100;
    filled = std::clamp(filled, 0, bar_len);

    std::string bar = "[" + std::string(filled, '#') + std::string(bar_len - filled, '-') + "]";
    std::string dag_line = dag_prefix + bar + dag_suffix_str;
    if (static_cast<int>(dag_line.length()) > cols) {
        dag_line = dag_line.substr(0, cols);
    }
    std::cout << dag_line << "\n";
    lines++;

    // 2. Individual Independent Stage Progress Bars (dynamically scaled)
    for (const auto& s : stages) {
        size_t stage_done = s.passed + s.failed + s.skipped;
        int stage_pct = (s.total > 0) ? static_cast<int>((stage_done * 100) / s.total) : 100;

        std::string state_badge = "[QUEUED]";
        if (s.running > 0) {
            state_badge = "[RUNNING " + std::string(1, spinner_char) + "]";
        } else if (s.failed > 0) {
            state_badge = "[FAILED]";
        } else if (s.passed == s.total && s.total > 0) {
            state_badge = "[PASSED]";
        } else if (s.skipped == s.total && s.total > 0) {
            state_badge = "[SKIPPED]";
        }

        std::ostringstream counts_ss;
        if (s.skipped == s.total && s.total > 0) {
            counts_ss << "(" << s.skipped << "/" << s.total << " skipped)";
        } else {
            counts_ss << "(" << s.passed << "/" << s.total << " passed";
            if (s.running > 0) counts_ss << ", " << s.running << " running";
            if (s.failed > 0)  counts_ss << ", " << s.failed << " failed";
            if (s.skipped > 0) counts_ss << ", " << s.skipped << " skipped";
            counts_ss << ")";
        }
        std::string counts_str = counts_ss.str();

        std::string s_name = s.stage_name;
        if (static_cast<int>(s_name.length()) > stage_col_width) {
            s_name = s_name.substr(0, stage_col_width);
        }

        std::ostringstream prefix_ss;
        prefix_ss << "  Stage: " << std::left << std::setw(stage_col_width) << s_name << " ";
        std::string prefix_str = prefix_ss.str();

        std::ostringstream suffix_ss;
        suffix_ss << " " << std::right << std::setw(3) << stage_pct << "% "
                  << std::left << std::setw(11) << state_badge << " "
                  << counts_str;
        std::string s_suffix_str = suffix_ss.str();

        int s_filled = (stage_pct * bar_len) / 100;
        s_filled = std::clamp(s_filled, 0, bar_len);

        std::string s_bar = "[" + std::string(s_filled, '#') + std::string(bar_len - s_filled, '-') + "]";
        std::string stage_line = prefix_str + s_bar + s_suffix_str;
        if (static_cast<int>(stage_line.length()) > cols) {
            stage_line = stage_line.substr(0, cols);
        }
        std::cout << stage_line << "\n";
        lines++;
    }

    // 3. Active Job Ticker (hide in SUMMARY mode)
    if (output_mode_ != OutputMode::SUMMARY && !active_jobs.empty()) {
        size_t display_count = std::min<size_t>(active_jobs.size(), 4);
        for (size_t i = 0; i < display_count; ++i) {
            const auto& job = active_jobs[i];
            std::ostringstream ticker_ss;
            ticker_ss << "    " << spinner_char << " [" << job.job_id << "] " << job.stage_name << "/" << job.job_name
                      << " (PID: " << job.pid_or_grid_id << ") - "
                      << format_duration(job.elapsed_sec);
            std::string ticker_line = ticker_ss.str();
            if (static_cast<int>(ticker_line.length()) > cols) {
                ticker_line = ticker_line.substr(0, std::max(10, cols - 3)) + "...";
            }
            std::cout << ticker_line << "\n";
            lines++;
        }
        if (active_jobs.size() > display_count) {
            std::cout << "    " << spinner_char << " ... and " << (active_jobs.size() - display_count) << " more active jobs\n";
            lines++;
        }
    }

    // 4. Interactive Controls & Emergency Cleanup Footer
    int banner_cols = std::clamp(cols, 40, 80);
    std::string div_line(banner_cols, '-');
    std::cout << div_line << "\n";
    lines++;

    std::cout << "Available keys: [m] Runtime Menu | [Ctrl+C] Cancel / Abort\n";
    lines++;

    std::string kill_cmd = !kill_script_path_.empty()
                               ? kill_script_path_.string()
                               : ("pkill -f '" + run_dir_.string() + "'");
    std::ostringstream kill_ss;
    kill_ss << "Emergency kill (if CLI crashes): " << kill_cmd;
    std::string kill_str = kill_ss.str();
    if (static_cast<int>(kill_str.length()) > cols) {
        kill_str = kill_str.substr(0, std::max(10, cols - 3)) + "...";
    }
    std::cout << kill_str << "\n";
    lines++;

    std::cout << std::flush;
    last_lines_rendered_ = lines;
}

void ProgressDisplay::render_non_interactive(const ProgressStats& stats,
                                            const std::vector<StageSummaryInfo>& stages) {
    size_t completed = stats.passed_jobs + stats.failed_jobs + stats.skipped_jobs;
    int percent = (stats.total_jobs > 0) ? static_cast<int>((completed * 100) / stats.total_jobs) : 100;

    std::ostringstream ss;
    ss << "[" << Logger::current_timestamp() << "] PROGRESS: " << percent << "% ("
       << stats.passed_jobs << "/" << stats.total_jobs << " passed";
    if (stats.skipped_jobs > 0) ss << ", " << stats.skipped_jobs << " skipped";
    if (stats.failed_jobs > 0)  ss << ", " << stats.failed_jobs << " failed";
    ss << ") | ";

    for (size_t i = 0; i < stages.size(); ++i) {
        const auto& s = stages[i];
        size_t done = s.passed + s.failed + s.skipped;
        int pct = (s.total > 0) ? static_cast<int>((done * 100) / s.total) : 100;
        ss << s.stage_name << ": " << pct << "%";
        if (s.running > 0) ss << " (" << s.running << " run)";
        if (i + 1 < stages.size()) ss << " | ";
    }

    std::cout << ss.str() << std::endl;
}

void ProgressDisplay::on_job_started(std::string_view stage_name, std::string_view job_id, std::string_view job_name) {
    if (!interactive_ && output_mode_ == OutputMode::ALL) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::cout << "[" << Logger::current_timestamp() << "] [START]  ["
                  << job_id << "] " << stage_name << "/" << job_name << std::endl;
    }
}

void ProgressDisplay::on_job_finished(std::string_view stage_name, std::string_view job_id, std::string_view job_name,
                                      JobStatus status, int exit_code, double elapsed_sec) {
    if (!interactive_) {
        bool should_print = false;
        if (output_mode_ == OutputMode::ALL) {
            should_print = true;
        } else if (output_mode_ == OutputMode::FAILURES) {
            should_print = (status == JobStatus::FAILED || status == JobStatus::ABORTED);
        }

        if (should_print) {
            std::lock_guard<std::mutex> lock(mutex_);
            std::cout << "[" << Logger::current_timestamp() << "] ["
                      << ((status == JobStatus::FAILED || status == JobStatus::ABORTED) ? "FAIL " : "FINISH")
                      << "] [" << job_id << "] " << stage_name << "/" << job_name
                      << " -> " << to_string(status) << " (exit: " << exit_code
                      << ", elapsed: " << std::fixed << std::setprecision(2) << elapsed_sec << "s)" << std::endl;
        }
    }
}

void ProgressDisplay::print_final_summary(const ProgressStats& stats, bool success, const std::string& html_report_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (interactive_ && last_lines_rendered_ > 0) {
        for (int i = 0; i < last_lines_rendered_; ++i) {
            std::cout << "\033[F\033[2K";
        }
        last_lines_rendered_ = 0;
    }

    int cols = get_terminal_width(80);
    int banner_width = std::clamp(cols, 60, 100);
    std::string div(banner_width, '=');

    std::string title = " CONDOR PIPELINE EXECUTION SUMMARY ";
    int pad_left = std::max(2, (banner_width - static_cast<int>(title.length())) / 2);
    int pad_right = std::max(2, banner_width - static_cast<int>(title.length()) - pad_left);
    std::string header = std::string(pad_left, '=') + title + std::string(pad_right, '=');

    std::cout << "\n" << header << "\n"
              << "Result:       " << (success ? "SUCCESS" : "FAILURE") << "\n"
              << "Total Jobs:   " << stats.total_jobs << "\n"
              << "Passed:       " << stats.passed_jobs << "\n"
              << "Failed:       " << stats.failed_jobs << "\n"
              << "Skipped:      " << stats.skipped_jobs << "\n"
              << "Total Time:   " << format_duration(stats.elapsed_sec) << "\n";
    if (!html_report_path.empty()) {
        std::cout << "HTML Report:  " << html_report_path << "\n";
    }
    std::cout << div << "\n" << std::endl;
    if (interactive_) {
        std::cout << "\033[?25h" << std::flush;
    }
}

} // namespace condor
