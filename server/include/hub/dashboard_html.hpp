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
    }
    .badge-live { background: rgba(34, 197, 94, 0.2); color: var(--success); }
    .badge-offline { background: rgba(239, 68, 68, 0.2); color: var(--danger); }
    .pulse-dot {
      width: 8px;
      height: 8px;
      border-radius: 50%;
      background: currentColor;
      box-shadow: 0 0 8px currentColor;
    }
    .btn {
      padding: 8px 14px;
      border-radius: 6px;
      border: 1px solid transparent;
      font-size: 13px;
      font-weight: 600;
      cursor: pointer;
      display: inline-flex;
      align-items: center;
      gap: 6px;
      transition: all 0.15s ease;
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

    /* Fleet Cards */
    .section-title {
      font-size: 18px;
      font-weight: 700;
      margin-bottom: 16px;
      display: flex;
      justify-content: space-between;
      align-items: center;
    }
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
    }
    .run-actions {
      display: flex;
      gap: 8px;
      align-items: center;
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
      font-size: 16px;
    }

    /* Modal */
    .modal-backdrop {
      position: fixed;
      inset: 0;
      background: rgba(0, 0, 0, 0.7);
      display: flex;
      align-items: center;
      justify-content: center;
      z-index: 100;
    }
    .modal-box {
      background: var(--card-bg);
      border: 1px solid var(--border);
      border-radius: 8px;
      padding: 24px;
      max-width: 500px;
      width: 90%;
    }
    .modal-title { font-size: 18px; font-weight: 700; margin-bottom: 12px; }
    .modal-actions { display: flex; justify-content: flex-end; gap: 8px; margin-top: 20px; }
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

  <div class="section-title">
    <span>Active & Recent Pipelines</span>
    <button class="btn btn-secondary" onclick="fetchRuns()">🔄 Refresh</button>
  </div>

  <div class="fleet-grid" id="runs-container">
    <div class="empty-state">No pipeline runs registered yet. Launch Condor with <code>--server-url ws://&lt;host&gt;:8080/api/v1/ws</code></div>
  </div>

  <script>
    let ws = null;
    let runsData = {};

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
          } else if (msg.type === 'telemetry' || msg.type === 'event') {
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
      runsData = {};
      let totalRunning = 0, totalPassed = 0, totalFailed = 0, activeRunners = 0;

      runs.forEach(r => {
        runsData[r.run_id] = r;
        if (r.status === 'RUNNING' || r.status === 'PAUSED') activeRunners++;
        totalRunning += (r.progress?.running_jobs || 0);
        totalPassed += (r.progress?.passed_jobs || 0);
        totalFailed += (r.progress?.failed_jobs || 0);
      });

      document.getElementById('stat-active-runners').textContent = activeRunners;
      document.getElementById('stat-running-jobs').textContent = totalRunning;
      document.getElementById('stat-completed-jobs').textContent = totalPassed;
      document.getElementById('stat-failed-jobs').textContent = totalFailed;

      renderFleet(runs);
    }

    function renderFleet(runs) {
      const container = document.getElementById('runs-container');
      if (!runs || runs.length === 0) {
        container.innerHTML = '<div class="empty-state">No pipeline runs registered yet. Launch Condor with <code>--server-url ws://&lt;host&gt;:8080/api/v1/ws</code></div>';
        return;
      }

      container.innerHTML = runs.map(run => {
        const prog = run.progress || {};
        const total = prog.total_jobs || 1;
        const passedPct = ((prog.passed_jobs || 0) / total) * 100;
        const runningPct = ((prog.running_jobs || 0) / total) * 100;
        const failedPct = ((prog.failed_jobs || 0) / total) * 100;
        const skippedPct = ((prog.skipped_jobs || 0) / total) * 100;

        const isPaused = run.status === 'PAUSED';
        const isRunning = run.status === 'RUNNING';

        let statusClass = 'badge-offline';
        if (run.status === 'RUNNING') statusClass = 'badge-live';
        if (run.status === 'PAUSED') statusClass = 'badge';

        const activeJobsHtml = (run.active_jobs && run.active_jobs.length > 0)
          ? run.active_jobs.map(j => `
              <div class="job-row">
                <div class="job-name">
                  <span class="job-badge">${j.stage}/${j.job_id}</span>
                  <span>${j.name}</span>
                </div>
                <div style="color: var(--text-muted); font-size: 12px; display:flex; gap:12px;">
                  <span>PID: ${j.pid}</span>
                  <span>${j.elapsed_sec.toFixed(1)}s</span>
                  <button class="btn btn-danger" style="padding:2px 6px; font-size:11px;" onclick="killJob('${run.run_id}', '${j.job_id}')">Kill</button>
                </div>
              </div>
            `).join('')
          : '<div style="color: var(--text-muted); font-style: italic;">No active worker jobs currently executing</div>';

        return `
          <div class="run-card">
            <div class="run-card-header">
              <div>
                <div style="display:flex; align-items:center;">
                  <span class="run-info-title">${run.pipeline}</span>
                  <span class="run-id">${run.run_id}</span>
                </div>
                <div class="run-meta">
                  <span>Project: <strong>${run.project || 'default'}</strong></span>
                  <span>Status: <span class="badge ${statusClass}">${run.status}</span></span>
                  <span>Elapsed: <strong>${formatTime(prog.elapsed_sec || 0)}</strong></span>
                  ${run.peak_rss_mb > 0 ? `<span>Peak RSS: <strong>${run.peak_rss_mb.toFixed(1)} MB</strong></span>` : ''}
                </div>
              </div>
              <div class="run-actions">
                ${isRunning ? `<button class="btn btn-warning" onclick="sendControl('${run.run_id}', 'pause')">⏸️ Pause</button>` : ''}
                ${isPaused ? `<button class="btn btn-primary" onclick="sendControl('${run.run_id}', 'resume')">▶️ Resume</button>` : ''}
                <button class="btn btn-danger" onclick="sendControl('${run.run_id}', 'abort')">🛑 Abort</button>
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

    // Connect on page load
    connectWebSocket();
    setInterval(fetchRuns, 2000);
  </script>
</body>
</html>
)rawhtml";

} // namespace condor::hub
