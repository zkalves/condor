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
#include <iomanip>
#include <vector>
#include <string>
#include <cstdlib>
#include <CLI/CLI.hpp>

#include "condor/common.hpp"
#include "condor/config.hpp"
#include "condor/backend.hpp"
#include "condor/parser.hpp"
#include "condor/variable.hpp"
#include "condor/expression.hpp"
#include "condor/workspace.hpp"
#include "condor/scheduler.hpp"
#include "condor/logging.hpp"
#include "condor/menu.hpp"
#include "condor/remote_client.hpp"

#ifndef CONDOR_VERSION
#define CONDOR_VERSION "1.0.0"
#endif

using namespace condor;

int main(int argc, char** argv) {
    // Early command handler for GNU GPL legal queries ('condor --warranty' and 'condor --license') and version
    if (argc >= 2) {
        std::string_view arg(argv[1]);
        if (arg == "-v" || arg == "--version" || arg == "version") {
            std::cout << "condor " CONDOR_VERSION "\n";
            return 0;
        }
        if (arg == "--warranty" || arg == "warranty") {
            RuntimeMenu::print_warranty();
            return 0;
        }
        if (arg == "--license" || arg == "license") {
            RuntimeMenu::print_conditions();
            return 0;
        }
    }

    CLI::App app{"condor (Concurrent + Dependency + Orchestrator) - Modern C++20 Parallel Process and Grid Orchestrator"};
    app.set_version_flag("-v,--version", "condor " CONDOR_VERSION);

    std::string pipeline_config_path;
    std::string stage_name;
    size_t max_parallel_jobs{0};
    std::string base_output_dir;
    std::string run_dir;
    std::string backend_override;
    std::string on_failure_action_str;
    bool exit_zero_on_failure{false};
    int status_interval_sec{0};
    std::string output_mode_str;
    std::string path_style_str;
    bool relative_paths{false};
    bool absolute_paths{false};
    std::vector<std::string> defined_variables;
    std::vector<std::string> defined_resources;
    bool dry_run{false};
    bool no_interactive{false};
    bool verbose_logging{false};
    size_t count_multiplier{0};
    std::string rerun_base_dir;
    std::vector<std::string> stage_modes_cli;
    int grid_poll_interval_sec{0};
    std::string junit_xml_path;
    int kill_grace_period_sec{-1};
    size_t max_retained_runs{0};
    bool purge_successful_logs{false};
    std::string server_url;
    std::string server_token;
    std::string server_project;
    std::string server_owner;
    std::vector<std::string> server_tags;
    int server_poll_ms{0};
    std::string kill_run_path;

    // CLI Options per specification
    app.add_option("pipeline_file,--pipeline-config", pipeline_config_path, "Path to pipeline YAML configuration")
       ->check(CLI::ExistingFile);

    app.add_option("--kill-run", kill_run_path, "Emergency kill all running jobs in specified run directory (or latest)");

    app.add_option("--stage", stage_name, "Run only the specified stage (must match ^[a-zA-Z0-9_-]+$)");
    app.add_option("--max-parallel-jobs", max_parallel_jobs, "Maximum concurrent local jobs (default: hardware concurrency)");
    app.add_option("--count-multiplier", count_multiplier, "Multiplier for scalable job counts (default: 1)")
       ->check(CLI::PositiveNumber);
    app.add_option("--base-output-dir", base_output_dir, "Base folder for timestamped runs (default: ./condor_runs)");
    app.add_option("--run-dir", run_dir, "Run inside explicit fixed directory instead of generating timestamped directory");
    app.add_option("-b,--backend,--backend-override", backend_override, "Execution backend for pipeline (default: local or configured in YAML)");
    app.add_option("--on-failure-action", on_failure_action_str, "Override failure policy (abort, continue, stage_barrier)");
    app.add_flag("--exit-zero-on-failure", exit_zero_on_failure, "Force return code 0 even if jobs or stages fail");
    app.add_option("--status-interval-sec", status_interval_sec, "Interval in seconds to dump status summaries in CI mode (default: 30)");
    app.add_option("--output-mode", output_mode_str, "Terminal output mode (all, failures, summary)");
    app.add_option("--path-style", path_style_str, "Path representation inside runs (absolute, relative)");
    app.add_flag("--relative-paths", relative_paths, "Use relative paths inside runs (equivalent to --path-style=relative)");
    app.add_flag("--absolute-paths", absolute_paths, "Use absolute paths inside runs (equivalent to --path-style=absolute)");
    app.add_option("--define-variable", defined_variables, "Define or override configuration variables (KEY=VAL, repeatable)")
       ->allow_extra_args(false);
    app.add_option("--define-resource,--resource", defined_resources, "Define or override resource pool capacities (NAME=CAPACITY, repeatable)")
       ->allow_extra_args(false);
    app.add_option("--rerun", rerun_base_dir, "Base run directory to re-run from (supports incremental rerun of failed jobs)");
    app.add_option("--stage-mode", stage_modes_cli, "Override stage execution mode (STAGE:MODE where mode is full, failed_only, cached; repeatable)")
       ->allow_extra_args(false);
    app.add_option("--grid-poll-interval-sec", grid_poll_interval_sec, "Polling interval in seconds for cluster/grid backends (default: 10)");
    auto* junit_opt = app.add_option("--junit-xml", junit_xml_path, "Emit JUnit/xUnit XML test report to file (default: junit_report.xml)")
       ->expected(0, 1);
    app.add_option("--kill-grace-period-sec", kill_grace_period_sec, "Grace period in seconds between SIGTERM and SIGKILL during cancellation (default: 3)");
    auto* max_ret_opt = app.add_option("--max-retained-runs", max_retained_runs, "Maximum past run directories to retain in base_output_dir (0 = keep all)");
    app.add_flag("--purge-successful-logs", purge_successful_logs, "Delete stdout.log, stderr.err, and runner scripts of passed jobs to conserve disk space");
    app.add_flag("--dry-run", dry_run, "Validate stage names, DAG cycles, evaluate conditions, print plan, and exit");
    app.add_flag("--no-interactive", no_interactive, "Disable interactive stdin polling and ANSI progress redraws (CI mode)");
    app.add_flag("--verbose-logging", verbose_logging, "Enable detailed scheduler event logging");
    app.add_option("--server-url", server_url, "Central reporting server URL (e.g. ws://host:port/ws or http://host:port/api/v1/telemetry)");
    app.add_option("--server-token", server_token, "Authentication Bearer token for reporting server");
    app.add_option("--server-project", server_project, "Project identifier for central server reporting");
    app.add_option("--server-owner,--owner", server_owner, "Owner or user identifier for pipeline run reporting");
    app.add_option("--server-tags", server_tags, "Tags for central server reporting (repeatable)")
       ->allow_extra_args(false);
    app.add_option("--server-poll-ms", server_poll_ms, "Interval in milliseconds between real-time telemetry updates (default: 500)");
    app.add_flag_callback("--warranty", []() {
        RuntimeMenu::print_warranty();
        std::exit(0);
    }, "Display GNU General Public License warranty disclaimer");
    app.add_flag_callback("--license", []() {
        RuntimeMenu::print_conditions();
        std::exit(0);
    }, "Display GNU General Public License redistribution conditions");
    app.add_flag_callback("--list-backends", []() {
        std::cout << "====================== CONDOR AVAILABLE BACKENDS ======================\n";
        std::cout << "  " << std::left << std::setw(14) << "local"
                  << "[local]     Direct local POSIX process execution\n";
        auto backends = BackendRegistry::discover_backends();
        std::vector<std::string> names;
        for (const auto& [name, _] : backends) {
            if (name != "local") names.push_back(name);
        }
        std::sort(names.begin(), names.end());
        for (const auto& name : names) {
            const auto& b = backends.at(name);
            std::cout << "  " << std::left << std::setw(14) << name
                      << "[" << to_string(b.type) << "]  "
                      << (b.description.empty() ? "(Custom grid backend)" : b.description) << "\n";
        }
        std::cout << "=======================================================================\n";
        std::exit(0);
    }, "List all available execution backends discovered on system and exit");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    if (!kill_run_path.empty()) {
        std::error_code ec;
        std::string base_dir = base_output_dir.empty() ? "./condor_runs" : base_output_dir;
        std::filesystem::path target = kill_run_path;
        if (!std::filesystem::exists(target, ec)) {
            auto candidate = std::filesystem::path(base_dir) / kill_run_path;
            if (std::filesystem::exists(candidate, ec)) {
                target = candidate;
            }
        }
        if (std::filesystem::is_symlink(target, ec)) {
            auto resolved = std::filesystem::canonical(target, ec);
            if (!ec) target = resolved;
        }
        std::cout << "[CONDOR] Emergency kill requested for run directory: " << target.string() << std::endl;
        WorkspaceManager::kill_run(target);
        return 0;
    }

    if (pipeline_config_path.empty()) {
        std::cerr << "Error: pipeline YAML configuration file is required.\n"
                  << "Run 'condor --help' for usage details." << std::endl;
        return 1;
    }

    // Set logger verbosity
    Logger::instance().set_verbose(verbose_logging);

    // Validate --stage argument early if provided
    if (!stage_name.empty() && !is_valid_identifier(stage_name)) {
        std::cerr << "Configuration error: Stage name '" << stage_name
                  << "' violates identifier rules (must match ^[a-zA-Z0-9_-]+$)." << std::endl;
        return 1;
    }

    PipelineConfig config;
    try {
        config = YamlParser::parse_file(pipeline_config_path, count_multiplier);
    } catch (const ConfigurationError& e) {
        std::cerr << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error loading configuration: " << e.what() << std::endl;
        return 1;
    }

    // Automatically set CONDOR_CWD if not explicitly set
    if (!config.variables.has("CONDOR_CWD")) {
        config.variables.set("CONDOR_CWD", std::filesystem::current_path().string());
    }

    // Apply CLI overrides to variables
    try {
        for (const auto& def : defined_variables) {
            config.variables.set_from_definition(def);
        }
    } catch (const ConfigurationError& e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    // Apply CLI overrides to resources
    for (const auto& rdef : defined_resources) {
        auto eq_pos = rdef.find('=');
        if (eq_pos == std::string::npos || eq_pos == 0 || eq_pos == rdef.size() - 1) {
            std::cerr << "Configuration error: --resource expects NAME=CAPACITY (e.g. vcs=4), got: " << rdef << std::endl;
            return 1;
        }
        std::string rname = rdef.substr(0, eq_pos);
        std::string rcap_str = rdef.substr(eq_pos + 1);
        if (!is_valid_identifier(rname)) {
            std::cerr << "Configuration error: Resource name '" << rname
                      << "' violates identifier rules (must match ^[a-zA-Z0-9_-]+$)." << std::endl;
            return 1;
        }
        try {
            int cap = std::stoi(rcap_str);
            if (cap <= 0) {
                std::cerr << "Configuration error: Resource '" << rname
                          << "' capacity must be greater than 0, got: " << cap << std::endl;
                return 1;
            }
            config.resources[rname] = cap;
        } catch (...) {
            std::cerr << "Configuration error: Invalid resource capacity in '" << rdef << "'." << std::endl;
            return 1;
        }
    }
    if (!defined_resources.empty()) {
        try {
            YamlParser::validate(config);
        } catch (const ConfigurationError& e) {
            std::cerr << e.what() << std::endl;
            return 1;
        }
    }

    // Apply CLI overrides to settings
    if (!stage_name.empty()) {
        if (!config.find_stage(stage_name)) {
            std::cerr << "Configuration error: Specified stage '" << stage_name
                      << "' not found in pipeline." << std::endl;
            return 1;
        }
        config.settings.stage_filter = stage_name;
    }
    if (max_parallel_jobs > 0) {
        config.settings.max_parallel_jobs = max_parallel_jobs;
    }
    if (!base_output_dir.empty()) {
        config.settings.base_output_dir = base_output_dir;
    }
    if (!run_dir.empty()) {
        config.settings.run_dir = run_dir;
    }
    if (!backend_override.empty()) {
        config.settings.backend_override = backend_override;
    }
    if (!on_failure_action_str.empty()) {
        try {
            config.settings.on_failure_action = parse_failure_action(on_failure_action_str);
        } catch (const std::exception& e) {
            std::cerr << e.what() << std::endl;
            return 1;
        }
    }
    if (exit_zero_on_failure) {
        config.settings.exit_zero_on_failure = true;
    }
    if (status_interval_sec > 0) {
        config.settings.status_interval_sec = status_interval_sec;
    }
    if (!output_mode_str.empty()) {
        try {
            config.settings.output_mode = parse_output_mode(output_mode_str);
        } catch (const std::exception& e) {
            std::cerr << e.what() << std::endl;
            return 1;
        }
    }
    config.variables.set("output_mode", to_string(config.settings.output_mode));
    config.variables.set("CONDOR_OUTPUT_MODE", to_string(config.settings.output_mode));

    if (!path_style_str.empty()) {
        try {
            config.settings.path_style = parse_path_style(path_style_str);
        } catch (const std::exception& e) {
            std::cerr << e.what() << std::endl;
            return 1;
        }
    } else if (relative_paths) {
        config.settings.path_style = PathStyle::RELATIVE;
    } else if (absolute_paths) {
        config.settings.path_style = PathStyle::ABSOLUTE;
    }
    config.variables.set("path_style", to_string(config.settings.path_style));
    config.variables.set("CONDOR_PATH_STYLE", to_string(config.settings.path_style));

    if (!rerun_base_dir.empty()) {
        config.settings.rerun_base_dir = rerun_base_dir;
    }

    for (const auto& sm : stage_modes_cli) {
        auto colon_pos = sm.find(':');
        if (colon_pos == std::string::npos) {
            std::cerr << "Configuration error: --stage-mode expects format <stage>:<mode> (e.g. sim_tests:failed_only), got: "
                      << sm << std::endl;
            return 1;
        }
        std::string s_name = sm.substr(0, colon_pos);
        std::string s_mode_str = sm.substr(colon_pos + 1);
        try {
            config.settings.stage_modes[s_name] = parse_stage_execution_mode(s_mode_str);
        } catch (const std::exception& e) {
            std::cerr << "Configuration error for --stage-mode: " << e.what() << std::endl;
            return 1;
        }
    }

    if (grid_poll_interval_sec > 0) {
        config.settings.grid_poll_interval_sec = grid_poll_interval_sec;
    }

    if (junit_opt->count() > 0) {
        config.settings.emit_junit_xml = true;
        if (!junit_xml_path.empty()) {
            config.settings.junit_xml_path = junit_xml_path;
        }
    }

    if (kill_grace_period_sec >= 0) {
        config.settings.kill_grace_period_sec = kill_grace_period_sec;
    }
    if (max_ret_opt->count() > 0) {
        config.settings.max_retained_runs = max_retained_runs;
    }
    if (purge_successful_logs) {
        config.settings.purge_successful_logs = true;
    }
    if (!server_url.empty()) {
        config.settings.server.enabled = true;
        config.settings.server.url = server_url;
    }
    if (!server_token.empty()) {
        config.settings.server.token = server_token;
    }
    if (!server_project.empty()) {
        config.settings.server.project = server_project;
    }
    if (!server_owner.empty()) {
        config.settings.server.owner = server_owner;
    }
    if (config.settings.server.owner.empty()) {
        const char* u = std::getenv("USER");
        if (!u) u = std::getenv("LOGNAME");
        config.settings.server.owner = u ? u : "anonymous";
    }
    if (!server_tags.empty()) {
        config.settings.server.tags = server_tags;
    }
    if (server_poll_ms > 0) {
        config.settings.server.poll_interval_ms = server_poll_ms;
    }

    config.settings.dry_run = dry_run;
    config.settings.no_interactive = no_interactive;
    config.settings.verbose_logging = verbose_logging;

    // Dry Run Mode
    if (config.settings.dry_run) {
        std::cout << "================ CONDOR DRY-RUN EXECUTION PLAN ================\n"
                  << "Pipeline Name:       " << config.name << "\n"
                  << "Max Parallel Jobs:   " << config.settings.max_parallel_jobs << "\n"
                  << "Base Output Dir:     " << config.settings.base_output_dir << "\n"
                  << "On Failure Action:   " << to_string(config.settings.on_failure_action) << "\n"
                  << "Exit Zero On Fail:   " << (config.settings.exit_zero_on_failure ? "true" : "false") << "\n"
                  << "Output Mode:         " << to_string(config.settings.output_mode) << "\n"
                  << "Path Style:          " << to_string(config.settings.path_style) << "\n";
        if (!config.settings.rerun_base_dir.empty()) {
            std::cout << "Rerun Base Dir:      " << config.settings.rerun_base_dir << "\n";
        }
        if (config.settings.emit_junit_xml) {
            std::cout << "JUnit XML Report:    "
                      << (!config.settings.junit_xml_path.empty() ? config.settings.junit_xml_path : "<run_dir>/artifacts/junit_report.xml")
                      << "\n";
        }
        if (config.settings.kill_grace_period_sec != 3) {
            std::cout << "Kill Grace Period:   " << config.settings.kill_grace_period_sec << "s\n";
        }
        if (config.settings.max_retained_runs > 0) {
            std::cout << "Max Retained Runs:   " << config.settings.max_retained_runs << "\n";
        }
        if (config.settings.purge_successful_logs) {
            std::cout << "Purge Passed Logs:   true\n";
        }
        if (config.settings.server.enabled) {
            std::cout << "Remote Server:       " << config.settings.server.url << "\n";
            if (!config.settings.server.project.empty()) {
                std::cout << "Server Project:      " << config.settings.server.project << "\n";
            }
        }
        std::cout << "----------------------------------------------------------------\n"
                  << "Defined Variables:\n";
        for (const auto& [k, v] : config.variables.variables()) {
            std::cout << "  " << k << " = \"" << v << "\" (Exported as "
                      << VariableManager::to_env_key(k) << ")\n";
        }
        if (!config.resources.empty()) {
            std::cout << "----------------------------------------------------------------\n"
                      << "Resource Pools:\n";
            for (const auto& [rname, cap] : config.resources) {
                std::cout << "  " << rname << " = " << cap << " tokens\n";
            }
        }
        std::cout << "----------------------------------------------------------------\n"
                  << "Stages & Jobs Plan:\n";

        for (const auto& stage : config.stages) {
            if (!config.settings.stage_filter.empty() && config.settings.stage_filter != stage.name) {
                continue;
            }
            bool stage_enabled = ExpressionEvaluator::evaluate(stage.enabled_if, config.variables);
            StageExecutionMode smode = stage.mode;
            auto mode_it = config.settings.stage_modes.find(stage.name);
            if (mode_it != config.settings.stage_modes.end()) {
                smode = mode_it->second;
            }
            std::cout << "\n[STAGE] " << stage.name
                      << " (Mode: " << to_string(smode)
                      << ", Enabled: " << (stage_enabled ? "YES" : "NO - SKIPPED")
                      << ", Allow Failure: " << (stage.allow_failure ? "true" : "false");
            if (!stage.backend.empty()) {
                std::cout << ", Backend: " << stage.backend;
            }
            std::cout << ")\n";
            if (!stage.depends_on.empty()) {
                std::cout << "  Depends On: ";
                for (size_t i = 0; i < stage.depends_on.size(); ++i) {
                    std::cout << stage.depends_on[i] << (i + 1 < stage.depends_on.size() ? ", " : "\n");
                }
            }

            for (const auto& job : stage.jobs) {
                bool job_enabled = stage_enabled && ExpressionEvaluator::evaluate(job.enabled_if, config.variables);
                std::string interp_cmd = config.variables.interpolate(job.command);
                std::string bname;
                if (!config.settings.backend_override.empty()) {
                    bname = config.settings.backend_override;
                } else if (!job.backend.empty()) {
                    bname = job.backend;
                } else if (!stage.backend.empty()) {
                    bname = stage.backend;
                } else if (!config.settings.backend.empty()) {
                    bname = config.settings.backend;
                } else {
                    bname = "local";
                }
                if (bname.empty()) bname = "local";

                std::string job_path_str = (config.settings.path_style == PathStyle::RELATIVE)
                                            ? "stages/" + stage.name + "/" + job.job_id + "/"
                                            : "<run_dir>/stages/" + stage.name + "/" + job.job_id + "/";
                std::cout << "  - [" << job.job_id << "] " << job.name << "\n"
                          << "      Backend: " << bname << "\n"
                          << "      Command: " << interp_cmd << "\n";
                if (!job.working_dir.empty()) {
                    std::cout << "      WorkDir: " << job.working_dir << "\n";
                }
                if (job.timeout_sec > 0) {
                    std::cout << "      Timeout: " << job.timeout_sec << "s\n";
                }
                if (job.retry > 0) {
                    std::cout << "      Retry:   " << job.retry << " (delay: " << job.retry_delay_sec << "s)\n";
                }
                if (job.container.has_value()) {
                    std::cout << "      Container: " << job.container->backend << " (" << job.container->image << ")\n";
                }
                if (!job.resources.empty()) {
                    std::cout << "      Resources: ";
                    size_t ri = 0;
                    for (const auto& [rname, req] : job.resources) {
                        std::cout << rname << "=" << req << (++ri < job.resources.size() ? ", " : "");
                    }
                    std::cout << "\n";
                }
                std::cout << "      Status:  " << (job_enabled ? "QUEUED" : "SKIPPED") << "\n"
                          << "      Path:    " << job_path_str << "\n";
            }
        }
        std::cout << "================================================================\n";
        return 0;
    }

    // Normal Execution Mode
    WorkspaceManager workspace(config.settings.base_output_dir, config.settings.run_dir);
    try {
        workspace.initialize();
    } catch (const std::exception& e) {
        std::cerr << "Initialization error: " << e.what() << std::endl;
        return 1;
    }

    // Output interactive legal notice (GNU General Public License §5)
    if (!config.settings.no_interactive && isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)) {
        RuntimeMenu::print_notice();
        std::cout << std::endl;
    }

    std::cout << "[CONDOR] Launching pipeline '" << config.name << "'\n"
              << "[CONDOR] Active run directory: " << workspace.run_dir() << "\n"
              << "[CONDOR] Max concurrent slots: " << config.settings.max_parallel_jobs << "\n"
              << "[CONDOR] Available keys:      [m] Runtime Menu | [Ctrl+C] Cancel / Abort\n"
              << "[CONDOR] Emergency kill:       " << workspace.kill_script_path().string() << "\n"
              << "                         (or: pkill -f '" << workspace.run_dir().string() << "')\n";

    std::unique_ptr<RemoteServerClient> remote_client;
    if (config.settings.server.enabled && !config.settings.server.url.empty()) {
        std::cout << "[CONDOR] Remote telemetry & control: " << config.settings.server.url << "\n";
        remote_client = std::make_unique<RemoteServerClient>(
            config.settings.server,
            workspace.run_id(),
            config.name
        );
    }
    std::cout << std::endl;

    Scheduler scheduler(config, workspace);
    if (remote_client) {
        scheduler.set_remote_client(remote_client.get());
    }
    bool success = scheduler.run();

    if (config.settings.exit_zero_on_failure) {
        return 0;
    }
    return success ? 0 : 1;
}
