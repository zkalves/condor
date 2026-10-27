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

#include "condor/parser.hpp"
#include "condor/backend.hpp"
#include "condor/logging.hpp"
#include <yaml-cpp/yaml.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <unordered_set>
#include <thread>
#include <filesystem>

namespace condor {

static std::filesystem::path resolve_include_path(const std::string& inc_path, const std::string& base_dir) {
    std::filesystem::path p(inc_path);
    if (p.is_absolute()) {
        return p;
    }
    // Try relative to base_dir
    std::filesystem::path cand = std::filesystem::path(base_dir) / p;
    if (std::filesystem::exists(cand)) {
        return cand;
    }
    // Try relative to current working directory
    if (std::filesystem::exists(p)) {
        return p;
    }
    return cand;
}

static void parse_job_hooks(const YAML::Node& jnode, JobConfig& job) {
    if (jnode["hooks"] && jnode["hooks"].IsMap()) {
        const auto& jh = jnode["hooks"];
        if (jh["pre"])    job.hooks.pre    = jh["pre"].as<std::string>();
        if (jh["post"])   job.hooks.post   = jh["post"].as<std::string>();
        if (jh["always"]) job.hooks.always = jh["always"].as<bool>();
    }
}

static void parse_job_deps(const YAML::Node& jnode, JobConfig& job) {
    if (jnode["depends_on"]) {
        if (jnode["depends_on"].IsSequence()) {
            for (const auto& dep : jnode["depends_on"]) {
                job.depends_on.push_back(dep.as<std::string>());
            }
        } else if (jnode["depends_on"].IsScalar()) {
            job.depends_on.push_back(jnode["depends_on"].as<std::string>());
        }
    }
}

static JobDefaults parse_defaults_node(const YAML::Node& node) {
    JobDefaults defs;
    if (!node || !node.IsMap()) return defs;
    if (node["command"]) defs.command = node["command"].as<std::string>();
    if (node["backend"]) defs.backend = node["backend"].as<std::string>();
    if (node["args"])    defs.args    = node["args"].as<std::string>();
    if (node["hooks"] && node["hooks"].IsMap()) {
        const auto& h = node["hooks"];
        if (h["pre"])    defs.hooks.pre    = h["pre"].as<std::string>();
        if (h["post"])   defs.hooks.post   = h["post"].as<std::string>();
        if (h["always"]) defs.hooks.always = h["always"].as<bool>();
    }
    if (node["variables"] && node["variables"].IsMap()) {
        for (auto it = node["variables"].begin(); it != node["variables"].end(); ++it) {
            defs.variables[it->first.as<std::string>()] = it->second.as<std::string>();
        }
    }
    return defs;
}

static void merge_defaults(JobDefaults& target, const JobDefaults& override_defs) {
    if (!override_defs.command.empty()) target.command = override_defs.command;
    if (!override_defs.backend.empty()) target.backend = override_defs.backend;
    if (!override_defs.args.empty())    target.args    = override_defs.args;
    if (!override_defs.hooks.pre.empty()) target.hooks.pre = override_defs.hooks.pre;
    if (!override_defs.hooks.post.empty()) target.hooks.post = override_defs.hooks.post;
    if (override_defs.hooks.always) target.hooks.always = override_defs.hooks.always;
    for (const auto& [k, v] : override_defs.variables) {
        target.variables[k] = v;
    }
}

static void parse_job_entry(const YAML::Node& jnode, StageConfig& stage, int& job_counter, const JobDefaults& defaults) {
    std::string base_name = jnode["name"] ? jnode["name"].as<std::string>() : "job";
    std::string command = jnode["command"] ? jnode["command"].as<std::string>() : defaults.command;
    std::string backend = jnode["backend"] ? jnode["backend"].as<std::string>() : defaults.backend;
    std::string enabled_if = jnode["enabled_if"] ? jnode["enabled_if"].as<std::string>() : "";
    bool allow_failure = jnode["allow_failure"] ? jnode["allow_failure"].as<bool>() : false;

    bool has_count = jnode["count"].IsDefined();
    bool has_seed = jnode["seed"].IsDefined();
    bool has_seeds = jnode["seeds"].IsDefined();

    // Mutual exclusivity checks
    if (has_count && (has_seed || has_seeds)) {
        throw ConfigurationError("Configuration error: Job '" + base_name +
                                 "' cannot specify both 'count' and 'seed'/'seeds' (they are mutually exclusive).");
    }
    if (has_seed && has_seeds) {
        throw ConfigurationError("Configuration error: Job '" + base_name +
                                 "' cannot specify both 'seed' and 'seeds'.");
    }

    auto apply_job_defaults = [&](JobConfig& job) {
        // Inherit default variables
        for (const auto& [k, v] : defaults.variables) {
            job.variables[k] = v;
        }
        job.variables["JOB_NAME"] = job.name;
        job.variables["TEST_NAME"] = base_name;
        if (jnode["args"]) {
            job.variables["ARGS"] = jnode["args"].as<std::string>();
        } else if (!defaults.args.empty()) {
            job.variables["ARGS"] = defaults.args;
        }
        if (jnode["variables"] && jnode["variables"].IsMap()) {
            for (auto it = jnode["variables"].begin(); it != jnode["variables"].end(); ++it) {
                job.variables[it->first.as<std::string>()] = it->second.as<std::string>();
            }
        }
        if (job.hooks.pre.empty() && !defaults.hooks.pre.empty()) {
            job.hooks.pre = defaults.hooks.pre;
        }
        if (job.hooks.post.empty() && !defaults.hooks.post.empty()) {
            job.hooks.post = defaults.hooks.post;
            job.hooks.always = defaults.hooks.always;
        }
    };

    if (has_seeds) {
        std::vector<int> seed_list;
        if (jnode["seeds"].IsSequence()) {
            for (const auto& s : jnode["seeds"]) {
                seed_list.push_back(s.as<int>());
            }
        } else if (jnode["seeds"].IsScalar()) {
            seed_list.push_back(jnode["seeds"].as<int>());
        }

        for (int s_val : seed_list) {
            JobConfig job;
            std::ostringstream id_ss;
            id_ss << std::setw(4) << std::setfill('0') << job_counter++;
            job.job_id = id_ss.str();
            job.stage_name = stage.name;
            job.name = base_name + "_seed_" + std::to_string(s_val);
            job.command = command;
            job.backend = backend;
            job.enabled_if = enabled_if;
            job.allow_failure = allow_failure;
            parse_job_deps(jnode, job);
            parse_job_hooks(jnode, job);
            job.seed = s_val;
            apply_job_defaults(job);
            job.variables["SEED"] = std::to_string(s_val);
            stage.jobs.push_back(job);
        }
    } else if (has_count) {
        int count_val = jnode["count"].as<int>();
        if (count_val <= 0) {
            throw ConfigurationError("Configuration error: Job '" + base_name + "' count must be greater than 0.");
        }

        for (int i = 0; i < count_val; ++i) {
            JobConfig job;
            std::ostringstream id_ss;
            id_ss << std::setw(4) << std::setfill('0') << job_counter++;
            job.job_id = id_ss.str();
            job.stage_name = stage.name;
            job.name = base_name + "_" + std::to_string(i);
            job.command = command;
            job.backend = backend;
            job.enabled_if = enabled_if;
            job.allow_failure = allow_failure;
            parse_job_deps(jnode, job);
            parse_job_hooks(jnode, job);
            job.count = count_val;
            apply_job_defaults(job);
            job.variables["INDEX"] = std::to_string(i);
            job.variables["SEED"] = std::to_string(i + 1);
            stage.jobs.push_back(job);
        }
    } else if (has_seed) {
        int s_val = jnode["seed"].as<int>();
        JobConfig job;
        std::ostringstream id_ss;
        id_ss << std::setw(4) << std::setfill('0') << job_counter++;
        job.job_id = id_ss.str();
        job.stage_name = stage.name;
        job.name = base_name;
        job.command = command;
        job.backend = backend;
        job.enabled_if = enabled_if;
        job.allow_failure = allow_failure;
        parse_job_deps(jnode, job);
        parse_job_hooks(jnode, job);
        job.seed = s_val;
        apply_job_defaults(job);
        job.variables["SEED"] = std::to_string(s_val);
        stage.jobs.push_back(job);
    } else {
        JobConfig job;
        std::ostringstream id_ss;
        id_ss << std::setw(4) << std::setfill('0') << job_counter++;
        job.job_id = id_ss.str();
        job.stage_name = stage.name;
        job.name = jnode["name"] ? jnode["name"].as<std::string>() : ("job_" + job.job_id);
        job.command = command;
        job.backend = backend;
        job.enabled_if = enabled_if;
        job.allow_failure = allow_failure;
        parse_job_deps(jnode, job);
        parse_job_hooks(jnode, job);
        apply_job_defaults(job);
        stage.jobs.push_back(job);
    }
}

void YamlParser::populate_default_backends(PipelineConfig& config, const std::string& base_dir) {
    // Built-in Local POSIX backend (only internal execution engine)
    BackendConfig local_b;
    local_b.name = "local";
    local_b.type = BackendType::LOCAL;
    local_b.description = "Direct local POSIX process execution";
    config.backends["local"] = local_b;

    // Discover external backend definition files dynamically from search paths
    // (Grid engines such as Slurm, SGE/Grid Engine, IBM Spectrum LSF, PBS Pro, TORQUE, Apache Spark)
    auto discovered = BackendRegistry::discover_backends(base_dir);
    for (auto& [bname, bcfg] : discovered) {
        if (config.backends.find(bname) == config.backends.end()) {
            config.backends[bname] = std::move(bcfg);
        }
    }
}

PipelineConfig YamlParser::parse_string(std::string_view yaml_content, const std::string& base_dir) {
    YAML::Node root;
    try {
        root = YAML::Load(std::string(yaml_content));
    } catch (const YAML::Exception& e) {
        throw ConfigurationError("YAML parsing error: " + std::string(e.what()));
    }

    if (!root || !root.IsMap()) {
        throw ConfigurationError("Invalid YAML configuration: root must be a mapping.");
    }

    PipelineConfig config;
    populate_default_backends(config, base_dir);

    if (!base_dir.empty()) {
        std::error_code ec;
        std::filesystem::path abs_base = std::filesystem::absolute(base_dir, ec);
        std::string base_str = ec ? base_dir : abs_base.string();
        config.variables.set("CONDOR_PIPELINE_DIR", base_str);
        config.variables.set("PIPELINE_DIR", base_str);
    }

    if (root["name"]) {
        config.name = root["name"].as<std::string>();
    }

    // Top-level includes
    std::vector<std::string> top_includes;
    if (root["include"]) {
        if (root["include"].IsSequence()) {
            for (const auto& it : root["include"]) top_includes.push_back(it.as<std::string>());
        } else {
            top_includes.push_back(root["include"].as<std::string>());
        }
    }
    if (root["includes"] && root["includes"].IsSequence()) {
        for (const auto& it : root["includes"]) top_includes.push_back(it.as<std::string>());
    }
    config.includes = top_includes;

    // Parse global pipeline defaults
    if (root["defaults"] && root["defaults"].IsMap()) {
        config.defaults = parse_defaults_node(root["defaults"]);
    }

    // Parse variables
    if (root["variables"] && root["variables"].IsMap()) {
        for (auto it = root["variables"].begin(); it != root["variables"].end(); ++it) {
            std::string key = it->first.as<std::string>();
            std::string val;
            if (it->second.IsScalar()) {
                val = it->second.as<std::string>();
            } else {
                std::ostringstream ss;
                ss << it->second;
                val = ss.str();
            }
            config.variables.set(key, val);
        }
    }

    // Parse settings
    if (root["settings"] && root["settings"].IsMap()) {
        const auto& s = root["settings"];
        if (s["max_parallel_jobs"]) {
            config.settings.max_parallel_jobs = s["max_parallel_jobs"].as<size_t>();
        }
        if (s["base_output_dir"]) {
            config.settings.base_output_dir = s["base_output_dir"].as<std::string>();
        }
        if (s["on_failure_action"]) {
            config.settings.on_failure_action = parse_failure_action(s["on_failure_action"].as<std::string>());
        }
        if (s["exit_zero_on_failure"]) {
            config.settings.exit_zero_on_failure = s["exit_zero_on_failure"].as<bool>();
        }
        if (s["status_interval_sec"]) {
            config.settings.status_interval_sec = s["status_interval_sec"].as<int>();
        }
        if (s["default_backend"]) {
            config.settings.default_backend = s["default_backend"].as<std::string>();
        }
        if (s["output_mode"]) {
            config.settings.output_mode = parse_output_mode(s["output_mode"].as<std::string>());
        } else if (s["terminal_output"]) {
            config.settings.output_mode = parse_output_mode(s["terminal_output"].as<std::string>());
        }
        if (s["path_style"]) {
            config.settings.path_style = parse_path_style(s["path_style"].as<std::string>());
        } else if (s["path_mode"]) {
            config.settings.path_style = parse_path_style(s["path_mode"].as<std::string>());
        } else if (s["relative_paths"]) {
            config.settings.path_style = s["relative_paths"].as<bool>() ? PathStyle::RELATIVE : PathStyle::ABSOLUTE;
        }
    }

    if (config.settings.max_parallel_jobs == 0) {
        size_t cores = std::thread::hardware_concurrency();
        config.settings.max_parallel_jobs = (cores > 0) ? cores : 4;
    }

    // Parse custom backends
    if (root["backends"] && root["backends"].IsMap()) {
        for (auto it = root["backends"].begin(); it != root["backends"].end(); ++it) {
            std::string bname = it->first.as<std::string>();
            const auto& bnode = it->second;

            BackendConfig bconfig;
            bconfig.name = bname;

            std::string btype = bnode["type"] ? bnode["type"].as<std::string>() : "template";
            if (btype == "local") {
                bconfig.type = BackendType::LOCAL;
            } else {
                bconfig.type = BackendType::TEMPLATE;
            }

            if (bnode["description"]) bconfig.description = bnode["description"].as<std::string>();
            if (bnode["submit_command"]) bconfig.submit_command = bnode["submit_command"].as<std::string>();
            if (bnode["poll_command"])   bconfig.poll_command   = bnode["poll_command"].as<std::string>();
            if (bnode["cancel_command"]) bconfig.cancel_command = bnode["cancel_command"].as<std::string>();
            if (bnode["job_id_regex"])   bconfig.job_id_regex   = bnode["job_id_regex"].as<std::string>();
            if (bnode["success_pattern"]) bconfig.success_pattern = bnode["success_pattern"].as<std::string>();
            if (bnode["failure_pattern"]) bconfig.failure_pattern = bnode["failure_pattern"].as<std::string>();
            if (bnode["running_pattern"]) bconfig.running_pattern = bnode["running_pattern"].as<std::string>();
            if (bnode["empty_poll_means_done"]) bconfig.empty_poll_means_done = bnode["empty_poll_means_done"].as<bool>();

            config.backends[bname] = bconfig;
        }
    }

    // Parse global pipeline hooks
    if (root["hooks"] && root["hooks"].IsMap()) {
        const auto& h = root["hooks"];
        if (h["pre"])    config.hooks.pre    = h["pre"].as<std::string>();
        if (h["post"])   config.hooks.post   = h["post"].as<std::string>();
        if (h["always"]) config.hooks.always = h["always"].as<bool>();
    }

    // Parse stages
    if (!root["stages"] || !root["stages"].IsSequence()) {
        throw ConfigurationError("Configuration error: 'stages' list is required in pipeline YAML.");
    }

    for (size_t si = 0; si < root["stages"].size(); ++si) {
        int job_counter = 1; // Job ID is sequential per stage (0001, 0002, ...)
        const auto& snode = root["stages"][si];
        if (!snode.IsMap() || !snode["name"]) {
            throw ConfigurationError("Configuration error: Each stage must be a map with a 'name'.");
        }

        StageConfig stage;
        stage.name = snode["name"].as<std::string>();
        stage.defaults = config.defaults;
        if (snode["defaults"] && snode["defaults"].IsMap()) {
            merge_defaults(stage.defaults, parse_defaults_node(snode["defaults"]));
        }

        // Strict stage name validation immediately
        if (!is_valid_identifier(stage.name)) {
            throw ConfigurationError("Configuration error: Stage name '" + stage.name +
                                     "' violates identifier rules (must match POSIX-safe regex ^[a-zA-Z0-9_-]+$).");
        }

        if (snode["backend"]) stage.backend = snode["backend"].as<std::string>();
        else if (!stage.defaults.backend.empty()) stage.backend = stage.defaults.backend;
        if (snode["enabled_if"]) stage.enabled_if = snode["enabled_if"].as<std::string>();
        if (snode["allow_failure"]) stage.allow_failure = snode["allow_failure"].as<bool>();

        if (snode["depends_on"]) {
            if (snode["depends_on"].IsSequence()) {
                for (const auto& dep : snode["depends_on"]) {
                    stage.depends_on.push_back(dep.as<std::string>());
                }
            } else if (snode["depends_on"].IsScalar()) {
                stage.depends_on.push_back(snode["depends_on"].as<std::string>());
            }
        }

        if (snode["hooks"] && snode["hooks"].IsMap()) {
            const auto& sh = snode["hooks"];
            if (sh["pre"])    stage.hooks.pre    = sh["pre"].as<std::string>();
            if (sh["post"])   stage.hooks.post   = sh["post"].as<std::string>();
            if (sh["always"]) stage.hooks.always = sh["always"].as<bool>();
        }

        // Check if stage includes external file(s) for jobs
        std::vector<std::string> stage_includes;
        if (snode["include"]) {
            if (snode["include"].IsSequence()) {
                for (const auto& item : snode["include"]) stage_includes.push_back(item.as<std::string>());
            } else {
                stage_includes.push_back(snode["include"].as<std::string>());
            }
        }
        if (snode["includes"] && snode["includes"].IsSequence()) {
            for (const auto& item : snode["includes"]) stage_includes.push_back(item.as<std::string>());
        }

        for (const auto& inc_file : stage_includes) {
            stage.includes.push_back(inc_file);
            auto full_path = resolve_include_path(inc_file, base_dir);
            if (!std::filesystem::exists(full_path)) {
                throw ConfigurationError("Configuration error: Included file not found: '" + inc_file + "'");
            }
            YAML::Node inc_node;
            try {
                inc_node = YAML::LoadFile(full_path.string());
            } catch (const std::exception& e) {
                throw ConfigurationError("Configuration error: Failed to parse included file '" + inc_file + "': " + e.what());
            }

            JobDefaults inc_defaults = stage.defaults;
            if (inc_node["defaults"] && inc_node["defaults"].IsMap()) {
                merge_defaults(inc_defaults, parse_defaults_node(inc_node["defaults"]));
            }

            if (inc_node["jobs"] && inc_node["jobs"].IsSequence()) {
                for (const auto& jnode : inc_node["jobs"]) {
                    parse_job_entry(jnode, stage, job_counter, inc_defaults);
                }
            } else if (inc_node.IsSequence()) {
                for (const auto& jnode : inc_node) {
                    parse_job_entry(jnode, stage, job_counter, inc_defaults);
                }
            }
        }

        // Parse explicit jobs inside stage
        if (snode["jobs"] && snode["jobs"].IsSequence()) {
            for (const auto& jnode : snode["jobs"]) {
                if (jnode["include"]) {
                    std::string inc_file = jnode["include"].as<std::string>();
                    auto full_path = resolve_include_path(inc_file, base_dir);
                    if (!std::filesystem::exists(full_path)) {
                        throw ConfigurationError("Configuration error: Included file not found: '" + inc_file + "'");
                    }
                    YAML::Node inc_node = YAML::LoadFile(full_path.string());
                    JobDefaults inc_defaults = stage.defaults;
                    if (inc_node["defaults"] && inc_node["defaults"].IsMap()) {
                        merge_defaults(inc_defaults, parse_defaults_node(inc_node["defaults"]));
                    }
                    if (inc_node["jobs"] && inc_node["jobs"].IsSequence()) {
                        for (const auto& ij : inc_node["jobs"]) {
                            parse_job_entry(ij, stage, job_counter, inc_defaults);
                        }
                    } else if (inc_node.IsSequence()) {
                        for (const auto& ij : inc_node) {
                            parse_job_entry(ij, stage, job_counter, inc_defaults);
                        }
                    }
                } else {
                    parse_job_entry(jnode, stage, job_counter, stage.defaults);
                }
            }
        } else if (snode["command"]) {
            // Stage-level single job shorthand
            parse_job_entry(snode, stage, job_counter, stage.defaults);
        }

        config.stages.push_back(stage);
    }

    validate(config);
    return config;
}

PipelineConfig YamlParser::parse_file(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file) {
        throw ConfigurationError("Could not open pipeline configuration file: " + filepath);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::filesystem::path p(filepath);
    std::error_code ec;
    std::filesystem::path abs_p = std::filesystem::absolute(p, ec);
    std::string base_dir = (ec ? p : abs_p).parent_path().string();
    if (base_dir.empty()) base_dir = ".";
    auto config = parse_string(buffer.str(), base_dir);
    if (!config.variables.has("CONDOR_PIPELINE_FILE")) {
        config.variables.set("CONDOR_PIPELINE_FILE", (ec ? p : abs_p).string());
    }
    return config;
}

void YamlParser::validate(const PipelineConfig& config) {
    std::unordered_set<std::string> stage_names;

    for (const auto& stage : config.stages) {
        if (!is_valid_identifier(stage.name)) {
            throw ConfigurationError("Configuration error: Stage name '" + stage.name +
                                     "' violates identifier rules (must match POSIX-safe regex ^[a-zA-Z0-9_-]+$).");
        }
        if (stage_names.count(stage.name)) {
            throw ConfigurationError("Configuration error: Duplicate stage name '" + stage.name + "'.");
        }
        stage_names.insert(stage.name);
    }

    // Verify dependencies reference valid stages
    for (const auto& stage : config.stages) {
        for (const auto& dep : stage.depends_on) {
            if (!stage_names.count(dep)) {
                throw ConfigurationError("Configuration error: Stage '" + stage.name +
                                         "' depends on unknown stage '" + dep + "'.");
            }
        }
    }

    check_dag_cycles(config);
}

void YamlParser::check_dag_cycles(const PipelineConfig& config) {
    enum class NodeState { UNVISITED, VISITING, VISITED };
    std::unordered_map<std::string, NodeState> state;
    for (const auto& stage : config.stages) {
        state[stage.name] = NodeState::UNVISITED;
    }

    auto dfs = [&](const auto& self, const std::string& node, std::vector<std::string>& path) -> void {
        state[node] = NodeState::VISITING;
        path.push_back(node);

        const StageConfig* s = config.find_stage(node);
        if (s) {
            for (const auto& dep : s->depends_on) {
                if (state[dep] == NodeState::VISITING) {
                    std::string cycle_str;
                    for (const auto& p : path) {
                        cycle_str += p + " -> ";
                    }
                    cycle_str += dep;
                    throw ConfigurationError("Configuration error: Cycle detected in stage dependencies: " + cycle_str);
                } else if (state[dep] == NodeState::UNVISITED) {
                    self(self, dep, path);
                }
            }
        }

        path.pop_back();
        state[node] = NodeState::VISITED;
    };

    std::vector<std::string> path;
    for (const auto& stage : config.stages) {
        if (state[stage.name] == NodeState::UNVISITED) {
            dfs(dfs, stage.name, path);
        }
    }
}

} // namespace condor
