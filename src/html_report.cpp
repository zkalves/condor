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

#include "condor/html_report.hpp"
#include "condor/scheduler.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <filesystem>
#include <unordered_set>

namespace condor {

static std::string escape_html(std::string_view text) {
    std::string out;
    out.reserve(text.size() * 1.1);
    for (char c : text) {
        switch (c) {
            case '&':  out.append("&amp;"); break;
            case '<':  out.append("&lt;"); break;
            case '>':  out.append("&gt;"); break;
            case '"':  out.append("&quot;"); break;
            case '\'': out.append("&#39;"); break;
            default:   out.push_back(c); break;
        }
    }
    return out;
}

static std::string read_file_preview(const std::filesystem::path& path, size_t max_bytes = 64 * 1024) {
    if (!std::filesystem::exists(path)) return "";
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return "";

    std::string content;
    ifs.seekg(0, std::ios::end);
    size_t size = ifs.tellg();
    if (size > max_bytes) {
        ifs.seekg(static_cast<std::streamoff>(size - max_bytes));
        content.resize(max_bytes);
        ifs.read(content.data(), max_bytes);
        return "[... output truncated to last 64 KB ...]\n" + content;
    } else {
        ifs.seekg(0, std::ios::beg);
        content.resize(size);
        ifs.read(content.data(), size);
        return content;
    }
}

// Discovers and parses custom YAML metrics in job directory
static std::vector<std::pair<std::string, std::string>> extract_yaml_metrics(const std::filesystem::path& job_dir) {
    std::vector<std::pair<std::string, std::string>> metrics;
    if (!std::filesystem::exists(job_dir)) return metrics;

    for (const auto& entry : std::filesystem::directory_iterator(job_dir)) {
        if (!entry.is_regular_file()) continue;
        std::string fname = entry.path().filename().string();
        if (fname == "job_status.yaml") continue; // Standard job record
        if (fname.size() > 5 && fname.substr(fname.size() - 5) == ".yaml") {
            std::ifstream ifs(entry.path());
            std::string line;
            while (std::getline(ifs, line)) {
                // Parse simple key: value lines
                auto colon = line.find(':');
                if (colon != std::string::npos) {
                    std::string key = line.substr(0, colon);
                    std::string val = line.substr(colon + 1);
                    // Trim key
                    while (!key.empty() && (key.front() == ' ' || key.front() == '\t')) key.erase(key.begin());
                    while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
                    // Trim val
                    while (!val.empty() && (val.front() == ' ' || val.front() == '\t' || val.front() == '"')) val.erase(val.begin());
                    while (!val.empty() && (val.back() == ' ' || val.back() == '\t' || val.back() == '"')) val.pop_back();
                    if (!key.empty() && !val.empty()) {
                        metrics.emplace_back(key, val);
                    }
                }
            }
        }
    }
    return metrics;
}

// Discovers HTML reports generated within the job directory or published via metrics
static std::vector<std::string> discover_html_reports(const std::filesystem::path& job_dir,
                                                     const std::vector<std::pair<std::string, std::string>>& metrics) {
    std::vector<std::string> reports;
    std::unordered_set<std::string> seen;

    // 1. Check custom metrics for explicit html report links
    for (const auto& [k, v] : metrics) {
        std::string lower_v = v;
        std::transform(lower_v.begin(), lower_v.end(), lower_v.begin(), ::tolower);
        if (lower_v.size() > 5 && (lower_v.ends_with(".html") || lower_v.ends_with(".htm"))) {
            std::filesystem::path candidate = v;
            std::string rel_path;
            if (candidate.is_absolute()) {
                std::error_code ec;
                rel_path = std::filesystem::relative(candidate, job_dir, ec).string();
                if (ec || rel_path.rfind("..", 0) == 0) {
                    rel_path = v;
                }
            } else {
                rel_path = v;
            }
            if (!seen.count(rel_path)) {
                reports.push_back(rel_path);
                seen.insert(rel_path);
            }
        }
    }

    if (!std::filesystem::exists(job_dir)) return reports;

    // 2. Discover .html files within job directory
    std::error_code ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(job_dir, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (ec) break;
        if (entry.is_regular_file()) {
            std::string fname = entry.path().filename().string();
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".html" || ext == ".htm") {
                std::string rel_path = std::filesystem::relative(entry.path(), job_dir, ec).string();
                if (!ec && !seen.count(rel_path)) {
                    if (fname == "index.html" || fname.find("cov") != std::string::npos) {
                        reports.insert(reports.begin(), rel_path);
                    } else {
                        reports.push_back(rel_path);
                    }
                    seen.insert(rel_path);
                }
            }
        }
    }
    return reports;
}

