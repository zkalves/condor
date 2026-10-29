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

#include <string_view>

namespace condor::hub {

inline constexpr std::string_view kDashboardHtml = R"rawhtml(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Condor Hub - Fleet Monitor & Remote Control Plane</title>
  <style>
    :root {
      --bg: #0b1120;
      --card-bg: #151f32;
      --card-hover: #1e2c47;
      --border: #22324e;
      --border-subtle: #1a263c;
      --text: #f1f5f9;
      --text-muted: #8494ab;
      --primary: #38bdf8;
      --primary-hover: #0ea5e9;
      --success: #22c55e;
      --success-text: #4ade80;
      --warning: #f59e0b;
      --warning-text: #fcd34d;
      --danger: #ef4444;
      --danger-text: #f87171;
      --purple: #a855f7;
      --purple-text: #c084fc;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      background: var(--bg);
      color: var(--text);
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
      line-height: 1.4;
      padding: 16px 20px;
    }
    header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 16px;
      padding-bottom: 12px;
      border-bottom: 1px solid var(--border);
    }
    .header-left {
      display: flex;
      align-items: center;
      gap: 12px;
    }
    .logo {
      font-size: 20px;
      font-weight: 800;
      color: var(--primary);
      letter-spacing: -0.5px;
    }
    .header-right {
      display: flex;
      align-items: center;
      gap: 12px;
    }
    .badge {
      display: inline-flex;
      align-items: center;
      gap: 5px;
      padding: 2px 7px;
      border-radius: 4px;
      font-size: 11px;
      font-weight: 700;
      text-transform: uppercase;
      white-space: nowrap;
      letter-spacing: 0.02em;
    }
    .badge-live { background: rgba(34, 197, 94, 0.18); color: var(--success-text); border: 1px solid rgba(74, 222, 128, 0.3); }
    .badge-passed { background: rgba(34, 197, 94, 0.18); color: var(--success-text); border: 1px solid rgba(74, 222, 128, 0.25); }
    .badge-failed { background: rgba(239, 68, 68, 0.2); color: var(--danger-text); border: 1px solid rgba(248, 113, 113, 0.3); }
    .badge-aborted { background: rgba(245, 158, 11, 0.18); color: var(--warning-text); border: 1px solid rgba(251, 191, 36, 0.3); }
    .badge-offline { background: rgba(148, 163, 184, 0.15); color: var(--text-muted); border: 1px solid #334155; }
    .badge-disconnected { background: rgba(239, 68, 68, 0.15); color: #fca5a5; border: 1px solid rgba(252, 165, 165, 0.2); }
    .pulse-dot {
      width: 7px;
      height: 7px;
      border-radius: 50%;
      background: currentColor;
      box-shadow: 0 0 6px currentColor;
    }
    .btn {
      padding: 5px 10px;
      border-radius: 4px;
      border: 1px solid transparent;
      font-size: 12px;
      font-weight: 600;
      cursor: pointer;
      display: inline-flex;
      align-items: center;
      gap: 5px;
      transition: all 0.12s ease;
      white-space: nowrap;
      text-decoration: none;
    }
    .btn-xs {
      padding: 2px 6px;
      font-size: 11px;
      border-radius: 3px;
    }
    .btn-danger {
      background: rgba(239, 68, 68, 0.15);
      border-color: rgba(239, 68, 68, 0.4);
      color: #fca5a5;
    }
    .btn-danger:hover { background: #ef4444; color: white; }
    .btn-primary { background: var(--primary); color: #0b1120; }
    .btn-primary:hover { background: var(--primary-hover); }
    .btn-secondary {
      background: #1e293b;
      border-color: var(--border);
      color: var(--text);
    }
    .btn-secondary:hover { background: #2d3b55; }
    .btn-warning {
      background: rgba(245, 158, 11, 0.18);
      border-color: rgba(245, 158, 11, 0.4);
      color: #fcd34d;
    }
    .btn-warning:hover { background: #f59e0b; color: black; }

    /* Compact Metrics Strip */
    .metrics-strip {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(130px, 1fr));
      gap: 10px;
      margin-bottom: 14px;
    }
    .metric-card {
      background: var(--card-bg);
      border: 1px solid var(--border);
      border-radius: 6px;
      padding: 10px 14px;
      display: flex;
      flex-direction: column;
    }
    .metric-title {
      font-size: 11px;
      font-weight: 700;
      color: var(--text-muted);
      text-transform: uppercase;
      letter-spacing: 0.04em;
    }
    .metric-val {
      font-size: 22px;
      font-weight: 800;
      line-height: 1.2;
      margin-top: 2px;
      font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace;
    }

    /* Toolbar & Filter Bar */
    .toolbar {
      display: flex;
      justify-content: space-between;
      align-items: center;
      flex-wrap: wrap;
      gap: 10px;
      margin-bottom: 12px;
    }
    .toolbar-left {
      display: flex;
      align-items: center;
      gap: 8px;
      flex-wrap: wrap;
    }
    .toolbar-right {
      display: flex;
      align-items: center;
      gap: 8px;
    }
    .section-heading {
      font-size: 15px;
      font-weight: 700;
      display: flex;
      align-items: center;
      gap: 8px;
    }
    .count-tag {
      font-size: 11px;
      background: #1e293b;
      color: var(--primary);
      border: 1px solid var(--border);
      padding: 1px 7px;
      border-radius: 10px;
      font-weight: 700;
    }
    .input-compact {
      background: #0f172a;
      border: 1px solid var(--border);
      border-radius: 4px;
      color: var(--text);
      padding: 4px 8px;
      font-size: 12px;
      outline: none;
      transition: border-color 0.15s;
    }
    .input-compact:focus {
      border-color: var(--primary);
    }
    .select-compact {
      background: #0f172a;
      border: 1px solid var(--border);
      border-radius: 4px;
      color: var(--text);
      padding: 4px 8px;
      font-size: 12px;
      outline: none;
      cursor: pointer;
    }
    .view-group {
      display: inline-flex;
      background: #0f172a;
      border: 1px solid var(--border);
      border-radius: 4px;
      padding: 2px;
      gap: 2px;
    }
    .view-btn {
      background: transparent;
      border: none;
      color: var(--text-muted);
      padding: 3px 8px;
      border-radius: 3px;
      font-size: 11px;
      font-weight: 600;
      cursor: pointer;
    }
    .view-btn.active {
      background: var(--card-bg);
      color: var(--primary);
      box-shadow: 0 1px 2px rgba(0,0,0,0.4);
    }

    /* Settings Dropdown / Drawer */
    .settings-panel {
      position: absolute;
      right: 20px;
      top: 140px;
      background: #162032;
      border: 1px solid var(--border);
      border-radius: 6px;
      padding: 14px;
      width: 290px;
      box-shadow: 0 8px 24px rgba(0,0,0,0.5);
      z-index: 50;
      display: none;
    }
    .settings-row {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 10px;
      font-size: 12px;
    }
    .settings-row:last-child { margin-bottom: 0; }
    .settings-title {
      font-size: 13px;
      font-weight: 700;
      margin-bottom: 10px;
      padding-bottom: 6px;
      border-bottom: 1px solid var(--border);
      color: var(--primary);
      display: flex;
      justify-content: space-between;
    }

    /* Compact Table Styles */
    .table-card {
      background: var(--card-bg);
      border: 1px solid var(--border);
      border-radius: 6px;
      overflow-x: auto;
      box-shadow: 0 2px 4px rgba(0, 0, 0, 0.2);
    }
    .compact-table {
      width: 100%;
      border-collapse: collapse;
      text-align: left;
      font-size: 12px;
    }
    .compact-table th {
      background: #101826;
      color: var(--text-muted);
      font-weight: 700;
      font-size: 11px;
      text-transform: uppercase;
      letter-spacing: 0.04em;
      padding: 7px 9px;
      border-bottom: 1px solid var(--border);
      cursor: pointer;
      user-select: none;
      white-space: nowrap;
      transition: background 0.12s, color 0.12s;
    }
    .compact-table th:hover {
      background: #172235;
      color: var(--text);
    }
    .compact-table th .sort-icon {
      display: inline-block;
      margin-left: 3px;
      font-size: 10px;
      opacity: 0.4;
    }
    .compact-table th.active-sort .sort-icon {
      opacity: 1;
      color: var(--primary);
    }
    .compact-table td {
      padding: 6px 9px;
      border-bottom: 1px solid var(--border-subtle);
      vertical-align: middle;
      white-space: nowrap;
    }
    /* Comfortable Density Mode Override */
    .density-comfortable td {
      padding: 10px 12px;
    }
    .density-comfortable th {
      padding: 9px 12px;
    }

    .compact-table tbody tr {
      transition: background 0.1s ease;
    }
    .compact-table tbody tr:hover {
      background: var(--card-hover);
    }
    .compact-table tbody tr.row-expanded {
      background: #101826;
    }

    /* Column Specific Formatting */
    .col-num {
      text-align: right;
      font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace;
      font-weight: 600;
    }
    .num-passed { color: var(--success-text); }
    .num-failed { color: var(--danger-text); font-weight: 700; }
    .num-running { color: var(--primary); }
    .num-waiting { color: var(--warning-text); }
    .num-other { color: var(--purple-text); }
    .num-skipped { color: var(--text-muted); }
    .num-total { color: var(--text); font-weight: 700; }

    .run-name {
      font-weight: 700;
      color: var(--primary);
      display: flex;
      align-items: center;
      gap: 6px;
    }
    .run-id-sub {
      font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace;
      font-size: 10px;
      color: var(--text-muted);
      background: #0b1120;
      padding: 1px 4px;
      border-radius: 3px;
    }
    .owner-pill {
      display: inline-flex;
      align-items: center;
      gap: 4px;
      font-size: 11px;
      color: #cbd5e1;
      background: #0f172a;
      border: 1px solid var(--border);
      padding: 1px 6px;
      border-radius: 3px;
    }
    .time-mono {
      font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace;
      font-size: 11px;
      color: #cbd5e1;
    }
    .btn-slots {
      background: #0f172a;
      border: 1px solid var(--border);
      color: var(--primary);
      padding: 2px 6px;
      border-radius: 3px;
      font-size: 11px;
      font-weight: 600;
      cursor: pointer;
      display: inline-flex;
      align-items: center;
      gap: 3px;
    }
    .btn-slots:hover {
      background: #1e293b;
    }

    /* In-table column filter row */
    .filter-row th {
      background: #0c131f;
      padding: 4px 6px;
      border-bottom: 1px solid var(--border);
    }
    .th-filter-input {
      width: 100%;
      background: #060b13;
      border: 1px solid #1a273e;
      border-radius: 3px;
      color: var(--text);
      font-size: 11px;
      padding: 2px 4px;
      outline: none;
    }
    .th-filter-input:focus { border-color: var(--primary); }

    /* Expandable Sub-Row (Worker Details) */
    .details-row {
      background: #090e18 !important;
    }
    .details-row td {
      padding: 8px 12px;
      border-bottom: 2px solid var(--border);
    }
    .worker-box {
      background: #0c1424;
      border-left: 3px solid var(--primary);
      padding: 8px 12px;
      border-radius: 0 4px 4px 0;
    }
    .worker-title {
      font-size: 11px;
      font-weight: 700;
      text-transform: uppercase;
      color: var(--text-muted);
      margin-bottom: 6px;
      display: flex;
      justify-content: space-between;
    }
    .worker-grid {
      display: grid;
      grid-template-columns: repeat(auto-fill, minmax(260px, 1fr));
      gap: 6px;
    }
    .worker-item {
      background: #131c2e;
      border: 1px solid #1c2a42;
      border-radius: 4px;
      padding: 6px 8px;
      display: flex;
      justify-content: space-between;
      align-items: center;
      font-size: 11px;
    }

    /* Cards View Alternative */
    .fleet-grid {
      display: grid;
      grid-template-columns: 1fr;
      gap: 12px;
    }
    .run-card {
      background: var(--card-bg);
      border: 1px solid var(--border);
      border-radius: 6px;
      padding: 16px;
      display: flex;
      flex-direction: column;
      gap: 12px;
    }
    .run-card:hover { border-color: #3b82f6; }
    .run-card-header {
      display: flex;
      justify-content: space-between;
      align-items: flex-start;
      flex-wrap: wrap;
      gap: 10px;
    }
    .progress-bar-wrap {
      height: 8px;
      background: #0f172a;
      border-radius: 4px;
      overflow: hidden;
      display: flex;
    }
    .prog-passed { background: var(--success); }
    .prog-running { background: var(--primary); }
    .prog-failed { background: var(--danger); }
    .prog-skipped { background: #64748b; }
    .empty-state {
      text-align: center;
      padding: 36px 16px;
      color: var(--text-muted);
      font-size: 13px;
    }
  </style>
</head>
<body>

  <header>
    <div class="header-left">
      <div class="logo">CONDOR HUB</div>
      <span class="badge" id="conn-badge">
        <span class="pulse-dot"></span>
        <span id="conn-text">Connecting...</span>
      </span>
    </div>
    <div class="header-right">
      <button class="btn btn-secondary" onclick="toggleSettings()">⚙️ Settings</button>
      <button class="btn btn-danger" onclick="triggerEmergencyStop()">🚨 Stop All</button>
    </div>
  </header>

  <!-- Settings Popup Modal -->
  <div id="settings-menu" class="settings-panel">
    <div class="settings-title">
      <span>Dashboard Settings</span>
      <span style="cursor:pointer;" onclick="toggleSettings()">✕</span>
    </div>
    <div class="settings-row">
      <span>Table Density:</span>
      <select id="setting-density" class="select-compact" onchange="changeDensity(this.value)">
        <option value="compact">Compact (Dense)</option>
        <option value="comfortable">Comfortable</option>
      </select>
    </div>
    <div class="settings-row">
      <span>Group By:</span>
      <select id="setting-group" class="select-compact" onchange="changeGrouping(this.value)">
        <option value="flat">All Runs (Flat)</option>
        <option value="pipeline">Group by Pipeline</option>
      </select>
    </div>
    <div class="settings-row">
      <span>Auto Refresh:</span>
      <select id="setting-refresh" class="select-compact" onchange="changeRefreshRate(this.value)">
        <option value="live">Live (WebSocket)</option>
        <option value="2000">Every 2s</option>
        <option value="5000">Every 5s</option>
        <option value="paused">Paused</option>
      </select>
    </div>
    <div class="settings-row">
      <span>In-Table Filters:</span>
      <label style="cursor:pointer;">
        <input type="checkbox" id="setting-filter-row" onchange="toggleFilterRow(this.checked)" checked /> Show Row
      </label>
    </div>
  </div>

  <!-- Metric Strip -->
  <div class="metrics-strip">
    <div class="metric-card">
      <div class="metric-title">Active Runners</div>
      <div class="metric-val" id="stat-active-runners">0</div>
    </div>
    <div class="metric-card">
      <div class="metric-title">Running Jobs</div>
      <div class="metric-val num-running" id="stat-running-jobs">0</div>
    </div>
    <div class="metric-card">
      <div class="metric-title">Waiting Jobs</div>
      <div class="metric-val num-waiting" id="stat-waiting-jobs">0</div>
    </div>
    <div class="metric-card">
      <div class="metric-title">Passed Jobs</div>
      <div class="metric-val num-passed" id="stat-completed-jobs">0</div>
    </div>
    <div class="metric-card">
      <div class="metric-title">Failed Jobs</div>
      <div class="metric-val num-failed" id="stat-failed-jobs">0</div>
    </div>
  </div>

  <!-- Toolbar with Global & Column Filter Controls -->
  <div class="toolbar">
    <div class="toolbar-left">
      <div class="section-heading">
        <span>Fleet Executions</span>
        <span class="count-tag" id="runs-count">0</span>
      </div>
      <input type="text" id="search-box" class="input-compact" style="width: 220px;" placeholder="🔍 Search pipeline, run, owner..." oninput="handleSearch(this.value)" />
      
      <select id="status-select" class="select-compact" onchange="handleStatusFilter(this.value)">
        <option value="ALL">All Statuses</option>
        <option value="RUNNING">Running</option>
        <option value="PAUSED">Paused</option>
        <option value="PASSED">Passed</option>
        <option value="FAILED">Failed</option>
        <option value="ABORTED">Aborted</option>
        <option value="DISCONNECTED">Disconnected</option>
      </select>

      <select id="owner-select" class="select-compact" onchange="handleOwnerFilter(this.value)">
        <option value="ALL">All Owners</option>
      </select>
    </div>

    <div class="toolbar-right">
      <div class="view-group">
        <button id="view-table-btn" class="view-btn active" onclick="switchView('table')">📊 Table</button>
        <button id="view-cards-btn" class="view-btn" onclick="switchView('cards')">🗂️ Cards</button>
      </div>
      <button class="btn btn-secondary btn-xs" onclick="resetFilters()">Clear Filters</button>
      <button class="btn btn-secondary btn-xs" onclick="fetchRuns()">🔄 Refresh</button>
    </div>
  </div>

  <!-- Compact Dynamic Table Container -->
  <div id="table-view-container" class="table-card">
    <table class="compact-table" id="main-table">
      <thead>
        <tr>
          <th onclick="handleSort('status')" id="th-status" style="width: 100px;">Status <span class="sort-icon">⇅</span></th>
          <th onclick="handleSort('name')" id="th-name">Name <span class="sort-icon">⇅</span></th>
          <th onclick="handleSort('total_runs')" id="th-total_runs" class="col-num" style="width: 85px;">Total Runs <span class="sort-icon">⇅</span></th>
          <th onclick="handleSort('passed')" id="th-passed" class="col-num" style="width: 70px;">Passed <span class="sort-icon">⇅</span></th>
          <th onclick="handleSort('failed')" id="th-failed" class="col-num" style="width: 70px;">Failed <span class="sort-icon">⇅</span></th>
          <th onclick="handleSort('running')" id="th-running" class="col-num" style="width: 75px;">Running <span class="sort-icon">⇅</span></th>
          <th onclick="handleSort('waiting')" id="th-waiting" class="col-num" style="width: 75px;">Waiting <span class="sort-icon">⇅</span></th>
          <th onclick="handleSort('other')" id="th-other" class="col-num" style="width: 65px;">Other <span class="sort-icon">⇅</span></th>
          <th onclick="handleSort('skipped')" id="th-skipped" class="col-num" style="width: 70px;">Skipped <span class="sort-icon">⇅</span></th>
          <th onclick="handleSort('start_time')" id="th-start_time" style="width: 145px;">Start Time <span class="sort-icon">⇅</span></th>
          <th onclick="handleSort('owner')" id="th-owner" style="width: 110px;">Owner <span class="sort-icon">⇅</span></th>
          <th style="width: 110px; text-align: right;">Actions</th>
        </tr>
        <!-- In-Table Column Filter Row -->
        <tr class="filter-row" id="column-filters-row">
          <th>
            <select class="th-filter-input" onchange="handleStatusFilter(this.value)" id="th-filter-status">
              <option value="ALL">All</option>
              <option value="RUNNING">RUNNING</option>
              <option value="PASSED">PASSED</option>
              <option value="FAILED">FAILED</option>
              <option value="PAUSED">PAUSED</option>
              <option value="ABORTED">ABORTED</option>
            </select>
          </th>
          <th><input type="text" class="th-filter-input" placeholder="Filter name..." oninput="handleColumnFilter('name', this.value)" id="th-filter-name" /></th>
          <th colspan="7" style="color: #64748b; font-size: 10px; text-align: center; font-weight: normal;">job counts & status breakdown</th>
          <th><input type="text" class="th-filter-input" placeholder="Filter date..." oninput="handleColumnFilter('start_time', this.value)" id="th-filter-start_time" /></th>
          <th><input type="text" class="th-filter-input" placeholder="Filter owner..." oninput="handleColumnFilter('owner', this.value)" id="th-filter-owner" /></th>
          <th style="text-align: right;"><button class="btn btn-secondary btn-xs" onclick="resetFilters()">Reset</button></th>
        </tr>
      </thead>
      <tbody id="runs-table-body">
        <tr>
          <td colspan="12" class="empty-state">No pipeline runs registered yet. Launch Condor with <code>--server-url ws://&lt;host&gt;:8080/api/v1/ws</code></td>
        </tr>
      </tbody>
    </table>
  </div>

  <!-- Cards View Container (Alternative) -->
  <div id="cards-view-container" class="fleet-grid" style="display: none;">
    <div class="empty-state">No pipeline runs registered yet. Launch Condor with <code>--server-url ws://&lt;host&gt;:8080/api/v1/ws</code></div>
  </div>

  <script>
    let ws = null;
    let allRuns = [];
    let currentView = localStorage.getItem('condor_hub_view') || 'table';
    let currentDensity = localStorage.getItem('condor_hub_density') || 'compact';
    let currentGrouping = localStorage.getItem('condor_hub_grouping') || 'flat';
    let refreshRate = localStorage.getItem('condor_hub_refresh') || 'live';
    let showFilterRow = localStorage.getItem('condor_hub_filter_row') !== 'false';
    let refreshTimer = null;

    let sortField = 'start_time';
    let sortAsc = false;
    let searchQuery = '';
    let statusFilter = 'ALL';
    let ownerFilter = 'ALL';
    let colFilters = { name: '', start_time: '', owner: '' };
    let expandedRows = new Set();

    function parseRunIdTime(runId) {
      if (!runId || runId.length < 15 || runId[8] !== '_') return 0;
      const y = parseInt(runId.substring(0, 4), 10);
      const m = parseInt(runId.substring(4, 6), 10) - 1;
      const d = parseInt(runId.substring(6, 8), 10);
      const hh = parseInt(runId.substring(9, 11), 10);
      const mm = parseInt(runId.substring(11, 13), 10);
      const ss = parseInt(runId.substring(13, 15), 10);
      return new Date(y, m, d, hh, mm, ss).getTime();
    }

    function formatStartTime(ms, str, runId) {
      if (str && str.length > 0) return str;
      let timeMs = (ms && ms > 0) ? ms : parseRunIdTime(runId);
      if (timeMs > 0) {
        const d = new Date(timeMs);
        const pad = n => String(n).padStart(2, '0');
        return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())} ${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`;
      }
      return '-';
    }

    function connectWebSocket() {
      const proto = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
      const wsUrl = `${proto}//${window.location.host}/ws/dashboard`;

      ws = new WebSocket(wsUrl);

      ws.onopen = () => {
        const badge = document.getElementById('conn-badge');
        badge.className = 'badge badge-live';
        document.getElementById('conn-text').textContent = 'Live Connected';
        fetchRuns();
      };

      ws.onmessage = (event) => {
        try {
          const msg = JSON.parse(event.data);
          if (msg.type === 'fleet_update') {
            updateRuns(msg.runs);
          } else if (msg.type === 'telemetry' || msg.type === 'event' || msg.type === 'client_registered' || msg.type === 'client_disconnected' || msg.type === 'report_received') {
            fetchRuns();
          }
        } catch (e) {
          console.error('Error parsing WS frame:', e);
        }
      };

      ws.onclose = () => {
        const badge = document.getElementById('conn-badge');
        badge.className = 'badge badge-offline';
        document.getElementById('conn-text').textContent = 'Reconnecting...';
        setTimeout(connectWebSocket, 2000);
      };

      ws.onerror = () => {
        ws.close();
      };
    }

    async function fetchRuns() {
      try {
        const res = await fetch('/api/v1/runs');
        if (res.ok) {
          const data = await res.json();
          updateRuns(data.runs || []);
        }
      } catch (err) {
        console.error('Failed to fetch runs:', err);
      }
    }

    function updateRuns(runs) {
      allRuns = runs || [];
      let totalRunning = 0, totalWaiting = 0, totalPassed = 0, totalFailed = 0, activeRunners = 0;
      const ownersSet = new Set();

      allRuns.forEach(r => {
        if (r.status === 'RUNNING' || r.status === 'PAUSED') activeRunners++;
        totalRunning += (r.running || r.progress?.running_jobs || 0);
        totalWaiting += (r.waiting || r.progress?.waiting_jobs || 0);
        totalPassed += (r.passed || r.progress?.passed_jobs || 0);
        totalFailed += (r.failed || r.progress?.failed_jobs || 0);
        if (r.owner) ownersSet.add(r.owner);
      });

      document.getElementById('stat-active-runners').textContent = activeRunners;
      document.getElementById('stat-running-jobs').textContent = totalRunning;
      document.getElementById('stat-waiting-jobs').textContent = totalWaiting;
      document.getElementById('stat-completed-jobs').textContent = totalPassed;
      document.getElementById('stat-failed-jobs').textContent = totalFailed;

      // Update owners select dropdown
      const ownerSel = document.getElementById('owner-select');
      const curOwner = ownerSel.value;
      let optHtml = '<option value="ALL">All Owners</option>';
      Array.from(ownersSet).sort().forEach(o => {
        optHtml += `<option value="${escapeHtml(o)}"${o === curOwner ? ' selected' : ''}>👤 ${escapeHtml(o)}</option>`;
      });
      ownerSel.innerHTML = optHtml;

      renderView();
    }

    function switchView(view) {
      currentView = view;
      localStorage.setItem('condor_hub_view', view);
      document.getElementById('view-table-btn').classList.toggle('active', view === 'table');
      document.getElementById('view-cards-btn').classList.toggle('active', view === 'cards');
      document.getElementById('table-view-container').style.display = view === 'table' ? 'block' : 'none';
      document.getElementById('cards-view-container').style.display = view === 'cards' ? 'grid' : 'none';
      renderView();
    }

    function toggleSettings() {
      const panel = document.getElementById('settings-menu');
      panel.style.display = (panel.style.display === 'block') ? 'none' : 'block';
    }

    function changeDensity(val) {
      currentDensity = val;
      localStorage.setItem('condor_hub_density', val);
      document.getElementById('main-table').classList.toggle('density-comfortable', val === 'comfortable');
      renderView();
    }

    function changeGrouping(val) {
      currentGrouping = val;
      localStorage.setItem('condor_hub_grouping', val);
      renderView();
    }

    function changeRefreshRate(val) {
      refreshRate = val;
      localStorage.setItem('condor_hub_refresh', val);
      if (refreshTimer) clearInterval(refreshTimer);
      if (val === 'live') {
        if (!ws || ws.readyState !== WebSocket.OPEN) connectWebSocket();
      } else if (val !== 'paused') {
        refreshTimer = setInterval(fetchRuns, parseInt(val, 10));
      }
    }

    function toggleFilterRow(checked) {
      showFilterRow = checked;
      localStorage.setItem('condor_hub_filter_row', checked ? 'true' : 'false');
      document.getElementById('column-filters-row').style.display = checked ? 'table-row' : 'none';
    }

    function handleSearch(q) {
      searchQuery = (q || '').trim().toLowerCase();
      renderView();
    }

    function handleStatusFilter(s) {
      statusFilter = s;
      document.getElementById('status-select').value = s;
      const thSel = document.getElementById('th-filter-status');
      if (thSel) thSel.value = s;
      renderView();
    }

    function handleOwnerFilter(o) {
      ownerFilter = o;
      renderView();
    }

    function handleColumnFilter(field, val) {
      colFilters[field] = (val || '').trim().toLowerCase();
      renderView();
    }

    function resetFilters() {
      searchQuery = '';
      statusFilter = 'ALL';
      ownerFilter = 'ALL';
      colFilters = { name: '', start_time: '', owner: '' };
      document.getElementById('search-box').value = '';
      document.getElementById('status-select').value = 'ALL';
      document.getElementById('owner-select').value = 'ALL';
      const thStat = document.getElementById('th-filter-status');
      if (thStat) thStat.value = 'ALL';
      const thName = document.getElementById('th-filter-name');
      if (thName) thName.value = '';
      const thStart = document.getElementById('th-filter-start_time');
      if (thStart) thStart.value = '';
      const thOwner = document.getElementById('th-filter-owner');
      if (thOwner) thOwner.value = '';
      renderView();
    }

    function handleSort(field) {
      if (sortField === field) {
        sortAsc = !sortAsc;
      } else {
        sortField = field;
        sortAsc = (field === 'name' || field === 'status' || field === 'owner');
      }
      renderView();
    }

    function toggleRowDetails(runId) {
      if (expandedRows.has(runId)) {
        expandedRows.delete(runId);
      } else {
        expandedRows.add(runId);
      }
      renderView();
    }

    function getFilteredAndSortedRuns() {
      let filtered = allRuns.filter(r => {
        if (statusFilter !== 'ALL' && r.status !== statusFilter) return false;
        if (ownerFilter !== 'ALL' && (r.owner || 'anonymous') !== ownerFilter) return false;
        if (colFilters.name && !(r.name || r.pipeline || '').toLowerCase().includes(colFilters.name)) return false;
        if (colFilters.start_time && !formatStartTime(r.start_time_ms, r.start_time, r.run_id).toLowerCase().includes(colFilters.start_time)) return false;
        if (colFilters.owner && !(r.owner || 'anonymous').toLowerCase().includes(colFilters.owner)) return false;
        if (!searchQuery) return true;
        const target = `${r.name || r.pipeline || ''} ${r.owner || ''} ${r.project || ''} ${r.run_id || ''} ${(r.tags || []).join(' ')}`.toLowerCase();
        return target.includes(searchQuery);
      });

      filtered.sort((a, b) => {
        let valA, valB;
        if (sortField === 'name') {
          valA = (a.name || a.pipeline || '').toLowerCase();
          valB = (b.name || b.pipeline || '').toLowerCase();
        } else if (sortField === 'status') {
          valA = a.status || '';
          valB = b.status || '';
        } else if (sortField === 'owner') {
          valA = (a.owner || 'anonymous').toLowerCase();
          valB = (b.owner || 'anonymous').toLowerCase();
        } else if (sortField === 'total_runs') {
          valA = a.total_runs || a.total_jobs || a.progress?.total_jobs || 0;
          valB = b.total_runs || b.total_jobs || b.progress?.total_jobs || 0;
        } else if (sortField === 'passed') {
          valA = a.passed || a.passed_jobs || a.progress?.passed_jobs || 0;
          valB = b.passed || b.passed_jobs || b.progress?.passed_jobs || 0;
        } else if (sortField === 'failed') {
          valA = a.failed || a.failed_jobs || a.progress?.failed_jobs || 0;
          valB = b.failed || b.failed_jobs || b.progress?.failed_jobs || 0;
        } else if (sortField === 'running') {
          valA = a.running || a.running_jobs || a.progress?.running_jobs || 0;
          valB = b.running || b.running_jobs || b.progress?.running_jobs || 0;
        } else if (sortField === 'waiting') {
          valA = a.waiting || a.waiting_jobs || a.progress?.waiting_jobs || 0;
          valB = b.waiting || b.waiting_jobs || b.progress?.waiting_jobs || 0;
        } else if (sortField === 'other') {
          valA = a.other || a.other_jobs || a.progress?.other_jobs || 0;
          valB = b.other || b.other_jobs || b.progress?.other_jobs || 0;
        } else if (sortField === 'skipped') {
          valA = a.skipped || a.skipped_jobs || a.progress?.skipped_jobs || 0;
          valB = b.skipped || b.skipped_jobs || b.progress?.skipped_jobs || 0;
        } else if (sortField === 'start_time') {
          valA = a.start_time_ms || parseRunIdTime(a.run_id);
          valB = b.start_time_ms || parseRunIdTime(b.run_id);
        } else {
          valA = a.run_id || '';
          valB = b.run_id || '';
        }

        if (valA < valB) return sortAsc ? -1 : 1;
        if (valA > valB) return sortAsc ? 1 : -1;
        return 0;
      });

      return filtered;
    }

    function renderView() {
      const runs = getFilteredAndSortedRuns();
      document.getElementById('runs-count').textContent = runs.length;

      // Update sort header icons
      ['status', 'name', 'total_runs', 'passed', 'failed', 'running', 'waiting', 'other', 'skipped', 'start_time', 'owner'].forEach(f => {
        const th = document.getElementById(`th-${f}`);
        if (!th) return;
        const isCurrent = (sortField === f);
        th.classList.toggle('active-sort', isCurrent);
        const icon = th.querySelector('.sort-icon');
        if (icon) {
          icon.textContent = isCurrent ? (sortAsc ? '▲' : '▼') : '⇅';
        }
      });

      if (currentView === 'table') {
        renderTable(runs);
      } else {
        renderCards(runs);
      }
    }

    function getStatusBadge(status) {
      let cls = 'badge-offline';
      let dot = '<span class="pulse-dot"></span>';
      if (status === 'RUNNING') cls = 'badge-live';
      else if (status === 'PAUSED') cls = 'badge-aborted';
      else if (status === 'PASSED') { cls = 'badge-passed'; dot = ''; }
      else if (status === 'FAILED') { cls = 'badge-failed'; dot = ''; }
      else if (status === 'ABORTED') { cls = 'badge-aborted'; dot = ''; }
      else if (status === 'DISCONNECTED') { cls = 'badge-disconnected'; dot = ''; }
      return `<span class="badge ${cls}">${dot}${status}</span>`;
    }

    function renderTable(runs) {
      const tbody = document.getElementById('runs-table-body');
      if (!runs || runs.length === 0) {
        tbody.innerHTML = `<tr><td colspan="12" class="empty-state">${allRuns.length === 0 ? 'No pipeline runs registered yet. Launch Condor with <code>--server-url ws://&lt;host&gt;:8080/api/v1/ws</code>' : 'No pipeline runs match the active filters.'}</td></tr>`;
        return;
      }

      let html = '';
      runs.forEach(run => {
        const totalRuns = run.total_runs || run.total_jobs || run.progress?.total_jobs || 0;
        const passed = run.passed || run.passed_jobs || run.progress?.passed_jobs || 0;
        const failed = run.failed || run.failed_jobs || run.progress?.failed_jobs || 0;
        const running = run.running || run.running_jobs || run.progress?.running_jobs || 0;
        const waiting = run.waiting !== undefined ? run.waiting : (run.waiting_jobs || run.progress?.waiting_jobs || 0);
        const other = run.other || run.other_jobs || run.progress?.other_jobs || 0;
        const skipped = run.skipped || run.skipped_jobs || run.progress?.skipped_jobs || 0;

        const isRunning = (run.status === 'RUNNING');
        const isPaused = (run.status === 'PAUSED');
        const activeCount = run.active_jobs?.length || running || 0;
        const isExpanded = expandedRows.has(run.run_id);
        const ownerName = run.owner && run.owner !== '' ? run.owner : 'anonymous';

        html += `
          <tr class="${isExpanded ? 'row-expanded' : ''}">
            <td>${getStatusBadge(run.status)}</td>
            <td>
              <div class="run-name">
                <span>${escapeHtml(run.name || run.pipeline)}</span>
                <span class="run-id-sub" title="Run ID: ${run.run_id}">${run.run_id.length > 15 ? run.run_id.substring(9) : run.run_id}</span>
              </div>
            </td>
            <td class="col-num num-total">${totalRuns}</td>
            <td class="col-num num-passed">${passed}</td>
            <td class="col-num num-failed">${failed > 0 ? `<strong>${failed}</strong>` : failed}</td>
            <td class="col-num num-running">
              ${activeCount > 0 ? `
                <button class="btn-slots" onclick="toggleRowDetails('${run.run_id}')" title="View active worker processes">
                  ⚡ ${activeCount} ${isExpanded ? '▲' : '▼'}
                </button>
              ` : `<span>${running}</span>`}
            </td>
            <td class="col-num num-waiting">${waiting}</td>
            <td class="col-num num-other">${other}</td>
            <td class="col-num num-skipped">${skipped}</td>
            <td><span class="time-mono">${formatStartTime(run.start_time_ms, run.start_time, run.run_id)}</span></td>
            <td><span class="owner-pill">👤 ${escapeHtml(ownerName)}</span></td>
            <td style="text-align: right;">
              <div style="display: inline-flex; gap: 3px; justify-content: flex-end;">
                ${run.has_report ? `<a href="/reports/${run.run_id}" target="_blank" class="btn btn-secondary btn-xs" title="Open HTML execution report">📄 Report</a>` : ''}
                ${isRunning ? `<button class="btn btn-warning btn-xs" onclick="sendControl('${run.run_id}', 'pause')">⏸</button>` : ''}
                ${isPaused ? `<button class="btn btn-primary btn-xs" onclick="sendControl('${run.run_id}', 'resume')">▶</button>` : ''}
                ${(isRunning || isPaused) ? `<button class="btn btn-danger btn-xs" onclick="sendControl('${run.run_id}', 'abort')">🛑</button>` : ''}
              </div>
            </td>
          </tr>
        `;

        if (isExpanded && run.active_jobs && run.active_jobs.length > 0) {
          const jobsGrid = run.active_jobs.map(j => `
            <div class="worker-item">
              <div>
                <strong>${escapeHtml(j.name)}</strong>
                <span style="color: var(--text-muted); margin-left: 6px;">[${escapeHtml(j.stage)}/${escapeHtml(j.job_id)}] &bull; PID: ${j.pid} &bull; ${j.elapsed_sec.toFixed(1)}s</span>
              </div>
              <button class="btn btn-danger btn-xs" onclick="killJob('${run.run_id}', '${j.job_id}')">Kill</button>
            </div>
          `).join('');

          html += `
            <tr class="details-row">
              <td colspan="12">
                <div class="worker-box">
                  <div class="worker-title">
                    <span>Active Worker Slots (${run.active_jobs.length})</span>
                    <span style="font-family: monospace; font-size: 10px;">Run: ${escapeHtml(run.run_id)}</span>
                  </div>
                  <div class="worker-grid">${jobsGrid}</div>
                </div>
              </td>
            </tr>
          `;
        }
      });

      tbody.innerHTML = html;
    }

    function renderCards(runs) {
      const container = document.getElementById('cards-view-container');
      if (!runs || runs.length === 0) {
        container.innerHTML = `<div class="empty-state">${allRuns.length === 0 ? 'No pipeline runs registered yet. Launch Condor with <code>--server-url ws://&lt;host&gt;:8080/api/v1/ws</code>' : 'No pipeline runs match the active filters.'}</div>`;
        return;
      }

      container.innerHTML = runs.map(run => {
        const totalRuns = run.total_runs || run.total_jobs || run.progress?.total_jobs || 1;
        const passed = run.passed || run.passed_jobs || run.progress?.passed_jobs || 0;
        const failed = run.failed || run.failed_jobs || run.progress?.failed_jobs || 0;
        const running = run.running || run.running_jobs || run.progress?.running_jobs || 0;
        const waiting = run.waiting !== undefined ? run.waiting : (run.waiting_jobs || run.progress?.waiting_jobs || 0);
        const skipped = run.skipped || run.skipped_jobs || run.progress?.skipped_jobs || 0;

        const passedPct = Math.min(100, (passed / totalRuns) * 100);
        const runningPct = Math.min(100, (running / totalRuns) * 100);
        const failedPct = Math.min(100, (failed / totalRuns) * 100);
        const skippedPct = Math.min(100, (skipped / totalRuns) * 100);

        const isPaused = (run.status === 'PAUSED');
        const isRunning = (run.status === 'RUNNING');
        const ownerName = run.owner && run.owner !== '' ? run.owner : 'anonymous';

        return `
          <div class="run-card">
            <div class="run-card-header">
              <div>
                <div style="display:flex; align-items:center; gap:8px;">
                  <span style="font-size: 16px; font-weight: 700; color: var(--primary);">${escapeHtml(run.name || run.pipeline)}</span>
                  <span class="run-id-sub">${run.run_id}</span>
                </div>
                <div style="font-size: 12px; color: var(--text-muted); margin-top: 4px; display: flex; gap: 12px; align-items: center; flex-wrap: wrap;">
                  <span>Status: ${getStatusBadge(run.status)}</span>
                  <span>Owner: <strong style="color: #cbd5e1;">👤 ${escapeHtml(ownerName)}</strong></span>
                  <span>Start: <strong>${formatStartTime(run.start_time_ms, run.start_time, run.run_id)}</strong></span>
                  <span>Jobs: <strong>${passed}P / ${failed}F / ${running}R / ${waiting}W</strong> (${totalRuns} total)</span>
                </div>
              </div>
              <div style="display: flex; gap: 6px; align-items: center;">
                ${run.has_report ? `<a href="/reports/${run.run_id}" target="_blank" class="btn btn-secondary btn-xs">📄 HTML Report &nearr;</a>` : ''}
                ${isRunning ? `<button class="btn btn-warning btn-xs" onclick="sendControl('${run.run_id}', 'pause')">⏸ Pause</button>` : ''}
                ${isPaused ? `<button class="btn btn-primary btn-xs" onclick="sendControl('${run.run_id}', 'resume')">▶ Resume</button>` : ''}
                ${(isRunning || isPaused) ? `<button class="btn btn-danger btn-xs" onclick="sendControl('${run.run_id}', 'abort')">🛑 Abort</button>` : ''}
              </div>
            </div>

            <div class="progress-bar-wrap">
              <div class="prog-passed" style="width: ${passedPct}%"></div>
              <div class="prog-running" style="width: ${runningPct}%"></div>
              <div class="prog-failed" style="width: ${failedPct}%"></div>
              <div class="prog-skipped" style="width: ${skippedPct}%"></div>
            </div>
          </div>
        `;
      }).join('');
    }

    function escapeHtml(str) {
      if (!str) return '';
      return String(str)
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;');
    }

    async function sendControl(runId, action, extra = {}) {
      try {
        const res = await fetch(`/api/v1/runs/${runId}/control`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ action, ...extra })
        });
        if (res.ok) {
          fetchRuns();
        } else {
          alert('Failed to send command: ' + (await res.text()));
        }
      } catch (e) {
        alert('Network error: ' + e.message);
      }
    }

    function killJob(runId, jobId) {
      if (confirm(`Kill job ${jobId}?`)) {
        sendControl(runId, 'kill_job', { job_id: jobId });
      }
    }

    function triggerEmergencyStop() {
      if (confirm('EMERGENCY: Abort all running Condor pipelines across the fleet?')) {
        fetch('/api/v1/emergency-stop', { method: 'POST' })
          .then(() => fetchRuns())
          .catch(e => alert(e.message));
      }
    }

    // Apply saved preferences
    document.getElementById('setting-density').value = currentDensity;
    document.getElementById('setting-group').value = currentGrouping;
    document.getElementById('setting-refresh').value = refreshRate;
    document.getElementById('setting-filter-row').checked = showFilterRow;
    document.getElementById('column-filters-row').style.display = showFilterRow ? 'table-row' : 'none';
    changeDensity(currentDensity);
    changeRefreshRate(refreshRate);

    // Initial view rendering
    switchView(currentView);

    // Connect WebSocket on load
    connectWebSocket();
  </script>
</body>
</html>
)rawhtml";

} // namespace condor::hub
