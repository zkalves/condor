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

using namespace condor;

int main(int argc, char** argv) {
    // Early command handler for GNU GPL legal queries ('condor --warranty' and 'condor --license')
    if (argc >= 2) {
        std::string_view arg(argv[1]);
        if (arg == "--warranty" || arg == "warranty" || (arg == "show" && argc >= 3 && std::string_view(argv[2]) == "w")) {
            RuntimeMenu::print_warranty();
            return 0;
        }
        if (arg == "--license" || arg == "license" || arg == "--copying" || (arg == "show" && argc >= 3 && std::string_view(argv[2]) == "c")) {
            RuntimeMenu::print_conditions();
            return 0;
        }
    }

    CLI::App app{"condor (Concurrent + Dependency + Orchestrator) - Modern C++20 Parallel Process and Grid Orchestrator"};

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
    bool dry_run{false};
    bool no_interactive{false};
    bool verbose_logging{false};

    // CLI Options per specification
    app.add_option("pipeline_file,--pipeline-config", pipeline_config_path, "Path to pipeline YAML configuration")
       ->check(CLI::ExistingFile);

    app.add_option("--stage", stage_name, "Run only the specified stage (must match ^[a-zA-Z0-9_-]+$)");
    app.add_option("--max-parallel-jobs", max_parallel_jobs, "Maximum concurrent local jobs (default: hardware concurrency)");
    app.add_option("--base-output-dir", base_output_dir, "Base folder for timestamped runs (default: ./condor_runs)");
    app.add_option("--run-dir", run_dir, "Run inside explicit fixed directory instead of generating timestamped directory");
    app.add_option("-b,--backend,--backend-override", backend_override, "Execution backend for pipeline (default: local or configured in YAML)");
    app.add_option("--on-failure-action", on_failure_action_str, "Override failure policy (abort, continue, stage_barrier)");
    app.add_flag("--exit-zero-on-failure", exit_zero_on_failure, "Force return code 0 even if jobs or stages fail");
    app.add_option("--status-interval-sec", status_interval_sec, "Interval in seconds to dump status summaries in CI mode (default: 30)");
    app.add_option("--output-mode,--terminal-output", output_mode_str, "Terminal output mode (all, failures, summary)");
    app.add_option("--path-style,--path-mode", path_style_str, "Path representation inside runs (absolute, relative)");
    app.add_flag("--relative-paths", relative_paths, "Use relative paths inside runs (equivalent to --path-style=relative)");
    app.add_flag("--absolute-paths", absolute_paths, "Use absolute paths inside runs (equivalent to --path-style=absolute)");
    app.add_option("--define-variable", defined_variables, "Define or override configuration variables (KEY=VAL, repeatable)");
    app.add_flag("--dry-run", dry_run, "Validate stage names, DAG cycles, evaluate conditions, print plan, and exit");
    app.add_flag("--no-interactive", no_interactive, "Disable interactive stdin polling and ANSI progress redraws (CI mode)");
    app.add_flag("--verbose-logging", verbose_logging, "Enable detailed scheduler event logging");
    app.add_flag_callback("--warranty", []() {
        RuntimeMenu::print_warranty();
        std::exit(0);
    }, "Display GNU General Public License warranty disclaimer");
    app.add_flag_callback("--license,--copying", []() {
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

    CLI11_PARSE(app, argc, argv);

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
        config = YamlParser::parse_file(pipeline_config_path);
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
                  << "Path Style:          " << to_string(config.settings.path_style) << "\n"
                  << "----------------------------------------------------------------\n"
                  << "Defined Variables:\n";
        for (const auto& [k, v] : config.variables.variables()) {
            std::cout << "  " << k << " = \"" << v << "\" (Exported as "
                      << VariableManager::to_env_key(k) << ")\n";
        }
        std::cout << "----------------------------------------------------------------\n"
                  << "Stages & Jobs Plan:\n";

        for (const auto& stage : config.stages) {
            if (!config.settings.stage_filter.empty() && config.settings.stage_filter != stage.name) {
                continue;
            }
            bool stage_enabled = ExpressionEvaluator::evaluate(stage.enabled_if, config.variables);
            std::cout << "\n[STAGE] " << stage.name
                      << " (Enabled: " << (stage_enabled ? "YES" : "NO - SKIPPED")
                      << ", Allow Failure: " << (stage.allow_failure ? "true" : "false") << ")\n";
            if (!stage.depends_on.empty()) {
                std::cout << "  Depends On: ";
                for (size_t i = 0; i < stage.depends_on.size(); ++i) {
                    std::cout << stage.depends_on[i] << (i + 1 < stage.depends_on.size() ? ", " : "\n");
                }
            }

            for (const auto& job : stage.jobs) {
                bool job_enabled = stage_enabled && ExpressionEvaluator::evaluate(job.enabled_if, config.variables);
                std::string interp_cmd = config.variables.interpolate(job.command);
                std::string bname = !config.settings.backend_override.empty()
                                    ? config.settings.backend_override
                                    : (!config.settings.backend.empty() ? config.settings.backend : config.settings.default_backend);
                if (bname.empty()) bname = "local";

                std::string job_path_str = (config.settings.path_style == PathStyle::RELATIVE)
                                            ? stage.name + "/" + job.job_id + "/"
                                            : "<run_dir>/" + stage.name + "/" + job.job_id + "/";
                std::cout << "  - [" << job.job_id << "] " << job.name << "\n"
                          << "      Backend: " << bname << "\n"
                          << "      Command: " << interp_cmd << "\n"
                          << "      Status:  " << (job_enabled ? "QUEUED" : "SKIPPED") << "\n"
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
              << std::endl;

    Scheduler scheduler(config, workspace);
    bool success = scheduler.run();

    if (config.settings.exit_zero_on_failure) {
        return 0;
    }
    return success ? 0 : 1;
}