std::string HtmlReportGenerator::generate(
    const PipelineConfig& config,
    const WorkspaceManager& workspace,
    const std::vector<StageRuntimeState>& stages,
    const std::unordered_map<std::string, JobRuntimeState>& jobs,
    const ProgressStats& stats,
    bool success
) {
    std::ostringstream h;

    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    char time_buf[64];
    std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S UTC", std::gmtime(&now_c));

    h << R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Condor Pipeline Report - )HTML" << escape_html(config.name) << R"HTML(</title>
    <style>
        :root {
            --bg: #0d1117;
            --surface: #161b22;
            --surface-hover: #1f242c;
            --border: #30363d;
            --border-light: #484f58;
            --text-main: #e6edf3;
            --text-muted: #8b949e;
            --accent: #58a6ff;
            --pass-text: #3fb950;
            --pass-bg: rgba(63, 185, 80, 0.15);
            --pass-border: #238636;
            --fail-text: #f85149;
            --fail-bg: rgba(248, 81, 73, 0.15);
            --fail-border: #da3633;
            --skip-text: #8b949e;
            --skip-bg: rgba(139, 148, 158, 0.15);
            --skip-border: #484f58;
            --font-mono: ui-monospace, SFMono-Regular, "SF Mono", Menlo, Consolas, "Liberation Mono", monospace;
            --font-sans: -apple-system, BlinkMacSystemFont, "Segoe UI", Helvetica, Arial, sans-serif;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            background-color: var(--bg);
            color: var(--text-main);
            font-family: var(--font-sans);
            line-height: 1.5;
            padding: 24px;
        }
        .container {
            max-width: 1360px;
            margin: 0 auto;
        }
        .header {
            background-color: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            padding: 24px;
            margin-bottom: 20px;
            display: flex;
            justify-content: space-between;
            align-items: flex-start;
            flex-wrap: wrap;
            gap: 16px;
        }
        .header-title h1 {
            font-size: 26px;
            font-weight: 600;
            display: flex;
            align-items: center;
            gap: 12px;
        }
        .header-meta {
            margin-top: 8px;
            font-size: 13px;
            color: var(--text-muted);
            display: flex;
            flex-direction: column;
            gap: 4px;
        }
        .verdict-badge {
            font-size: 14px;
            font-weight: 700;
            padding: 6px 16px;
            border-radius: 20px;
            text-transform: uppercase;
            letter-spacing: 0.5px;
        }
        .verdict-pass {
            background-color: var(--pass-bg);
            color: var(--pass-text);
            border: 1px solid var(--pass-border);
        }
        .verdict-fail {
            background-color: var(--fail-bg);
            color: var(--fail-text);
            border: 1px solid var(--fail-border);
        }
        .stats-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 16px;
            margin-bottom: 24px;
        }
        .stat-card {
            background-color: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            padding: 16px 20px;
        }
        .stat-label {
            font-size: 12px;
            text-transform: uppercase;
            color: var(--text-muted);
            font-weight: 600;
            letter-spacing: 0.5px;
        }
        .stat-value {
            font-size: 28px;
            font-weight: 700;
            margin-top: 4px;
        }
        .stages-overview {
            background-color: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            padding: 20px;
            margin-bottom: 24px;
        }
        .section-title {
            font-size: 18px;
            font-weight: 600;
            margin-bottom: 16px;
            display: flex;
            align-items: center;
            justify-content: space-between;
        }
        .stage-bars {
            display: flex;
            flex-direction: column;
            gap: 12px;
        }
        .stage-bar-item {
            display: grid;
            grid-template-columns: 180px 1fr 140px 24px;
            align-items: center;
            gap: 16px;
            font-size: 14px;
            padding: 8px 12px;
            border-radius: 6px;
            cursor: pointer;
            transition: all 0.15s ease;
            border: 1px solid transparent;
        }
        .stage-bar-item:hover {
            background-color: var(--surface-hover);
            border-color: var(--border);
        }
        .stage-bar-arrow {
            color: var(--accent);
            font-size: 14px;
            opacity: 0.6;
            text-align: right;
            transition: transform 0.15s ease;
        }
        .stage-bar-item:hover .stage-bar-arrow {
            opacity: 1;
            transform: translateX(3px);
        }
        .progress-track {
            background-color: #21262d;
            border-radius: 6px;
            height: 12px;
            overflow: hidden;
            position: relative;
        }
        .progress-fill {
            height: 100%;
            border-radius: 6px;
            transition: width 0.3s ease;
        }
        .fill-pass { background-color: var(--pass-text); }
        .fill-fail { background-color: var(--fail-text); }
        .fill-skip { background-color: var(--skip-text); }

        .stage-nav-bar {
            background-color: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            padding: 14px 18px;
            margin-bottom: 16px;
            display: flex;
            align-items: center;
            gap: 12px;
            flex-wrap: wrap;
        }
        .stage-nav-label {
            font-size: 12px;
            font-weight: 700;
            text-transform: uppercase;
            letter-spacing: 0.5px;
            color: var(--text-muted);
            white-space: nowrap;
        }
        .stage-nav-group {
            display: flex;
            gap: 8px;
            flex-wrap: wrap;
        }
        .stage-tab-btn {
            background-color: #21262d;
            color: var(--text-muted);
            border: 1px solid var(--border);
            padding: 6px 14px;
            border-radius: 6px;
            font-size: 13px;
            cursor: pointer;
            font-weight: 500;
            display: inline-flex;
            align-items: center;
            gap: 8px;
            transition: all 0.15s ease;
        }
        .stage-tab-btn:hover {
            color: var(--text-main);
            border-color: var(--border-light);
        }
        .stage-tab-btn.active {
            background-color: rgba(88, 166, 255, 0.15);
            color: var(--accent);
            border-color: var(--accent);
            font-weight: 600;
        }
        .stage-tab-count {
            font-size: 11px;
            background-color: rgba(255, 255, 255, 0.08);
            padding: 1px 6px;
            border-radius: 10px;
            color: var(--text-main);
        }
        .stage-tab-btn.active .stage-tab-count {
            background-color: var(--accent);
            color: #0d1117;
            font-weight: 700;
        }

        .active-stage-banner {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 10px 16px;
            background-color: rgba(88, 166, 255, 0.1);
            border: 1px solid rgba(88, 166, 255, 0.3);
            border-radius: 6px;
            margin-bottom: 16px;
            font-size: 13px;
            color: var(--accent);
        }
        .btn-clear-stage {
            background: none;
            border: 1px solid var(--accent);
            color: var(--accent);
            padding: 3px 10px;
            border-radius: 4px;
            font-size: 12px;
            cursor: pointer;
            font-weight: 500;
            transition: all 0.15s ease;
        }
        .btn-clear-stage:hover {
            background-color: var(--accent);
            color: #0d1117;
        }

        .toolbar {
            display: flex;
            justify-content: space-between;
            align-items: center;
            flex-wrap: wrap;
            gap: 12px;
            margin-bottom: 20px;
        }
        .filter-group {
            display: flex;
            gap: 8px;
        }
        .filter-btn {
            background-color: var(--surface);
            color: var(--text-muted);
            border: 1px solid var(--border);
            padding: 6px 14px;
            border-radius: 6px;
            font-size: 13px;
            cursor: pointer;
            font-weight: 500;
            transition: all 0.15s ease;
        }
        .filter-btn:hover {
            color: var(--text-main);
            border-color: var(--border-light);
        }
        .filter-btn.active {
            background-color: var(--accent);
            color: #ffffff;
            border-color: var(--accent);
        }
        .search-box {
            background-color: #0b0e14;
            color: var(--text-main);
            border: 1px solid var(--border);
            border-radius: 6px;
            padding: 6px 12px;
            font-size: 13px;
            width: 280px;
            outline: none;
        }
        .search-box:focus {
            border-color: var(--accent);
        }
        .btn-toggle-all {
            background: none;
            border: none;
            color: var(--accent);
            font-size: 13px;
            cursor: pointer;
            padding: 4px 8px;
        }
        .btn-toggle-all:hover {
            text-decoration: underline;
        }

        .stage-section {
            background-color: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            margin-bottom: 24px;
            overflow: hidden;
            transition: border-color 0.15s ease;
        }
        .stage-section:hover {
            border-color: var(--border-light);
        }
        .stage-section-header {
            padding: 14px 20px;
            background-color: #161b22;
            border-bottom: 1px solid var(--border);
            display: flex;
            justify-content: space-between;
            align-items: center;
            flex-wrap: wrap;
            gap: 12px;
            cursor: pointer;
            user-select: none;
        }
        .stage-section-header:hover {
            background-color: var(--surface-hover);
        }
        .stage-section-title-group {
            display: flex;
            align-items: center;
            gap: 12px;
            flex-wrap: wrap;
        }
        .stage-section-icon {
            font-size: 12px;
            color: var(--text-muted);
            transition: transform 0.2s ease;
        }
        .stage-section.collapsed .stage-section-icon {
            transform: rotate(-90deg);
        }
        .stage-name-heading {
            font-size: 16px;
            font-weight: 600;
            color: var(--text-main);
            display: flex;
            align-items: center;
            gap: 8px;
        }
        .stage-stats-badge {
            font-size: 13px;
            color: var(--text-muted);
            font-family: var(--font-mono);
        }
        .stage-section-actions {
            display: flex;
            align-items: center;
            gap: 8px;
        }
        .stage-action-btn {
            background-color: #21262d;
            border: 1px solid var(--border);
            color: var(--text-muted);
            padding: 4px 10px;
            border-radius: 4px;
            font-size: 12px;
            cursor: pointer;
            font-weight: 500;
            transition: all 0.15s ease;
        }
        .stage-action-btn:hover {
            color: var(--text-main);
            border-color: var(--border-light);
            background-color: var(--surface-hover);
        }
        .stage-jobs-content {
            padding: 16px;
            background-color: #0b0e14;
        }
        .stage-section.collapsed .stage-jobs-content {
            display: none;
        }
        .stage-empty-msg {
            padding: 24px;
            text-align: center;
            color: var(--text-muted);
            font-size: 13px;
            font-style: italic;
            background-color: #0b0e14;
        }
        .job-card {
            background-color: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            margin-bottom: 12px;
            overflow: hidden;
            transition: border-color 0.15s ease;
        }
        .job-card:hover {
            border-color: var(--border-light);
        }
        .job-header {
            padding: 14px 20px;
            display: flex;
            justify-content: space-between;
            align-items: center;
            cursor: pointer;
            user-select: none;
        }
        .job-header:hover {
            background-color: var(--surface-hover);
        }
        .job-title-group {
            display: flex;
            align-items: center;
            gap: 12px;
            flex-wrap: wrap;
        }
        .status-pill {
            font-size: 11px;
            font-weight: 700;
            padding: 3px 10px;
            border-radius: 12px;
            text-transform: uppercase;
            letter-spacing: 0.3px;
        }
        .status-passed { background: var(--pass-bg); color: var(--pass-text); border: 1px solid var(--pass-border); }
        .status-failed { background: var(--fail-bg); color: var(--fail-text); border: 1px solid var(--fail-border); }
        .status-skipped { background: var(--skip-bg); color: var(--skip-text); border: 1px solid var(--skip-border); }
        .stage-badge {
            font-size: 12px;
            background-color: #21262d;
            color: var(--text-muted);
            padding: 2px 8px;
            border-radius: 4px;
            font-family: var(--font-mono);
        }
        .job-id-badge {
            font-size: 12px;
            color: var(--accent);
            font-family: var(--font-mono);
            font-weight: 600;
        }
        .job-name {
            font-size: 14px;
            font-weight: 600;
        }
        .job-meta-right {
            display: flex;
            align-items: center;
            gap: 16px;
            font-size: 13px;
            color: var(--text-muted);
            font-family: var(--font-mono);
        }
        .job-report-link {
            display: inline-flex;
            align-items: center;
            gap: 5px;
            padding: 3px 8px;
            background: rgba(38, 139, 210, 0.15);
            border: 1px solid rgba(38, 139, 210, 0.45);
            border-radius: 4px;
            color: var(--accent);
            font-size: 11px;
            font-weight: 600;
            text-decoration: none;
            transition: all 0.15s ease;
        }
        .job-report-link:hover {
            background: var(--accent);
            color: #ffffff;
            text-decoration: none;
            box-shadow: 0 0 6px rgba(38, 139, 210, 0.5);
        }
        .job-report-btn {
            display: inline-flex;
            align-items: center;
            gap: 6px;
            padding: 6px 14px;
            background: rgba(38, 139, 210, 0.15);
            border: 1px solid rgba(38, 139, 210, 0.45);
            border-radius: 5px;
            color: var(--accent);
            font-size: 13px;
            font-weight: 600;
            text-decoration: none;
            transition: all 0.15s ease;
        }
        .job-report-btn:hover {
            background: var(--accent);
            color: #ffffff;
            text-decoration: none;
        }
        .chevron {
            transition: transform 0.2s ease;
            font-size: 12px;
        }
        .job-card.open .chevron {
            transform: rotate(180deg);
        }
        .job-body {
            display: none;
            padding: 20px;
            border-top: 1px solid var(--border);
            background-color: #0b0e14;
        }
        .job-card.open .job-body {
            display: block;
        }
        .detail-row {
            margin-bottom: 14px;
        }
        .detail-label {
            font-size: 12px;
            text-transform: uppercase;
            color: var(--text-muted);
            font-weight: 600;
            margin-bottom: 4px;
        }
        .code-box {
            background-color: #040608;
            border: 1px solid var(--border);
            border-radius: 6px;
            padding: 12px;
            font-family: var(--font-mono);
            font-size: 13px;
            color: #d1d5da;
            overflow-x: auto;
            white-space: pre-wrap;
            word-break: break-all;
        }
        .metrics-grid {
            display: grid;
            grid-template-columns: repeat(auto-fill, minmax(220px, 1fr));
            gap: 8px;
            margin-top: 6px;
        }
        .metric-item {
            background-color: #161b22;
            border: 1px solid var(--border);
            border-radius: 6px;
            padding: 8px 12px;
            display: flex;
            justify-content: space-between;
            font-size: 13px;
        }
        .metric-key { color: var(--text-muted); }
        .metric-val { color: var(--text-main); font-family: var(--font-mono); font-weight: 600; }
        .log-box {
            background-color: #040608;
            border: 1px solid var(--border);
            border-radius: 6px;
            padding: 12px;
            font-family: var(--font-mono);
            font-size: 12px;
            line-height: 1.45;
            color: #e6edf3;
            max-height: 350px;
            overflow-y: auto;
            white-space: pre-wrap;
            word-break: break-all;
        }
        .log-box.stderr {
            color: #ff7b72;
            border-color: rgba(248, 81, 73, 0.4);
        }
        footer {
            margin-top: 32px;
            text-align: center;
            font-size: 12px;
            color: var(--text-muted);
        }
    </style>
