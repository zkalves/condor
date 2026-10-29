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
#include "condor/schema.hpp"
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

static std::string parse_hook_command_field(const YAML::Node& node, std::vector<std::string>& out_cmds) {
    out_cmds.clear();
    if (!node) return "";
    if (node.IsScalar()) {
        std::string cmd = node.as<std::string>();
        if (!cmd.empty()) out_cmds.push_back(cmd);
        return cmd;
    }
    if (node.IsSequence()) {
        std::string combined;
        for (const auto& item : node) {
            std::string cmd = item.as<std::string>();
            if (!cmd.empty()) {
                out_cmds.push_back(cmd);
                if (!combined.empty()) combined += "\n";
                combined += cmd;
            }
        }
        return combined;
    }
    return "";
}

static void parse_hook_config(const YAML::Node& node, HookConfig& hook) {
    if (!node || !node.IsMap()) return;
    if (node["pre"]) {
        hook.pre = parse_hook_command_field(node["pre"], hook.pre_commands);
    }
    if (node["post"]) {
        hook.post = parse_hook_command_field(node["post"], hook.post_commands);
    }
    if (node["always"]) {
        hook.always = node["always"].as<bool>();
    }
    if (node["allow_failure"]) {
        hook.allow_failure = node["allow_failure"].as<bool>();
    }
}

