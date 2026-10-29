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
      --bg: #0f172a;
      --card-bg: #1e293b;
      --border: #334155;
      --text: #f8fafc;
      --text-muted: #94a3b8;
      --primary: #38bdf8;
      --primary-hover: #0ea5e9;
      --success: #22c55e;
      --warning: #f59e0b;
      --danger: #ef4444;
      --purple: #a855f7;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      background: var(--bg);
      color: var(--text);
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
      line-height: 1.5;
      padding: 24px;
    }
    header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 24px;
      padding-bottom: 16px;
      border-bottom: 1px solid var(--border);
    }
    .header-left {
      display: flex;
      align-items: center;
      gap: 12px;
    }
    .logo {
      font-size: 24px;
      font-weight: 800;
      color: var(--primary);
      letter-spacing: -0.5px;
    }
    .header-right {
      display: flex;
      align-items: center;
      gap: 16px;
    }
    .badge {
      display: inline-flex;
      align-items: center;
      gap: 6px;
      padding: 4px 10px;
      border-radius: 9999px;
      font-size: 12px;
      font-weight: 600;
      text-transform: uppercase;
      white-space: nowrap;
    }
    .badge-live { background: rgba(34, 197, 94, 0.2); color: var(--success); }
    .badge-passed { background: rgba(34, 197, 94, 0.25); color: #4ade80; border: 1px solid rgba(74, 222, 128, 0.3); }
    .badge-failed { background: rgba(239, 68, 68, 0.25); color: #f87171; border: 1px solid rgba(248, 113, 113, 0.3); }
    .badge-aborted { background: rgba(245, 158, 11, 0.25); color: #fbbf24; border: 1px solid rgba(251, 191, 36, 0.3); }
    .badge-offline { background: rgba(148, 163, 184, 0.2); color: var(--text-muted); }
    .badge-disconnected { background: rgba(239, 68, 68, 0.15); color: #fca5a5; border: 1px solid rgba(252, 165, 165, 0.2); }
    .pulse-dot {
      width: 8px;
      height: 8px;
      border-radius: 50%;
      background: currentColor;
      box-shadow: 0 0 8px currentColor;
    }
    .btn {
      padding: 7px 12px;
      border-radius: 6px;
      border: 1px solid transparent;
      font-size: 13px;
      font-weight: 600;
      cursor: pointer;
      display: inline-flex;
      align-items: center;
      gap: 6px;
      transition: all 0.15s ease;
      white-space: nowrap;
      text-decoration: none;
    }
    .btn-sm {
      padding: 4px 8px;
      font-size: 11px;
      border-radius: 4px;
    }
    .btn-danger {
      background: rgba(239, 68, 68, 0.15);
      border-color: rgba(239, 68, 68, 0.4);
      color: #fca5a5;
    }
    .btn-danger:hover { background: #ef4444; color: white; }
    .btn-primary { background: var(--primary); color: #0f172a; }
    .btn-primary:hover { background: var(--primary-hover); }
    .btn-secondary {
      background: #334155;
      color: var(--text);
    }
    .btn-secondary:hover { background: #475569; }
    .btn-warning {
      background: rgba(245, 158, 11, 0.2);
      border-color: rgba(245, 158, 11, 0.4);
      color: #fcd34d;
    }
    .btn-warning:hover { background: #f59e0b; color: black; }

    /* Stats Grid */
    .stats-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
      gap: 16px;
      margin-bottom: 24px;
    }
    .stat-card {
      background: var(--card-bg);
      border: 1px solid var(--border);
      border-radius: 8px;
      padding: 16px;
    }
    .stat-title {
      font-size: 12px;
      font-weight: 600;
      color: var(--text-muted);
      text-transform: uppercase;
      margin-bottom: 6px;
    }
    .stat-value {
      font-size: 28px;
      font-weight: 800;
      color: var(--text);
    }

    /* Toolbar & Controls */
    .toolbar-container {
      display: flex;
      justify-content: space-between;
      align-items: center;
      flex-wrap: wrap;
      gap: 12px;
      margin-bottom: 16px;
    }
    .toolbar-left {
      display: flex;
      align-items: center;
      gap: 12px;
    }
    .toolbar-right {
      display: flex;
      align-items: center;
      gap: 10px;
    }
    .section-title {
      font-size: 18px;
      font-weight: 700;
      display: flex;
      align-items: center;
      gap: 8px;
    }
    .count-pill {
      font-size: 12px;
      background: #334155;
      color: var(--primary);
      padding: 2px 8px;
      border-radius: 12px;
      font-weight: 700;
    }
    .search-input {
      background: #0f172a;
      border: 1px solid var(--border);
      border-radius: 6px;
      color: var(--text);
      padding: 7px 12px;
      font-size: 13px;
      width: 260px;
      outline: none;
      transition: border-color 0.15s;
    }
    .search-input:focus {
      border-color: var(--primary);
    }
    .filter-select {
      background: #0f172a;
      border: 1px solid var(--border);
      border-radius: 6px;
      color: var(--text);
      padding: 7px 10px;
      font-size: 13px;
      outline: none;
      cursor: pointer;
    }
    .view-toggle {
      display: inline-flex;
      background: #0f172a;
      border: 1px solid var(--border);
      border-radius: 6px;
      padding: 2px;
      gap: 2px;
    }
    .view-toggle-btn {
      background: transparent;
      border: none;
      color: var(--text-muted);
      padding: 5px 10px;
      border-radius: 4px;
      font-size: 12px;
      font-weight: 600;
      cursor: pointer;
      display: inline-flex;
      align-items: center;
      gap: 4px;
    }
    .view-toggle-btn.active {
      background: var(--card-bg);
      color: var(--primary);
      box-shadow: 0 1px 3px rgba(0,0,0,0.3);
    }

    /* Dynamic Table Styles */
    .table-container {
      background: var(--card-bg);
      border: 1px solid var(--border);
      border-radius: 8px;
      overflow-x: auto;
      box-shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.1), 0 2px 4px -2px rgba(0, 0, 0, 0.1);
    }
    .dynamic-table {
      width: 100%;
      border-collapse: collapse;
      text-align: left;
      font-size: 13px;
    }
    .dynamic-table th {
      background: #162032;
      color: var(--text-muted);
      font-weight: 700;
      font-size: 11px;
      text-transform: uppercase;
      letter-spacing: 0.05em;
      padding: 12px 14px;
      border-bottom: 1px solid var(--border);
      cursor: pointer;
      user-select: none;
      white-space: nowrap;
      transition: background 0.15s ease, color 0.15s ease;
    }
    .dynamic-table th:hover {
      background: #1e293b;
      color: var(--text);
    }
    .dynamic-table th .sort-icon {
      display: inline-block;
      margin-left: 4px;
      font-size: 10px;
      opacity: 0.4;
    }
    .dynamic-table th.active-sort .sort-icon {
      opacity: 1;
      color: var(--primary);
    }
    .dynamic-table td {
      padding: 12px 14px;
      border-bottom: 1px solid #283548;
      vertical-align: middle;
    }
    .dynamic-table tbody tr {
      transition: background 0.15s ease;
    }
    .dynamic-table tbody tr:hover {
      background: #24334a;
    }
    .dynamic-table tbody tr.row-expanded {
      background: #172439;
    }
    .run-id-mono {
      font-family: monospace;
      font-size: 11px;
      background: #0f172a;
      color: var(--text-muted);
      padding: 2px 6px;
      border-radius: 4px;
      display: inline-block;
    }
    .pipeline-cell-title {
      font-weight: 700;
      color: var(--primary);
      font-size: 14px;
    }
    .pipeline-cell-sub {
      font-size: 11px;
      color: var(--text-muted);
      margin-top: 2px;
      display: flex;
      gap: 6px;
      align-items: center;
    }
    .tag-badge {
      background: #334155;
      color: #94a3b8;
      font-size: 10px;
      padding: 1px 5px;
      border-radius: 3px;
    }
    .mini-progress-wrap {
      display: flex;
      flex-direction: column;
      gap: 4px;
      min-width: 140px;
    }
    .mini-progress-bar {
      height: 6px;
      background: #0f172a;
      border-radius: 3px;
      overflow: hidden;
      display: flex;
    }
    .mini-progress-stats {
      display: flex;
      justify-content: space-between;
      font-size: 11px;
      color: var(--text-muted);
    }
    .btn-slots {
      background: #0f172a;
      border: 1px solid var(--border);
      color: var(--text);
      padding: 3px 8px;
      border-radius: 4px;
      font-size: 11px;
      cursor: pointer;
      display: inline-flex;
      align-items: center;
      gap: 4px;
    }
    .btn-slots:hover {
      border-color: var(--primary);
      color: var(--primary);
    }

    /* Expandable Sub-Row (Active Jobs Drawer) */
    .details-row {
      background: #090e1a !important;
    }
    .details-row td {
      padding: 0 0 12px 0;
      border-bottom: 2px solid var(--border);
    }
    .details-box {
      padding: 12px 20px;
      background: #0d1527;
      border-left: 3px solid var(--primary);
      margin: 4px 14px 4px 14px;
      border-radius: 0 6px 6px 0;
    }
    .details-title {
      font-size: 11px;
      font-weight: 700;
      text-transform: uppercase;
      color: var(--text-muted);
      margin-bottom: 8px;
      display: flex;
      justify-content: space-between;
    }
    .job-item-grid {
      display: grid;
      grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));
      gap: 8px;
    }
    .job-item-card {
      background: #141f33;
      border: 1px solid #1e2d44;
      border-radius: 4px;
      padding: 8px 10px;
      display: flex;
      justify-content: space-between;
      align-items: center;
    }
    .job-item-name {
      font-size: 12px;
      font-weight: 600;
      color: var(--text);
    }
    .job-item-meta {
      font-size: 11px;
      color: var(--text-muted);
      margin-top: 2px;
      font-family: monospace;
    }

    /* Fleet Cards (Alternative View) */
    .fleet-grid {
      display: grid;
      grid-template-columns: 1fr;
      gap: 16px;
    }
    .run-card {
      background: var(--card-bg);
      border: 1px solid var(--border);
      border-radius: 8px;
      padding: 20px;
      display: flex;
      flex-direction: column;
      gap: 16px;
      transition: border-color 0.15s ease;
    }
    .run-card:hover { border-color: #475569; }
    .run-card-header {
      display: flex;
      justify-content: space-between;
      align-items: flex-start;
      flex-wrap: wrap;
      gap: 12px;
    }
    .run-info-title {
      font-size: 18px;
      font-weight: 700;
      color: var(--primary);
    }
    .run-id {
      font-family: monospace;
      font-size: 12px;
      color: var(--text-muted);
      background: #0f172a;
      padding: 2px 6px;
      border-radius: 4px;
      margin-left: 8px;
    }
    .run-meta {
      display: flex;
      align-items: center;
      gap: 12px;
      font-size: 13px;
      color: var(--text-muted);
      margin-top: 4px;
      flex-wrap: wrap;
    }
    .run-actions {
      display: flex;
      gap: 8px;
      align-items: center;
      flex-wrap: wrap;
    }

    /* Progress bar */
    .progress-container {
      display: flex;
      flex-direction: column;
      gap: 6px;
    }
    .progress-bar-wrap {
      height: 10px;
      background: #0f172a;
      border-radius: 6px;
      overflow: hidden;
      display: flex;
    }
    .prog-passed { background: var(--success); }
    .prog-running { background: var(--primary); }
    .prog-failed { background: var(--danger); }
    .prog-skipped { background: #64748b; }
    .progress-stats-row {
      display: flex;
      justify-content: space-between;
      font-size: 12px;
      color: var(--text-muted);
    }

    /* Active jobs list */
    .active-jobs-box {
      background: #0f172a;
      border-radius: 6px;
      padding: 12px;
      font-size: 13px;
      border: 1px solid #1e293b;
    }
    .active-jobs-title {
      font-weight: 700;
      font-size: 11px;
      text-transform: uppercase;
      color: var(--text-muted);
      margin-bottom: 8px;
      display: flex;
      justify-content: space-between;
    }
    .job-row {
      display: flex;
      justify-content: space-between;
      align-items: center;
      padding: 4px 0;
      border-bottom: 1px solid #1e293b;
    }
    .job-row:last-child { border-bottom: none; }
    .job-name {
      font-weight: 600;
      color: var(--text);
      display: flex;
      gap: 8px;
      align-items: center;
    }
    .job-badge {
      font-family: monospace;
      font-size: 11px;
      background: #334155;
      padding: 2px 6px;
      border-radius: 4px;
    }
    .empty-state {
      text-align: center;
      padding: 48px;
      color: var(--text-muted);
      font-size: 15px;
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
      <button class="btn btn-danger" onclick="triggerEmergencyStop()">🚨 Emergency Stop All</button>
    </div>
  </header>

  <div class="stats-grid">
    <div class="stat-card">
      <div class="stat-title">Active Runners</div>
      <div class="stat-value" id="stat-active-runners">0</div>
    </div>
    <div class="stat-card">
      <div class="stat-title">Running Jobs</div>
      <div class="stat-value" id="stat-running-jobs" style="color: var(--primary);">0</div>
    </div>
    <div class="stat-card">
      <div class="stat-title">Completed Jobs</div>
      <div class="stat-value" id="stat-completed-jobs" style="color: var(--success);">0</div>
    </div>
    <div class="stat-card">
      <div class="stat-title">Failed Jobs</div>
      <div class="stat-value" id="stat-failed-jobs" style="color: var(--danger);">0</div>
    </div>
  </div>

  <div class="toolbar-container">
    <div class="toolbar-left">
      <div class="section-title">
        <span>Pipeline Runs</span>
        <span class="count-pill" id="runs-count">0</span>
      </div>
      <input type="text" id="search-box" class="search-input" placeholder="🔍 Search pipeline, project, ID..." oninput="handleSearch(this.value)" />
      <select id="status-select" class="filter-select" onchange="handleStatusFilter(this.value)">
        <option value="ALL">All Statuses</option>
        <option value="RUNNING">Running</option>
        <option value="PAUSED">Paused</option>
        <option value="PASSED">Passed</option>
        <option value="FAILED">Failed</option>
        <option value="ABORTED">Aborted</option>
        <option value="DISCONNECTED">Disconnected</option>
      </select>
    </div>
    <div class="toolbar-right">
      <div class="view-toggle">
        <button id="view-table-btn" class="view-toggle-btn active" onclick="switchView('table')">📊 Table</button>
        <button id="view-cards-btn" class="view-toggle-btn" onclick="switchView('cards')">🗂️ Cards</button>
      </div>
      <button class="btn btn-secondary" onclick="fetchRuns()">🔄 Refresh</button>
    </div>
  </div>

  <!-- Dynamic Table View Container -->
  <div id="table-view-container" class="table-container">
    <table class="dynamic-table">
      <thead>
        <tr>
          <th onclick="handleSort('status')" id="th-status">Status <span class="sort-icon">⇅</span></th>
          <th onclick="handleSort('pipeline')" id="th-pipeline">Pipeline / Project <span class="sort-icon">⇅</span></th>
          <th onclick="handleSort('run_id')" id="th-run_id">Run ID <span class="sort-icon">⇅</span></th>
          <th onclick="handleSort('progress')" id="th-progress">Progress <span class="sort-icon">⇅</span></th>
          <th onclick="handleSort('active_jobs')" id="th-active_jobs">Active Workers <span class="sort-icon">⇅</span></th>
          <th onclick="handleSort('elapsed')" id="th-elapsed">Elapsed <span class="sort-icon">⇅</span></th>
          <th onclick="handleSort('peak_rss')" id="th-peak_rss">Peak RSS <span class="sort-icon">⇅</span></th>
          <th>Report</th>
          <th style="text-align: right;">Actions</th>
        </tr>
      </thead>
      <tbody id="runs-table-body">
        <tr>
          <td colspan="9" class="empty-state">No pipeline runs registered yet. Launch Condor with <code>--server-url ws://&lt;host&gt;:8080/api/v1/ws</code></td>
        </tr>
      </tbody>
    </table>
  </div>

  <!-- Cards View Container (Alternate) -->
  <div id="cards-view-container" class="fleet-grid" style="display: none;">
    <div class="empty-state">No pipeline runs registered yet. Launch Condor with <code>--server-url ws://&lt;host&gt;:8080/api/v1/ws</code></div>
  </div>

  <script>
    let ws = null;
    let allRuns = [];
    let currentView = localStorage.getItem('condor_hub_view') || 'table';
    let sortField = 'elapsed';
    let sortAsc = false;
    let searchQuery = '';
    let statusFilter = 'ALL';
    let expandedRows = new Set();

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
      let totalRunning = 0, totalPassed = 0, totalFailed = 0, activeRunners = 0;

      allRuns.forEach(r => {
        if (r.status === 'RUNNING' || r.status === 'PAUSED') activeRunners++;
        totalRunning += (r.progress?.running_jobs || 0);
        totalPassed += (r.progress?.passed_jobs || 0);
        totalFailed += (r.progress?.failed_jobs || 0);
      });

      document.getElementById('stat-active-runners').textContent = activeRunners;
      document.getElementById('stat-running-jobs').textContent = totalRunning;
      document.getElementById('stat-completed-jobs').textContent = totalPassed;
      document.getElementById('stat-failed-jobs').textContent = totalFailed;

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

    function handleSearch(q) {
      searchQuery = (q || '').trim().toLowerCase();
      renderView();
    }

    function handleStatusFilter(s) {
      statusFilter = s;
      renderView();
    }

    function handleSort(field) {
      if (sortField === field) {
        sortAsc = !sortAsc;
      } else {
        sortField = field;
        sortAsc = (field === 'pipeline' || field === 'run_id' || field === 'status');
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
        if (!searchQuery) return true;
        const target = `${r.pipeline || ''} ${r.project || ''} ${r.run_id || ''} ${(r.tags || []).join(' ')}`.toLowerCase();
        return target.includes(searchQuery);
      });

      filtered.sort((a, b) => {
        let valA, valB;
        if (sortField === 'pipeline') {
          valA = (a.pipeline || '').toLowerCase();
          valB = (b.pipeline || '').toLowerCase();
        } else if (sortField === 'run_id') {
          valA = a.run_id || '';
          valB = b.run_id || '';
        } else if (sortField === 'status') {
          valA = a.status || '';
          valB = b.status || '';
        } else if (sortField === 'progress') {
          valA = a.progress?.percent || 0;
          valB = b.progress?.percent || 0;
        } else if (sortField === 'active_jobs') {
          valA = a.active_jobs?.length || 0;
          valB = b.active_jobs?.length || 0;
        } else if (sortField === 'elapsed') {
          valA = a.progress?.elapsed_sec || 0;
          valB = b.progress?.elapsed_sec || 0;
        } else if (sortField === 'peak_rss') {
          valA = a.peak_rss_mb || 0;
          valB = b.peak_rss_mb || 0;
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
      ['status', 'pipeline', 'run_id', 'progress', 'active_jobs', 'elapsed', 'peak_rss'].forEach(f => {
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
        tbody.innerHTML = `<tr><td colspan="9" class="empty-state">${allRuns.length === 0 ? 'No pipeline runs registered yet. Launch Condor with <code>--server-url ws://&lt;host&gt;:8080/api/v1/ws</code>' : 'No pipeline runs match the active filter.'}</td></tr>`;
        return;
      }

      let html = '';
      runs.forEach(run => {
        const prog = run.progress || {};
        const total = prog.total_jobs || 1;
        const passedPct = Math.min(100, ((prog.passed_jobs || 0) / total) * 100);
        const runningPct = Math.min(100, ((prog.running_jobs || 0) / total) * 100);
        const failedPct = Math.min(100, ((prog.failed_jobs || 0) / total) * 100);
        const skippedPct = Math.min(100, ((prog.skipped_jobs || 0) / total) * 100);

        const isRunning = (run.status === 'RUNNING');
        const isPaused = (run.status === 'PAUSED');
        const activeCount = run.active_jobs?.length || 0;
        const isExpanded = expandedRows.has(run.run_id);

        const tagsHtml = (run.tags && run.tags.length > 0)
          ? run.tags.map(t => `<span class="tag-badge">${escapeHtml(t)}</span>`).join('')
          : '';

        html += `
          <tr class="${isExpanded ? 'row-expanded' : ''}">
            <td>${getStatusBadge(run.status)}</td>
            <td>
              <div class="pipeline-cell-title">${escapeHtml(run.pipeline)}</div>
              <div class="pipeline-cell-sub">
                <span>${escapeHtml(run.project || 'default')}</span>
                ${tagsHtml}
              </div>
            </td>
            <td><span class="run-id-mono" title="${run.run_id}">${run.run_id}</span></td>
            <td>
              <div class="mini-progress-wrap">
                <div class="mini-progress-bar">
                  <div class="prog-passed" style="width: ${passedPct}%"></div>
                  <div class="prog-running" style="width: ${runningPct}%"></div>
                  <div class="prog-failed" style="width: ${failedPct}%"></div>
                  <div class="prog-skipped" style="width: ${skippedPct}%"></div>
                </div>
                <div class="mini-progress-stats">
                  <span><strong>${prog.percent ? prog.percent.toFixed(1) : 0}%</strong></span>
                  <span>${prog.passed_jobs || 0}/${prog.total_jobs || 0} jobs</span>
                </div>
              </div>
            </td>
            <td>
              ${activeCount > 0 ? `
                <button class="btn-slots" onclick="toggleRowDetails('${run.run_id}')">
                  ⚡ <strong>${activeCount} active</strong> ${isExpanded ? '▲' : '▼'}
                </button>
              ` : `<span style="color: var(--text-muted); font-size: 12px;">Idle</span>`}
            </td>
            <td><strong>${formatTime(prog.elapsed_sec || 0)}</strong></td>
            <td>${run.peak_rss_mb > 0 ? `<span>${run.peak_rss_mb.toFixed(1)} MB</span>` : '<span style="color:var(--text-muted);">-</span>'}</td>
            <td>
              ${run.has_report ? `
                <a href="/reports/${run.run_id}" target="_blank" class="btn btn-secondary btn-sm" title="Open standalone HTML report">📄 Report &nearr;</a>
              ` : '<span style="color:var(--text-muted); font-size: 11px;">-</span>'}
            </td>
            <td style="text-align: right;">
              <div style="display: inline-flex; gap: 4px; justify-content: flex-end;">
                ${isRunning ? `<button class="btn btn-warning btn-sm" onclick="sendControl('${run.run_id}', 'pause')">⏸ Pause</button>` : ''}
                ${isPaused ? `<button class="btn btn-primary btn-sm" onclick="sendControl('${run.run_id}', 'resume')">▶ Resume</button>` : ''}
                ${(isRunning || isPaused) ? `<button class="btn btn-danger btn-sm" onclick="sendControl('${run.run_id}', 'abort')">🛑 Abort</button>` : ''}
              </div>
            </td>
          </tr>
        `;

        if (isExpanded && run.active_jobs && run.active_jobs.length > 0) {
          const jobsGrid = run.active_jobs.map(j => `
            <div class="job-item-card">
              <div>
                <div class="job-item-name">${escapeHtml(j.name)}</div>
                <div class="job-item-meta">${escapeHtml(j.stage)}/${escapeHtml(j.job_id)} &bull; PID: ${j.pid} &bull; ${j.elapsed_sec.toFixed(1)}s</div>
              </div>
              <button class="btn btn-danger btn-sm" onclick="killJob('${run.run_id}', '${j.job_id}')">Kill</button>
            </div>
          `).join('');

          html += `
            <tr class="details-row">
              <td colspan="9">
                <div class="details-box">
                  <div class="details-title">
                    <span>Active Worker Processes (${run.active_jobs.length})</span>
                    <span style="font-family: monospace; font-size: 10px;">Pipeline: ${escapeHtml(run.run_id)}</span>
                  </div>
                  <div class="job-item-grid">${jobsGrid}</div>
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
        container.innerHTML = `<div class="empty-state">${allRuns.length === 0 ? 'No pipeline runs registered yet. Launch Condor with <code>--server-url ws://&lt;host&gt;:8080/api/v1/ws</code>' : 'No pipeline runs match the active filter.'}</div>`;
        return;
      }

      container.innerHTML = runs.map(run => {
        const prog = run.progress || {};
        const total = prog.total_jobs || 1;
        const passedPct = Math.min(100, ((prog.passed_jobs || 0) / total) * 100);
        const runningPct = Math.min(100, ((prog.running_jobs || 0) / total) * 100);
        const failedPct = Math.min(100, ((prog.failed_jobs || 0) / total) * 100);
        const skippedPct = Math.min(100, ((prog.skipped_jobs || 0) / total) * 100);

        const isPaused = (run.status === 'PAUSED');
        const isRunning = (run.status === 'RUNNING');

        const activeJobsHtml = (run.active_jobs && run.active_jobs.length > 0)
          ? run.active_jobs.map(j => `
              <div class="job-row">
                <div class="job-name">
                  <span class="job-badge">${escapeHtml(j.stage)}/${escapeHtml(j.job_id)}</span>
                  <span>${escapeHtml(j.name)}</span>
                </div>
                <div style="color: var(--text-muted); font-size: 12px; display:flex; gap:12px; align-items:center;">
                  <span>PID: ${j.pid}</span>
                  <span>${j.elapsed_sec.toFixed(1)}s</span>
                  <button class="btn btn-danger btn-sm" onclick="killJob('${run.run_id}', '${j.job_id}')">Kill</button>
                </div>
              </div>
            `).join('')
          : '<div style="color: var(--text-muted); font-style: italic;">No active worker jobs currently executing</div>';

        return `
          <div class="run-card">
            <div class="run-card-header">
              <div>
                <div style="display:flex; align-items:center;">
                  <span class="run-info-title">${escapeHtml(run.pipeline)}</span>
                  <span class="run-id">${run.run_id}</span>
                </div>
                <div class="run-meta">
                  <span>Project: <strong>${escapeHtml(run.project || 'default')}</strong></span>
                  <span>Status: ${getStatusBadge(run.status)}</span>
                  <span>Elapsed: <strong>${formatTime(prog.elapsed_sec || 0)}</strong></span>
                  ${run.peak_rss_mb > 0 ? `<span>Peak RSS: <strong>${run.peak_rss_mb.toFixed(1)} MB</strong></span>` : ''}
                </div>
              </div>
              <div class="run-actions">
                ${run.has_report ? `<a href="/reports/${run.run_id}" target="_blank" class="btn btn-secondary">📄 HTML Report &nearr;</a>` : ''}
                ${isRunning ? `<button class="btn btn-warning" onclick="sendControl('${run.run_id}', 'pause')">⏸️ Pause</button>` : ''}
                ${isPaused ? `<button class="btn btn-primary" onclick="sendControl('${run.run_id}', 'resume')">▶️ Resume</button>` : ''}
                ${(isRunning || isPaused) ? `<button class="btn btn-danger" onclick="sendControl('${run.run_id}', 'abort')">🛑 Abort</button>` : ''}
              </div>
            </div>

            <div class="progress-container">
              <div class="progress-bar-wrap">
                <div class="prog-passed" style="width: ${passedPct}%"></div>
                <div class="prog-running" style="width: ${runningPct}%"></div>
                <div class="prog-failed" style="width: ${failedPct}%"></div>
                <div class="prog-skipped" style="width: ${skippedPct}%"></div>
              </div>
              <div class="progress-stats-row">
                <span>Progress: <strong>${prog.percent ? prog.percent.toFixed(1) : 0}%</strong></span>
                <span>${prog.passed_jobs || 0} passed / ${prog.failed_jobs || 0} failed / ${prog.running_jobs || 0} running / ${prog.total_jobs || 0} total</span>
              </div>
            </div>

            <div class="active-jobs-box">
              <div class="active-jobs-title">
                <span>Active Worker Slots (${run.active_jobs?.length || 0})</span>
              </div>
              ${activeJobsHtml}
            </div>
          </div>
        `;
      }).join('');
    }

    function formatTime(sec) {
      const s = Math.floor(sec % 60);
      const m = Math.floor((sec / 60) % 60);
      const h = Math.floor(sec / 3600);
      return `${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`;
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

    // Initialize initial view
    switchView(currentView);

    // Connect WebSocket on page load
    connectWebSocket();
    setInterval(fetchRuns, 2000);
  </script>
</body>
</html>
)rawhtml";

} // namespace condor::hub