</head>
<body>
<div class="container">
    <div class="header">
        <div class="header-title">
            <h1>
                Condor Pipeline Report: )HTML" << escape_html(config.name) << R"HTML(
            </h1>
            <div class="header-meta">
                <span><strong>Run Directory:</strong> )HTML" << escape_html(workspace.run_dir().string()) << R"HTML(</span>
                <span><strong>Generated:</strong> )HTML" << time_buf << R"HTML(</span>
                <span><strong>Concurrency:</strong> )HTML" << config.settings.max_parallel_jobs << R"HTML( worker slots</span>
                <span><strong>Path Style:</strong> )HTML" << to_string(config.settings.path_style) << R"HTML(</span>
            </div>
        </div>
        <div>
            <span class="verdict-badge )HTML" << (success ? "verdict-pass" : "verdict-fail") << R"HTML(">
                )HTML" << (success ? "Passed" : "Failed") << R"HTML(
            </span>
        </div>
    </div>

    <div class="stats-grid">
        <div class="stat-card">
            <div class="stat-label">Total Jobs</div>
            <div class="stat-value">)HTML" << stats.total_jobs << R"HTML(</div>
        </div>
        <div class="stat-card">
            <div class="stat-label">Passed</div>
            <div class="stat-value" style="color: var(--pass-text);">)HTML" << stats.passed_jobs << R"HTML(</div>
        </div>
        <div class="stat-card">
            <div class="stat-label">Failed</div>
            <div class="stat-value" style="color: var(--fail-text);">)HTML" << stats.failed_jobs << R"HTML(</div>
        </div>
        <div class="stat-card">
            <div class="stat-label">Duration</div>
            <div class="stat-value">)HTML" << ProgressDisplay::format_duration(stats.elapsed_sec) << R"HTML(</div>
        </div>
    </div>

    <div class="stages-overview">
        <div class="section-title">Stage Progression</div>
        <div class="stage-bars">
)HTML";

    for (const auto& stage : stages) {
        size_t stage_total = stage.job_ids.size();
        size_t stage_passed = 0;
        size_t stage_failed = 0;
        size_t stage_skipped = 0;

        for (const auto& jid : stage.job_ids) {
            auto it = jobs.find(jid);
            if (it != jobs.end()) {
                if (it->second.status == JobStatus::PASSED) stage_passed++;
                else if (it->second.status == JobStatus::FAILED || it->second.status == JobStatus::ABORTED) stage_failed++;
                else if (it->second.status == JobStatus::SKIPPED) stage_skipped++;
            }
        }

        bool all_skipped = (stage.skipped || (stage_skipped == stage_total && stage_total > 0));
        int pct = 100;
        std::string fill_cls = "fill-pass";
        std::string progress_text;

        if (all_skipped) {
            fill_cls = "fill-skip";
            pct = 100;
            progress_text = std::to_string(stage_total) + "/" + std::to_string(stage_total) + " skipped";
        } else if (stage_failed > 0) {
            fill_cls = "fill-fail";
            pct = stage_total > 0 ? static_cast<int>(((stage_passed + stage_failed) * 100) / stage_total) : 100;
            progress_text = std::to_string(stage_passed) + "/" + std::to_string(stage_total) + " passed (" + std::to_string(stage_failed) + " failed)";
        } else {
            pct = stage_total > 0 ? static_cast<int>((stage_passed * 100) / stage_total) : 100;
            if (stage_skipped > 0) {
                progress_text = std::to_string(stage_passed) + "/" + std::to_string(stage_total) + " passed (" + std::to_string(stage_skipped) + " skipped)";
            } else {
                progress_text = std::to_string(stage_passed) + "/" + std::to_string(stage_total) + " passed (" + std::to_string(pct) + "%)";
            }
        }

        h << R"HTML(            <div class="stage-bar-item" onclick="filterByStage(')HTML" << escape_html(stage.config.name)
          << R"HTML(')" title="Click to view stage ')HTML" << escape_html(stage.config.name) << R"HTML('">
                <span style="font-weight: 600;">)HTML" << escape_html(stage.config.name) << R"HTML(</span>
                <div class="progress-track">
                    <div class="progress-fill )HTML" << fill_cls << R"HTML(" style="width: )HTML" << pct << R"HTML(%;"></div>
                </div>
                <span style="text-align: right; color: var(--text-muted); font-size: 13px;">
                    )HTML" << progress_text << R"HTML(
                </span>
                <span class="stage-bar-arrow">&#8594;</span>
            </div>
)HTML";
    }

    h << R"HTML(        </div>
    </div>

    <div class="stage-nav-bar">
        <span class="stage-nav-label">Stage View:</span>
        <div class="stage-nav-group">
            <button class="stage-tab-btn active" data-stage="all" onclick="filterByStage('all')">
                All Stages <span class="stage-tab-count">)HTML" << stats.total_jobs << R"HTML(</span>
            </button>
)HTML";

    for (const auto& stage : stages) {
        h << R"HTML(            <button class="stage-tab-btn" data-stage=")HTML" << escape_html(stage.config.name)
          << R"HTML(" onclick="filterByStage(')HTML" << escape_html(stage.config.name) << R"HTML(')">
                )HTML" << escape_html(stage.config.name)
          << R"HTML( <span class="stage-tab-count">)HTML" << stage.job_ids.size() << R"HTML(</span>
            </button>
)HTML";
    }

    h << R"HTML(        </div>
    </div>

    <div id="activeStageBanner" class="active-stage-banner" style="display: none;">
        <span>Viewing Stage: <strong id="activeStageName"></strong></span>
        <button class="btn-clear-stage" onclick="filterByStage('all')">&times; Show All Stages</button>
    </div>

    <div class="toolbar">
        <div class="filter-group">
            <button class="filter-btn active" data-filter="all">All ()HTML" << stats.total_jobs << R"HTML()</button>
            <button class="filter-btn" data-filter="passed">Passed ()HTML" << stats.passed_jobs << R"HTML()</button>
            <button class="filter-btn" data-filter="failed">Failed ()HTML" << stats.failed_jobs << R"HTML()</button>
            <button class="filter-btn" data-filter="skipped">Skipped ()HTML" << stats.skipped_jobs << R"HTML()</button>
        </div>
        <div style="display: flex; gap: 12px; align-items: center;">
            <input type="text" id="jobSearch" class="search-box" placeholder="Filter jobs by name, ID, or stage...">
            <button id="toggleAllBtn" class="btn-toggle-all">Expand All</button>
        </div>
    </div>

    <div class="jobs-list" id="jobsListContainer">
)HTML";

    for (const auto& stage : stages) {
        size_t stage_total = stage.job_ids.size();
        size_t stage_passed = 0;
        size_t stage_failed = 0;
        size_t stage_skipped = 0;
        double stage_elapsed = 0.0;

        for (const auto& jid : stage.job_ids) {
            auto it = jobs.find(jid);
            if (it != jobs.end()) {
                const auto& j = it->second;
                if (j.status == JobStatus::PASSED) stage_passed++;
                else if (j.status == JobStatus::FAILED || j.status == JobStatus::ABORTED) stage_failed++;
                else if (j.status == JobStatus::SKIPPED) stage_skipped++;
                stage_elapsed += j.elapsed_sec;
            }
        }

        std::string stage_st_str = "Passed";
        std::string stage_st_cls = "status-passed";
        if (stage_failed > 0) {
            stage_st_str = "Failed";
            stage_st_cls = "status-failed";
        } else if (stage.skipped || (stage_skipped == stage_total && stage_total > 0)) {
            stage_st_str = "Skipped";
            stage_st_cls = "status-skipped";
        }

        std::ostringstream time_oss;
        if (stage_elapsed < 60.0) {
            time_oss << std::fixed << std::setprecision(3) << stage_elapsed << "s";
        } else {
            time_oss << ProgressDisplay::format_duration(stage_elapsed);
        }

        std::string stage_summary_txt;
        if (stage.skipped || (stage_skipped == stage_total && stage_total > 0)) {
            stage_summary_txt = std::to_string(stage_total) + "/" + std::to_string(stage_total) + " skipped";
        } else if (stage_skipped > 0) {
            stage_summary_txt = std::to_string(stage_passed) + "/" + std::to_string(stage_total) + " passed (" + std::to_string(stage_skipped) + " skipped)";
        } else if (stage_failed > 0) {
            stage_summary_txt = std::to_string(stage_passed) + "/" + std::to_string(stage_total) + " passed (" + std::to_string(stage_failed) + " failed)";
        } else {
            stage_summary_txt = std::to_string(stage_passed) + "/" + std::to_string(stage_total) + " passed";
        }

        h << R"HTML(        <div class="stage-section" id="stage-sec-)HTML" << escape_html(stage.config.name)
          << R"HTML(" data-stage=")HTML" << escape_html(stage.config.name) << R"HTML(">
            <div class="stage-section-header" onclick="toggleStageFromHeader(this, event)">
                <div class="stage-section-title-group">
                    <span class="stage-section-icon">&#9660;</span>
                    <span class="stage-name-heading">Stage: )HTML" << escape_html(stage.config.name) << R"HTML(</span>
                    <span class="status-pill )HTML" << stage_st_cls << R"HTML(">)HTML" << stage_st_str << R"HTML(</span>
                    <span class="stage-stats-badge">)HTML" << stage_summary_txt
          << R"HTML( &bull; )HTML" << time_oss.str() << R"HTML(</span>
                </div>
                <div class="stage-section-actions">
                    <button class="stage-action-btn" onclick="filterByStage(')HTML" << escape_html(stage.config.name)
          << R"HTML('); event.stopPropagation();">Only This Stage</button>
                    <button class="stage-action-btn btn-stage-collapse" onclick="toggleStageCollapse(')HTML"
          << escape_html(stage.config.name) << R"HTML(', event)">Collapse</button>
                </div>
            </div>
            <div class="stage-jobs-content">
)HTML";

        auto stage_dir = workspace.stage_dir(stage.config.name);
        std::string stage_pre_txt = read_file_preview(stage_dir / "pre_hook.log");
        std::string stage_post_txt = read_file_preview(stage_dir / "post_hook.log");
        if (!stage_pre_txt.empty() || !stage_post_txt.empty()) {
            h << R"HTML(                <div class="stage-hooks-bar" style="margin: 0 0 16px 0; padding: 12px 16px; background: var(--bg-surface); border: 1px solid var(--border); border-radius: 6px;">
                    <div style="font-weight: 600; font-size: 13px; margin-bottom: 8px; color: var(--text-heading);">&#9881; Stage Lifecycle Hooks</div>
)HTML";
            if (!stage_pre_txt.empty()) {
                h << R"HTML(                    <details style="margin-bottom: 8px;">
                        <summary style="cursor: pointer; font-size: 12px; font-weight: 500; color: var(--accent); user-select: none;">&#9656; Stage Pre-Hook Output (pre_hook.log)</summary>
                        <pre class="log-box" style="margin-top: 6px; max-height: 250px;">)HTML" << escape_html(stage_pre_txt) << R"HTML(</pre>
                    </details>
)HTML";
            }
            if (!stage_post_txt.empty()) {
                h << R"HTML(                    <details>
                        <summary style="cursor: pointer; font-size: 12px; font-weight: 500; color: var(--accent); user-select: none;">&#9656; Stage Post-Hook Output (post_hook.log)</summary>
                        <pre class="log-box" style="margin-top: 6px; max-height: 250px;">)HTML" << escape_html(stage_post_txt) << R"HTML(</pre>
                    </details>
)HTML";
            }
            h << R"HTML(                </div>
)HTML";
        }

        for (const auto& jid : stage.job_ids) {
            auto it = jobs.find(jid);
            if (it == jobs.end()) continue;
            const auto& job = it->second;

            std::string st_str = to_string(job.status);
            std::string st_cls = "status-passed";
            std::string filter_tag = "passed";
            if (job.status == JobStatus::FAILED || job.status == JobStatus::ABORTED) {
                st_cls = "status-failed";
                filter_tag = "failed";
            } else if (job.status == JobStatus::SKIPPED) {
                st_cls = "status-skipped";
                filter_tag = "skipped";
            }

            auto job_dir = workspace.job_dir(job.config.stage_name, job.config.job_id);
            auto metrics = extract_yaml_metrics(job_dir);
            auto html_reports = discover_html_reports(job_dir, metrics);
            std::string pre_hook_txt = read_file_preview(job_dir / "pre_hook.log");
            std::string post_hook_txt = read_file_preview(job_dir / "post_hook.log");
            std::string stdout_txt = read_file_preview(job_dir / "stdout.log");
            std::string stderr_txt = read_file_preview(job_dir / "stderr.err");

            std::string display_job_dir;
            if (config.settings.path_style == PathStyle::RELATIVE) {
                std::error_code ec;
                display_job_dir = std::filesystem::relative(job_dir, workspace.run_dir(), ec).string();
                if (display_job_dir.empty()) display_job_dir = ".";
            } else {
                display_job_dir = job_dir.string();
            }

            h << R"HTML(                <div class="job-card" data-status=")HTML" << filter_tag
              << R"HTML(" data-stage=")HTML" << escape_html(job.config.stage_name)
              << R"HTML(" data-search=")HTML" << escape_html(job.config.job_id + " " + job.config.stage_name + " " + job.config.name + " " + job.config.command)
              << R"HTML(">
                    <div class="job-header" onclick="toggleJob(this.parentElement)">
                        <div class="job-title-group">
                            <span class="status-pill )HTML" << st_cls << R"HTML(">)HTML" << st_str << R"HTML(</span>
                            <span class="job-id-badge">#)HTML" << escape_html(job.config.job_id) << R"HTML(</span>
                            <span class="stage-badge">)HTML" << escape_html(job.config.stage_name) << R"HTML(</span>
                            <span class="job-name">)HTML" << escape_html(job.config.name) << R"HTML(</span>
                        </div>
                        <div class="job-meta-right">
)HTML";
            if (!html_reports.empty()) {
                std::string primary = html_reports.front();
                std::string r_href;
                if (primary.find("://") != std::string::npos || primary.front() == '/') {
                    r_href = primary;
                } else {
                    r_href = (std::filesystem::path(job.config.stage_name) / job.config.job_id / primary).string();
                }
                h << R"HTML(                            <a href=")HTML" << escape_html(r_href)
                  << R"HTML(" target="_blank" class="job-report-link" onclick="event.stopPropagation();" title="Open Job HTML Report (")HTML"
                  << escape_html(primary) << R"HTML(")">&#128196; HTML Report &#8599;</a>
)HTML";
            }
            h << R"HTML(                            <span>Exit: )HTML" << job.exit_code << R"HTML(</span>
                            <span>)HTML" << std::fixed << std::setprecision(3) << job.elapsed_sec << R"HTML(s</span>
                            <span class="chevron">&#9660;</span>
                        </div>
                    </div>
                    <div class="job-body">
                        <div class="detail-row">
                            <div class="detail-label">Command</div>
                            <div class="code-box">)HTML" << escape_html(job.config.command) << R"HTML(</div>
                        </div>
                        <div class="detail-row">
                            <div class="detail-label">Job Directory</div>
                            <div class="code-box" style="font-size: 12px; color: var(--text-muted);">)HTML"
              << escape_html(display_job_dir) << R"HTML(</div>
                        </div>
)HTML";

            if (!html_reports.empty()) {
                h << R"HTML(                        <div class="detail-row">
                            <div class="detail-label">Generated HTML Reports</div>
                            <div style="display: flex; gap: 8px; flex-wrap: wrap;">
)HTML";
                for (const auto& rep : html_reports) {
                    std::string r_href;
                    if (rep.find("://") != std::string::npos || rep.front() == '/') {
                        r_href = rep;
                    } else {
                        r_href = (std::filesystem::path(job.config.stage_name) / job.config.job_id / rep).string();
                    }
                    h << R"HTML(                                <a href=")HTML" << escape_html(r_href)
                      << R"HTML(" target="_blank" class="job-report-btn">&#128196; View )HTML"
                      << escape_html(rep) << R"HTML( &#8599;</a>
)HTML";
                }
                h << R"HTML(                            </div>
                        </div>
)HTML";
            }

            if (!metrics.empty()) {
                h << R"HTML(                        <div class="detail-row">
                            <div class="detail-label">Custom Machine Metrics</div>
                            <div class="metrics-grid">
)HTML";
                for (const auto& [mk, mv] : metrics) {
                    std::string lower_mv = mv;
                    std::transform(lower_mv.begin(), lower_mv.end(), lower_mv.begin(), ::tolower);
                    bool is_html = (lower_mv.size() > 5 && (lower_mv.ends_with(".html") || lower_mv.ends_with(".htm")));
                    h << R"HTML(                                <div class="metric-item"><span class="metric-key">)HTML"
                      << escape_html(mk) << R"HTML(:</span><span class="metric-val">)HTML";
                    if (is_html) {
                        std::string r_href;
                        if (mv.find("://") != std::string::npos || mv.front() == '/') {
                            r_href = mv;
                        } else {
                            r_href = (std::filesystem::path(job.config.stage_name) / job.config.job_id / mv).string();
                        }
                        h << R"HTML(<a href=")HTML" << escape_html(r_href)
                          << R"HTML(" target="_blank" style="color: var(--accent); text-decoration: underline;">)HTML"
                          << escape_html(mv) << R"HTML( &#8599;</a>)HTML";
                    } else {
                        h << escape_html(mv);
                    }
                    h << R"HTML(</span></div>
)HTML";
                }
                h << R"HTML(                            </div>
                        </div>
)HTML";
            }

            if (!pre_hook_txt.empty()) {
                h << R"HTML(                        <div class="detail-row">
                            <div class="detail-label">Pre-Hook Output (pre_hook.log)</div>
                            <pre class="log-box">)HTML" << escape_html(pre_hook_txt) << R"HTML(</pre>
                        </div>
)HTML";
            }

            if (!stdout_txt.empty()) {
                h << R"HTML(                        <div class="detail-row">
                            <div class="detail-label">Standard Output (stdout.log)</div>
                            <pre class="log-box">)HTML" << escape_html(stdout_txt) << R"HTML(</pre>
                        </div>
)HTML";
            }

            if (!stderr_txt.empty()) {
                h << R"HTML(                        <div class="detail-row">
                            <div class="detail-label">Standard Error (stderr.err)</div>
                            <pre class="log-box stderr">)HTML" << escape_html(stderr_txt) << R"HTML(</pre>
                        </div>
)HTML";
            }

            if (!post_hook_txt.empty()) {
                h << R"HTML(                        <div class="detail-row">
                            <div class="detail-label">Post-Hook Output (post_hook.log)</div>
                            <pre class="log-box">)HTML" << escape_html(post_hook_txt) << R"HTML(</pre>
                        </div>
)HTML";
            }

            h << R"HTML(                    </div>
                </div>
)HTML";
        }

        h << R"HTML(            </div>
            <div class="stage-empty-msg" style="display: none;">
                No jobs in stage ")HTML" << escape_html(stage.config.name) << R"HTML(" match the current filters.
            </div>
        </div>
)HTML";
    }

    h << R"HTML(    </div>

    <footer>
        Rendered by Condor Workflow Orchestrator &bull; Standardized offline-first execution report
    </footer>