static void parse_job_hooks(const YAML::Node& jnode, JobConfig& job) {
    if (jnode["hooks"] && jnode["hooks"].IsMap()) {
        parse_hook_config(jnode["hooks"], job.hooks);
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

static bool parse_container_node(const YAML::Node& node, ContainerConfig& container) {
    if (!node) return false;
    if (node.IsScalar()) {
        container.image = node.as<std::string>();
        container.backend = "docker";
        return true;
    }
    if (node.IsMap()) {
        if (node["image"]) container.image = node["image"].as<std::string>();
        if (node["backend"]) container.backend = node["backend"].as<std::string>();
        if (node["options"]) {
            if (node["options"].IsSequence()) {
                for (const auto& opt : node["options"]) container.options.push_back(opt.as<std::string>());
            } else if (node["options"].IsScalar()) {
                container.options.push_back(node["options"].as<std::string>());
            }
        }
        if (node["mounts"]) {
            if (node["mounts"].IsSequence()) {
                for (const auto& m : node["mounts"]) container.mounts.push_back(m.as<std::string>());
            } else if (node["mounts"].IsScalar()) {
                container.mounts.push_back(node["mounts"].as<std::string>());
            }
        }
        return true;
    }
    return false;
}

static JobDefaults parse_defaults_node(const YAML::Node& node) {
    JobDefaults defs;
    if (!node || !node.IsMap()) return defs;
    if (node["backend"]) defs.backend = node["backend"].as<std::string>();
    if (node["working_dir"]) defs.working_dir = node["working_dir"].as<std::string>();
    if (node["timeout"]) {
        defs.timeout = node["timeout"].as<std::string>();
        defs.timeout_sec = parse_timeout_seconds(defs.timeout);
    }
    if (node["command"]) defs.command = node["command"].as<std::string>();
    if (node["args"])    defs.args    = node["args"].as<std::string>();
    if (node["append_args"]) defs.append_args = node["append_args"].as<bool>();
    if (node["scalable"]) defs.scalable = node["scalable"].as<bool>();
    if (node["hooks"] && node["hooks"].IsMap()) {
        parse_hook_config(node["hooks"], defs.hooks);
    }
    if (node["variables"] && node["variables"].IsMap()) {
        for (auto it = node["variables"].begin(); it != node["variables"].end(); ++it) {
            defs.variables[it->first.as<std::string>()] = it->second.as<std::string>();
        }
    }
    if (node["resources"] && node["resources"].IsMap()) {
        for (auto it = node["resources"].begin(); it != node["resources"].end(); ++it) {
            std::string res_name = it->first.as<std::string>();
            if (!is_valid_identifier(res_name)) {
                throw ConfigurationError("Configuration error: Resource name '" + res_name + "' violates identifier rules.");
            }
            int count = it->second.as<int>();
            if (count < 0) {
                throw ConfigurationError("Configuration error: Resource '" + res_name + "' count cannot be negative.");
            }
            defs.resources[res_name] = count;
        }
    }
    if (node["retry"]) {
        defs.retry = node["retry"].as<int>();
        if (defs.retry < 0) {
            throw ConfigurationError("Configuration error: 'retry' count cannot be negative.");
        }
    }
    if (node["retry_delay_sec"]) {
        defs.retry_delay_sec = node["retry_delay_sec"].as<int>();
        if (defs.retry_delay_sec < 0) {
            throw ConfigurationError("Configuration error: 'retry_delay_sec' cannot be negative.");
        }
    }
    if (node["retry_on_exit_codes"] && node["retry_on_exit_codes"].IsSequence()) {
        for (const auto& item : node["retry_on_exit_codes"]) {
            defs.retry_on_exit_codes.push_back(item.as<int>());
        }
    }
    if (node["container"]) {
        ContainerConfig c;
        if (parse_container_node(node["container"], c)) {
            defs.container = c;
        }
    }
    return defs;
}

static void merge_defaults(JobDefaults& target, const JobDefaults& override_defs) {
    if (!override_defs.command.empty()) target.command = override_defs.command;
    if (!override_defs.backend.empty()) target.backend = override_defs.backend;
    if (!override_defs.working_dir.empty()) target.working_dir = override_defs.working_dir;
    if (!override_defs.timeout.empty()) {
        target.timeout = override_defs.timeout;
        target.timeout_sec = override_defs.timeout_sec;
    }

    bool should_append = override_defs.append_args.has_value()
        ? *override_defs.append_args
        : target.append_args.value_or(false);

    if (!override_defs.args.empty()) {
        if (should_append && !target.args.empty()) {
            target.args = target.args + " " + override_defs.args;
        } else {
            target.args = override_defs.args;
        }
    }

    if (override_defs.append_args.has_value()) {
        target.append_args = override_defs.append_args;
    }
    if (override_defs.scalable.has_value()) {
        target.scalable = override_defs.scalable;
    }

    if (!override_defs.hooks.pre.empty()) {
        target.hooks.pre = override_defs.hooks.pre;
        target.hooks.pre_commands = override_defs.hooks.pre_commands;
    }
    if (!override_defs.hooks.post.empty()) {
        target.hooks.post = override_defs.hooks.post;
        target.hooks.post_commands = override_defs.hooks.post_commands;
        target.hooks.always = override_defs.hooks.always;
        target.hooks.allow_failure = override_defs.hooks.allow_failure;
    }
    for (const auto& [k, v] : override_defs.variables) {
        target.variables[k] = v;
    }
    for (const auto& [k, v] : override_defs.resources) {
        if (v == 0) {
            target.resources.erase(k);
        } else {
            target.resources[k] = v;
        }
    }
    if (override_defs.retry > 0) {
        target.retry = override_defs.retry;
    }
    if (override_defs.retry_delay_sec > 0) {
        target.retry_delay_sec = override_defs.retry_delay_sec;
    }
    if (!override_defs.retry_on_exit_codes.empty()) {
        target.retry_on_exit_codes = override_defs.retry_on_exit_codes;
    }
    if (override_defs.container.has_value()) {
        target.container = override_defs.container;
    }
}

static std::string sanitize_identifier(const std::string& input) {
    std::string out = input;
    for (char& c : out) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') {
            c = '_';
        }
    }
    return out;
}

static void parse_job_entry(const YAML::Node& jnode, StageConfig& stage, int& job_counter, const JobDefaults& defaults, size_t count_multiplier) {
    std::string base_name = jnode["name"] ? jnode["name"].as<std::string>() : "job";
    std::string backend = jnode["backend"] ? jnode["backend"].as<std::string>() : defaults.backend;
    std::string working_dir = jnode["working_dir"] ? jnode["working_dir"].as<std::string>() : defaults.working_dir;
    std::string timeout;
    int timeout_sec = 0;
    if (jnode["timeout"]) {
        timeout = jnode["timeout"].as<std::string>();
        timeout_sec = parse_timeout_seconds(timeout);
    } else {
        timeout = defaults.timeout;
        timeout_sec = defaults.timeout_sec;
    }
    std::string command = jnode["command"] ? jnode["command"].as<std::string>() : defaults.command;
    std::string enabled_if = jnode["enabled_if"] ? jnode["enabled_if"].as<std::string>() : "";
    bool allow_failure = jnode["allow_failure"] ? jnode["allow_failure"].as<bool>() : false;

    std::optional<bool> job_append_args;
    if (jnode["append_args"]) {
        job_append_args = jnode["append_args"].as<bool>();
    }
    bool should_append = job_append_args.has_value()
        ? *job_append_args
        : defaults.append_args.value_or(false);

    std::string final_args;
    if (jnode["args"]) {
        std::string j_args = jnode["args"].as<std::string>();
        if (should_append) {
            if (!defaults.args.empty() && !j_args.empty()) {
                final_args = defaults.args + " " + j_args;
            } else if (!j_args.empty()) {
                final_args = j_args;
            } else {
                final_args = defaults.args;
            }
        } else {
            final_args = j_args;
        }
    } else {
        final_args = defaults.args;
    }

    std::optional<bool> job_scalable;
    if (jnode["scalable"]) {
        job_scalable = jnode["scalable"].as<bool>();
    }
    bool is_scalable = job_scalable.has_value()
        ? *job_scalable
        : defaults.scalable.value_or(true);

    bool has_count = jnode["count"].IsDefined();
    int count_val = 1;
    if (has_count) {
        count_val = jnode["count"].as<int>();
        if (count_val <= 0) {
            throw ConfigurationError("Configuration error: Job '" + base_name + "' count must be greater than 0.");
        }
    }
    size_t effective_count = has_count ? (is_scalable ? (count_val * count_multiplier) : count_val) : 1;

    bool has_matrix = jnode["matrix"].IsDefined();
    std::vector<std::string> matrix_keys;
    std::vector<std::vector<std::string>> matrix_values;
    if (has_matrix) {
        if (!jnode["matrix"].IsMap()) {
            throw ConfigurationError("Configuration error: Job '" + base_name + "' matrix must be a mapping of variable names to lists of values.");
        }
        for (auto it = jnode["matrix"].begin(); it != jnode["matrix"].end(); ++it) {
            std::string var_name = it->first.as<std::string>();
            if (!is_valid_identifier(var_name)) {
                throw ConfigurationError("Configuration error: Matrix variable name '" + var_name + "' violates identifier rules.");
            }
            std::vector<std::string> vals;
            if (it->second.IsSequence()) {
                for (const auto& item : it->second) {
                    vals.push_back(item.as<std::string>());
                }
            } else if (it->second.IsScalar()) {
                vals.push_back(it->second.as<std::string>());
            }
            if (vals.empty()) {
                throw ConfigurationError("Configuration error: Matrix variable '" + var_name + "' must contain at least one value.");
            }
            matrix_keys.push_back(var_name);
            matrix_values.push_back(vals);
        }
        if (matrix_keys.empty()) {
            throw ConfigurationError("Configuration error: Job '" + base_name + "' matrix cannot be empty.");
        }
    }

    auto apply_job_defaults = [&](JobConfig& job) {
        // Inherit default variables
        for (const auto& [k, v] : defaults.variables) {
            job.variables[k] = v;
        }
        job.variables["JOB_NAME"] = job.name;
        job.variables["TEST_NAME"] = base_name;
        if (!final_args.empty()) {
            job.variables["ARGS"] = final_args;
        }
        if (jnode["variables"] && jnode["variables"].IsMap()) {
            for (auto it = jnode["variables"].begin(); it != jnode["variables"].end(); ++it) {
                job.variables[it->first.as<std::string>()] = it->second.as<std::string>();
            }
        }
        if (job.hooks.pre.empty() && !defaults.hooks.pre.empty()) {
            job.hooks.pre = defaults.hooks.pre;
            job.hooks.pre_commands = defaults.hooks.pre_commands;
        }
        if (job.hooks.post.empty() && !defaults.hooks.post.empty()) {
            job.hooks.post = defaults.hooks.post;
            job.hooks.post_commands = defaults.hooks.post_commands;
            job.hooks.always = defaults.hooks.always;
            job.hooks.allow_failure = defaults.hooks.allow_failure;
        }
        if (job_append_args.has_value()) {
            job.append_args = job_append_args;
        }
        // Inherit default resources
        for (const auto& [k, v] : defaults.resources) {
            job.resources[k] = v;
        }
        if (jnode["resources"] && jnode["resources"].IsMap()) {
            for (auto it = jnode["resources"].begin(); it != jnode["resources"].end(); ++it) {
                std::string rname = it->first.as<std::string>();
                if (!is_valid_identifier(rname)) {
                    throw ConfigurationError("Configuration error: Resource name '" + rname + "' violates identifier rules.");
                }
                int count = it->second.as<int>();
                if (count < 0) {
                    throw ConfigurationError("Configuration error: Resource '" + rname + "' count cannot be negative.");
                }
                if (count == 0) {
                    job.resources.erase(rname);
                } else {
                    job.resources[rname] = count;
                }
            }
        }

        // Inherit and override retry policy
        job.retry = defaults.retry;
        job.retry_delay_sec = defaults.retry_delay_sec;
        job.retry_on_exit_codes = defaults.retry_on_exit_codes;
        if (jnode["retry"]) {
            job.retry = jnode["retry"].as<int>();
            if (job.retry < 0) {
                throw ConfigurationError("Configuration error: 'retry' count cannot be negative.");
            }
        }
        if (jnode["retry_delay_sec"]) {
            job.retry_delay_sec = jnode["retry_delay_sec"].as<int>();
            if (job.retry_delay_sec < 0) {
                throw ConfigurationError("Configuration error: 'retry_delay_sec' cannot be negative.");
            }
        }
        if (jnode["retry_on_exit_codes"] && jnode["retry_on_exit_codes"].IsSequence()) {
            job.retry_on_exit_codes.clear();
            for (const auto& item : jnode["retry_on_exit_codes"]) {
                job.retry_on_exit_codes.push_back(item.as<int>());
            }
        }

        // Inherit and override container
        job.container = defaults.container;
        if (jnode["container"]) {
            ContainerConfig c;
            if (parse_container_node(jnode["container"], c)) {
                job.container = c;
            }
        }

        job.scalable = is_scalable;
        job.backend = backend;
        job.working_dir = working_dir;
        job.timeout = timeout;
        job.timeout_sec = timeout_sec;

        // Auto-append arguments to command if command does not contain ${ARGS}
        if (!final_args.empty() && job.command.find("${ARGS}") == std::string::npos) {
            if (job.command.empty()) {
                job.command = final_args;
            } else {
                job.command += " " + final_args;
            }
        }
    };

    if (has_matrix) {
        std::vector<std::unordered_map<std::string, std::string>> combinations;
        std::function<void(size_t, std::unordered_map<std::string, std::string>&)> generate_combinations;
        generate_combinations = [&](size_t depth, std::unordered_map<std::string, std::string>& current) {
            if (depth == matrix_keys.size()) {
                combinations.push_back(current);
                return;
            }
            const auto& key = matrix_keys[depth];
            for (const auto& val : matrix_values[depth]) {
                current[key] = val;
                generate_combinations(depth + 1, current);
            }
        };
        std::unordered_map<std::string, std::string> initial;
        generate_combinations(0, initial);

        for (size_t comb_idx = 0; comb_idx < combinations.size(); ++comb_idx) {
            const auto& comb = combinations[comb_idx];
            std::string comb_suffix = "";
            for (const auto& key : matrix_keys) {
                comb_suffix += "_" + key + "_" + sanitize_identifier(comb.at(key));
            }
            std::string comb_name = base_name + comb_suffix;

            if (has_count) {
                for (size_t i = 0; i < effective_count; ++i) {
                    JobConfig job;
                    std::ostringstream id_ss;
                    id_ss << std::setw(4) << std::setfill('0') << job_counter++;
                    job.job_id = id_ss.str();
                    job.stage_name = stage.name;
                    job.name = (effective_count > 1) ? (comb_name + "_" + std::to_string(i)) : comb_name;
                    job.command = command;
                    job.enabled_if = enabled_if;
                    job.allow_failure = allow_failure;
                    parse_job_deps(jnode, job);
                    parse_job_hooks(jnode, job);
                    job.count = effective_count;
                    apply_job_defaults(job);
                    for (const auto& [mk, mv] : comb) {
                        job.variables[mk] = mv;
                    }
                    job.variables["INDEX"] = std::to_string(i);
                    stage.jobs.push_back(job);
                }
            } else {
                JobConfig job;
                std::ostringstream id_ss;
                id_ss << std::setw(4) << std::setfill('0') << job_counter++;
                job.job_id = id_ss.str();
                job.stage_name = stage.name;
                job.name = comb_name;
                job.command = command;
                job.enabled_if = enabled_if;
                job.allow_failure = allow_failure;
                parse_job_deps(jnode, job);
                parse_job_hooks(jnode, job);
                apply_job_defaults(job);
                for (const auto& [mk, mv] : comb) {
                    job.variables[mk] = mv;
                }
                job.variables["INDEX"] = std::to_string(comb_idx);
                stage.jobs.push_back(job);
            }
        }
    } else if (has_count) {
        for (size_t i = 0; i < effective_count; ++i) {
            JobConfig job;
            std::ostringstream id_ss;
            id_ss << std::setw(4) << std::setfill('0') << job_counter++;
            job.job_id = id_ss.str();
            job.stage_name = stage.name;
            job.name = base_name + "_" + std::to_string(i);
            job.command = command;
            job.enabled_if = enabled_if;
            job.allow_failure = allow_failure;
            parse_job_deps(jnode, job);
            parse_job_hooks(jnode, job);
            job.count = effective_count;
            apply_job_defaults(job);
            job.variables["INDEX"] = std::to_string(i);
            stage.jobs.push_back(job);
        }
    } else {
        JobConfig job;
        std::ostringstream id_ss;
        id_ss << std::setw(4) << std::setfill('0') << job_counter++;
        job.job_id = id_ss.str();
        job.stage_name = stage.name;
        job.name = jnode["name"] ? jnode["name"].as<std::string>() : ("job_" + job.job_id);
        job.command = command;
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

PipelineConfig YamlParser::parse_string(std::string_view yaml_content, const std::string& base_dir, size_t cli_count_multiplier) {
    YAML::Node root;
    try {
        root = YAML::Load(std::string(yaml_content));
    } catch (const YAML::Exception& e) {
        throw ConfigurationError("YAML parsing error: " + std::string(e.what()));
    }

    if (!root || !root.IsMap()) {
        throw ConfigurationError("Invalid YAML configuration: root must be a mapping.");
    }

    // Validate YAML specification against Declarative JSON Schema
    validate_yaml_schema(root);

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
    config.includes = top_includes;

    // Parse global pipeline defaults
    if (root["defaults"] && root["defaults"].IsMap()) {
        const auto& dnode = root["defaults"];
        if (dnode["stage"] && dnode["stage"].IsMap()) {
            const auto& snode = dnode["stage"];
            if (snode["hooks"] && snode["hooks"].IsMap()) {
                parse_hook_config(snode["hooks"], config.stage_defaults.hooks);
            }
        }

        if (dnode["job"] && dnode["job"].IsMap()) {
            config.defaults = parse_defaults_node(dnode["job"]);
        } else {
            config.defaults = parse_defaults_node(dnode);
        }
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
        if (s["backend"]) {
            config.settings.backend = s["backend"].as<std::string>();
        }
        if (s["output_mode"]) {
            config.settings.output_mode = parse_output_mode(s["output_mode"].as<std::string>());
        }
        if (s["path_style"]) {
            config.settings.path_style = parse_path_style(s["path_style"].as<std::string>());
        }
        if (s["grid_poll_interval_sec"]) {
            config.settings.grid_poll_interval_sec = s["grid_poll_interval_sec"].as<int>();
        }
        if (s["emit_junit_xml"]) {
            config.settings.emit_junit_xml = s["emit_junit_xml"].as<bool>();
        }
        if (s["junit_xml_path"]) {
            config.settings.junit_xml_path = s["junit_xml_path"].as<std::string>();
        }
        if (s["count_multiplier"]) {
            config.settings.count_multiplier = s["count_multiplier"].as<size_t>();
            if (config.settings.count_multiplier == 0) config.settings.count_multiplier = 1;
        }
        if (s["resources"] && s["resources"].IsMap()) {
            for (auto it = s["resources"].begin(); it != s["resources"].end(); ++it) {
                std::string rname = it->first.as<std::string>();
                if (!is_valid_identifier(rname)) {
                    throw ConfigurationError("Configuration error: Resource name '" + rname +
                                             "' violates identifier rules (must match POSIX-safe regex ^[a-zA-Z0-9_-]+$).");
                }
                int cap = it->second.as<int>();
                if (cap <= 0) {
                    throw ConfigurationError("Configuration error: Resource '" + rname + "' capacity must be greater than 0.");
                }
                config.resources[rname] = cap;
            }
        }
        if (s["kill_grace_period_sec"]) {
            config.settings.kill_grace_period_sec = s["kill_grace_period_sec"].as<int>();
            if (config.settings.kill_grace_period_sec < 0) config.settings.kill_grace_period_sec = 0;
        }
        if (s["max_retained_runs"]) {
            config.settings.max_retained_runs = s["max_retained_runs"].as<size_t>();
        }
        if (s["purge_successful_logs"]) {
            config.settings.purge_successful_logs = s["purge_successful_logs"].as<bool>();
        }
        if (s["server"] && s["server"].IsMap()) {
            const auto& srv = s["server"];
            if (srv["enabled"]) config.settings.server.enabled = srv["enabled"].as<bool>();
            if (srv["url"]) {
                config.settings.server.url = srv["url"].as<std::string>();
                if (!srv["enabled"]) config.settings.server.enabled = true;
            }
            if (srv["token"]) config.settings.server.token = srv["token"].as<std::string>();
            if (srv["project"]) config.settings.server.project = srv["project"].as<std::string>();
            if (srv["tags"] && srv["tags"].IsSequence()) {
                for (const auto& tag_node : srv["tags"]) {
                    config.settings.server.tags.push_back(tag_node.as<std::string>());
                }
            }
            if (srv["poll_interval_ms"]) {
                config.settings.server.poll_interval_ms = srv["poll_interval_ms"].as<int>();
                if (config.settings.server.poll_interval_ms < 50) config.settings.server.poll_interval_ms = 50;
            }
        }
    }

    // Parse top-level resources
    if (root["resources"] && root["resources"].IsMap()) {
        for (auto it = root["resources"].begin(); it != root["resources"].end(); ++it) {
            std::string rname = it->first.as<std::string>();
            if (!is_valid_identifier(rname)) {
                throw ConfigurationError("Configuration error: Resource name '" + rname +
                                         "' violates identifier rules (must match POSIX-safe regex ^[a-zA-Z0-9_-]+$).");
            }
            int cap = it->second.as<int>();
            if (cap <= 0) {
                throw ConfigurationError("Configuration error: Resource '" + rname + "' capacity must be greater than 0.");
            }
            config.resources[rname] = cap;
        }
    }

    if (cli_count_multiplier > 0) {
        config.settings.count_multiplier = cli_count_multiplier;
    }

    if (config.settings.max_parallel_jobs == 0) {
        size_t cores = std::thread::hardware_concurrency();
        config.settings.max_parallel_jobs = (cores > 0) ? cores : 4;
    }

    // Top-level backend is disallowed: backend must be configured under settings.backend
    if (root["backend"]) {
        throw ConfigurationError("Configuration error: 'backend' cannot be configured at the top level. The execution backend is a pipeline configuration defined under 'settings.backend'.");
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
        parse_hook_config(root["hooks"], config.hooks);
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

        // Explicitly reject stage-level command and args
        if (snode["command"] || snode["args"]) {
            throw ConfigurationError("Configuration error: 'command' and 'args' cannot be configured directly on stage '" + stage.name +
                                     "'. Stages are DAG phases with hooks; commands belong to jobs under 'jobs:' or stage defaults under 'defaults:'.");
        }

        stage.defaults = config.defaults;
        if (snode["defaults"]) {
            if (!snode["defaults"].IsMap()) {
                throw ConfigurationError("Configuration error: 'defaults' in stage '" + stage.name + "' must be a mapping.");
            }
            const auto& sdnode = snode["defaults"];
            if (sdnode["job"] && sdnode["job"].IsMap()) {
                JobDefaults base_sd = parse_defaults_node(sdnode);
                merge_defaults(base_sd, parse_defaults_node(sdnode["job"]));
                merge_defaults(stage.defaults, base_sd);
            } else {
                merge_defaults(stage.defaults, parse_defaults_node(sdnode));
            }
        }

        // Strict stage name validation immediately
        if (!is_valid_identifier(stage.name)) {
            throw ConfigurationError("Configuration error: Stage name '" + stage.name +
                                     "' violates identifier rules (must match POSIX-safe regex ^[a-zA-Z0-9_-]+$).");
        }

        if (snode["backend"]) {
            stage.backend = snode["backend"].as<std::string>();
            if (stage.defaults.backend.empty()) {
                stage.defaults.backend = stage.backend;
            }
        }
        if (snode["mode"]) {
            stage.mode = parse_stage_execution_mode(snode["mode"].as<std::string>());
        }
        if (snode["enabled_if"]) stage.enabled_if = snode["enabled_if"].as<std::string>();
        if (snode["allow_failure"]) stage.allow_failure = snode["allow_failure"].as<bool>();
        if (snode["container"]) {
            ContainerConfig c;
            if (parse_container_node(snode["container"], c)) {
                stage.container = c;
                stage.defaults.container = c;
            }
        }

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
            parse_hook_config(snode["hooks"], stage.hooks);
        } else if (!config.stage_defaults.hooks.pre.empty() || !config.stage_defaults.hooks.post.empty()) {
            stage.hooks = config.stage_defaults.hooks;
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
                const auto& ind = inc_node["defaults"];
                if (ind["job"] && ind["job"].IsMap()) {
                    JobDefaults base_ind = parse_defaults_node(ind);
                    merge_defaults(base_ind, parse_defaults_node(ind["job"]));
                    merge_defaults(inc_defaults, base_ind);
                } else {
                    merge_defaults(inc_defaults, parse_defaults_node(ind));
                }
            }

            if (inc_node["jobs"] && inc_node["jobs"].IsSequence()) {
                for (const auto& jnode : inc_node["jobs"]) {
                    parse_job_entry(jnode, stage, job_counter, inc_defaults, config.settings.count_multiplier);
                }
            } else if (inc_node.IsSequence()) {
                for (const auto& jnode : inc_node) {
                    parse_job_entry(jnode, stage, job_counter, inc_defaults, config.settings.count_multiplier);
                }
            }
        }

        // Parse explicit jobs inside stage
        if (snode["jobs"] && snode["jobs"].IsSequence()) {
            for (const auto& jnode : snode["jobs"]) {
                if (jnode["include"]) {
                    std::vector<std::string> job_includes;
                    if (jnode["include"].IsSequence()) {
                        for (const auto& item : jnode["include"]) job_includes.push_back(item.as<std::string>());
                    } else {
                        job_includes.push_back(jnode["include"].as<std::string>());
                    }
                    for (const auto& inc_file : job_includes) {
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
                            const auto& ind = inc_node["defaults"];
                            if (ind["job"] && ind["job"].IsMap()) {
                                JobDefaults base_ind = parse_defaults_node(ind);
                                merge_defaults(base_ind, parse_defaults_node(ind["job"]));
                                merge_defaults(inc_defaults, base_ind);
                            } else {
                                merge_defaults(inc_defaults, parse_defaults_node(ind));
                            }
                        }
                        if (inc_node["jobs"] && inc_node["jobs"].IsSequence()) {
                            for (const auto& ij : inc_node["jobs"]) {
                                parse_job_entry(ij, stage, job_counter, inc_defaults, config.settings.count_multiplier);
                            }
                        } else if (inc_node.IsSequence()) {
                            for (const auto& ij : inc_node) {
                                parse_job_entry(ij, stage, job_counter, inc_defaults, config.settings.count_multiplier);
                            }
                        }
                    }
                } else {
                    parse_job_entry(jnode, stage, job_counter, stage.defaults, config.settings.count_multiplier);
                }
            }
        }

        if (stage.jobs.empty()) {
            throw ConfigurationError("Configuration error: Stage '" + stage.name + "' has no jobs configured. Specify 'jobs:' or 'include:'.");
        }

        config.stages.push_back(stage);
    }

    validate(config);
    return config;
}