</div>

<script>
    function toggleJob(card) {
        card.classList.toggle('open');
    }

    let activeFilter = 'all';
    let activeStage = 'all';

    function filterByStage(stageName) {
        activeStage = stageName;

        // Update stage buttons
        document.querySelectorAll('.stage-tab-btn').forEach(btn => {
            if (btn.getAttribute('data-stage') === stageName) {
                btn.classList.add('active');
            } else {
                btn.classList.remove('active');
            }
        });

        // Update active stage banner
        const banner = document.getElementById('activeStageBanner');
        const bannerName = document.getElementById('activeStageName');
        if (stageName === 'all') {
            banner.style.display = 'none';
        } else {
            banner.style.display = 'flex';
            bannerName.textContent = stageName;
        }

        // Update URL hash
        if (window.history && window.history.replaceState) {
            const newHash = (stageName === 'all') ? window.location.pathname + window.location.search : '#stage=' + encodeURIComponent(stageName);
            window.history.replaceState(null, '', newHash);
        }

        applyFilters();

        if (stageName !== 'all') {
            const sec = document.getElementById('stage-sec-' + stageName);
            if (sec) {
                sec.scrollIntoView({ behavior: 'smooth', block: 'start' });
            }
        }
    }

    function toggleStageCollapse(stageName, event) {
        if (event) event.stopPropagation();
        const sec = document.getElementById('stage-sec-' + stageName);
        if (!sec) return;
        sec.classList.toggle('collapsed');
        const btn = sec.querySelector('.btn-stage-collapse');
        if (btn) btn.textContent = sec.classList.contains('collapsed') ? 'Expand' : 'Collapse';
    }

    function toggleStageFromHeader(headerEl, event) {
        if (event.target.closest('.stage-action-btn')) return;
        const sec = headerEl.closest('.stage-section');
        if (!sec) return;
        sec.classList.toggle('collapsed');
        const btn = sec.querySelector('.btn-stage-collapse');
        if (btn) btn.textContent = sec.classList.contains('collapsed') ? 'Expand' : 'Collapse';
    }

    // Status filter buttons
    const filterBtns = document.querySelectorAll('.filter-btn');
    filterBtns.forEach(btn => {
        btn.addEventListener('click', () => {
            filterBtns.forEach(b => b.classList.remove('active'));
            btn.classList.add('active');
            activeFilter = btn.getAttribute('data-filter');
            applyFilters();
        });
    });

    // Search filter
    const searchInput = document.getElementById('jobSearch');
    searchInput.addEventListener('input', applyFilters);

    function applyFilters() {
        const query = searchInput.value.toLowerCase().trim();
        const sections = document.querySelectorAll('.stage-section');

        sections.forEach(sec => {
            const stageName = sec.getAttribute('data-stage');
            const stageMatches = (activeStage === 'all' || stageName === activeStage);

            if (!stageMatches) {
                sec.style.display = 'none';
                return;
            }

            const cards = sec.querySelectorAll('.job-card');
            let matchingInStage = 0;

            cards.forEach(card => {
                const status = card.getAttribute('data-status');
                const searchData = (card.getAttribute('data-search') || '').toLowerCase();
                const matchesStatus = (activeFilter === 'all' || status === activeFilter);
                const matchesSearch = (!query || searchData.includes(query));

                if (matchesStatus && matchesSearch) {
                    card.style.display = 'block';
                    matchingInStage++;
                } else {
                    card.style.display = 'none';
                }
            });

            const emptyMsg = sec.querySelector('.stage-empty-msg');
            if (matchingInStage > 0) {
                sec.style.display = 'block';
                if (emptyMsg) emptyMsg.style.display = 'none';
            } else {
                if (activeStage === stageName) {
                    sec.style.display = 'block';
                    if (emptyMsg) emptyMsg.style.display = 'block';
                } else {
                    sec.style.display = 'none';
                }
            }
        });
    }

    // Toggle All
    let allExpanded = false;
    document.getElementById('toggleAllBtn').addEventListener('click', () => {
        allExpanded = !allExpanded;
        document.querySelectorAll('.job-card').forEach(card => {
            if (allExpanded) card.classList.add('open');
            else card.classList.remove('open');
        });
        if (allExpanded) {
            document.querySelectorAll('.stage-section').forEach(sec => {
                sec.classList.remove('collapsed');
                const btn = sec.querySelector('.btn-stage-collapse');
                if (btn) btn.textContent = 'Collapse';
            });
        }
        document.getElementById('toggleAllBtn').textContent = allExpanded ? 'Collapse All' : 'Expand All';
    });

    // Check URL hash on load
    window.addEventListener('DOMContentLoaded', () => {
        if (window.location.hash.startsWith('#stage=')) {
            const hashStage = decodeURIComponent(window.location.hash.substring(7));
            if (hashStage) {
                filterByStage(hashStage);
            }
        }
    });
</script>
</body>
</html>
)HTML";

    return h.str();
}

bool HtmlReportGenerator::write_report(
    const std::filesystem::path& output_path,
    const PipelineConfig& config,
    const WorkspaceManager& workspace,
    const std::vector<StageRuntimeState>& stages,
    const std::unordered_map<std::string, JobRuntimeState>& jobs,
    const ProgressStats& stats,
    bool success
) {
    try {
        std::string html = generate(config, workspace, stages, jobs, stats, success);
        std::ofstream ofs(output_path, std::ios::trunc);
        if (!ofs) return false;
        ofs << html;
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace condor