PipelineConfig YamlParser::parse_file(const std::string& filepath, size_t cli_count_multiplier) {
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
    auto config = parse_string(buffer.str(), base_dir, cli_count_multiplier);
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

    // Verify resource pool capacities and job requests
    for (const auto& [rname, cap] : config.resources) {
        if (!is_valid_identifier(rname)) {
            throw ConfigurationError("Configuration error: Resource pool name '" + rname +
                                     "' violates identifier rules (must match POSIX-safe regex ^[a-zA-Z0-9_-]+$).");
        }
        if (cap <= 0) {
            throw ConfigurationError("Configuration error: Resource '" + rname + "' capacity must be greater than 0.");
        }
    }

    for (const auto& [rname, req] : config.defaults.resources) {
        auto it = config.resources.find(rname);
        if (it == config.resources.end()) {
            throw ConfigurationError("Configuration error: Default resource '" + rname + "' is not defined in pipeline resources.");
        }
        if (req > it->second) {
            throw ConfigurationError("Configuration error: Default resource '" + rname + "' request (" +
                                     std::to_string(req) + ") exceeds pool capacity (" + std::to_string(it->second) + ").");
        }
    }

    for (const auto& stage : config.stages) {
        for (const auto& [rname, req] : stage.defaults.resources) {
            auto it = config.resources.find(rname);
            if (it == config.resources.end()) {
                throw ConfigurationError("Configuration error: Stage '" + stage.name + "' default resource '" +
                                         rname + "' is not defined in pipeline resources.");
            }
            if (req > it->second) {
                throw ConfigurationError("Configuration error: Stage '" + stage.name + "' default resource '" +
                                         rname + "' request (" + std::to_string(req) + ") exceeds pool capacity (" +
                                         std::to_string(it->second) + ").");
            }
        }
        for (const auto& job : stage.jobs) {
            for (const auto& [rname, req] : job.resources) {
                auto it = config.resources.find(rname);
                if (it == config.resources.end()) {
                    throw ConfigurationError("Configuration error: Job '" + job.name + "' requests undefined resource '" +
                                             rname + "'.");
                }
                if (req > it->second) {
                    throw ConfigurationError("Configuration error: Job '" + job.name + "' requests " +
                                             std::to_string(req) + " units of resource '" + rname +
                                             "', exceeding total pool capacity of " + std::to_string(it->second) + ".");
                }
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

    // 2. Job-level dependency cycle detection
    std::unordered_map<std::string, std::vector<std::string>> job_adj;
    std::unordered_set<std::string> all_job_keys;

    for (const auto& stage : config.stages) {
        for (const auto& job : stage.jobs) {
            std::string jkey = stage.name + "/" + job.job_id;
            all_job_keys.insert(jkey);
            job_adj[jkey] = {};
        }
    }

    for (const auto& stage : config.stages) {
        for (const auto& job : stage.jobs) {
            std::string jkey = stage.name + "/" + job.job_id;

            // Inherit stage-level dependencies
            for (const auto& sdep : stage.depends_on) {
                const StageConfig* dep_s = config.find_stage(sdep);
                if (dep_s) {
                    for (const auto& dj : dep_s->jobs) {
                        job_adj[jkey].push_back(dep_s->name + "/" + dj.job_id);
                    }
                }
            }

            // Direct job-level dependencies
            for (const auto& dep : job.depends_on) {
                // 1. stage/job_id
                if (all_job_keys.count(dep)) {
                    job_adj[jkey].push_back(dep);
                    continue;
                }
                // 2. job_id in same stage
                std::string same_stage_id = stage.name + "/" + dep;
                if (all_job_keys.count(same_stage_id)) {
                    job_adj[jkey].push_back(same_stage_id);
                    continue;
                }
                // 3. Stage name dependency
                const StageConfig* dep_s = config.find_stage(dep);
                if (dep_s) {
                    for (const auto& dj : dep_s->jobs) {
                        job_adj[jkey].push_back(dep_s->name + "/" + dj.job_id);
                    }
                    continue;
                }
                // 4. Job name match within same stage or across stages
                for (const auto& ostg : config.stages) {
                    for (const auto& oj : ostg.jobs) {
                        if (oj.name == dep || (oj.job_id == dep && ostg.name == stage.name)) {
                            job_adj[jkey].push_back(ostg.name + "/" + oj.job_id);
                        }
                    }
                }
            }
        }
    }

    std::unordered_map<std::string, NodeState> job_states;
    for (const auto& k : all_job_keys) {
        job_states[k] = NodeState::UNVISITED;
    }

    auto job_dfs = [&](const auto& self, const std::string& node, std::vector<std::string>& jpath) -> void {
        job_states[node] = NodeState::VISITING;
        jpath.push_back(node);

        auto it = job_adj.find(node);
        if (it != job_adj.end()) {
            for (const auto& dep : it->second) {
                if (!all_job_keys.count(dep)) continue;
                if (job_states[dep] == NodeState::VISITING) {
                    std::string cycle_str;
                    for (const auto& p : jpath) {
                        cycle_str += p + " -> ";
                    }
                    cycle_str += dep;
                    throw ConfigurationError("Configuration error: Cycle detected in job dependencies: " + cycle_str);
                } else if (job_states[dep] == NodeState::UNVISITED) {
                    self(self, dep, jpath);
                }
            }
        }

        jpath.pop_back();
        job_states[node] = NodeState::VISITED;
    };

    std::vector<std::string> jpath;
    for (const auto& k : all_job_keys) {
        if (job_states[k] == NodeState::UNVISITED) {
            job_dfs(job_dfs, k, jpath);
        }
    }
}

} // namespace condor
