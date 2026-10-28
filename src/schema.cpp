/*
 * This file is part of Condor.
 *
 * Condor is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Condor.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Copyright (C) 2026 Ezequiel Alves. All rights reserved.
 */

#include "condor/schema.hpp"
#include "condor/common.hpp"
#include <nlohmann/json.hpp>
#include <nlohmann/json-schema.hpp>
#include <yaml-cpp/yaml.h>
#include <sstream>
#include <mutex>
#include <cstdint>

namespace condor {

static constexpr std::string_view PIPELINE_SCHEMA_JSON = R"json({
  "$schema": "http://json-schema.org/draft-07/schema#",
  "$id": "https://raw.githubusercontent.com/zkalves/condor/main/res/schema/condor-pipeline-schema.json",
  "title": "Condor Pipeline Configuration Schema",
  "description": "JSON Schema specification for Condor C++20 pipeline YAML files",
  "type": "object",
  "required": ["stages"],
  "properties": {
    "name": {
      "type": "string",
      "description": "Pipeline project name"
    },
    "include": {
      "oneOf": [
        { "type": "string" },
        { "type": "array", "items": { "type": "string" } }
      ],
      "description": "Root-level YAML configuration file(s) to include"
    },
    "variables": {
      "type": "object",
      "description": "Global application-independent variables",
      "additionalProperties": {
        "type": ["string", "number", "boolean"]
      }
    },
    "settings": {
      "type": "object",
      "description": "Runtime execution options and settings",
      "properties": {
        "max_parallel_jobs": {
          "type": "integer",
          "minimum": 0,
          "description": "Maximum concurrent job execution slots (0 = automatic)"
        },
        "base_output_dir": {
          "type": "string",
          "description": "Directory where run outputs are stored"
        },
        "run_dir": {
          "type": "string",
          "description": "Explicit output directory override"
        },
        "on_failure_action": {
          "type": "string",
          "enum": ["abort", "continue", "stage_barrier"],
          "description": "Pipeline failure policy"
        },
        "exit_zero_on_failure": {
          "type": "boolean",
          "description": "Exit with code 0 even if jobs failed"
        },
        "status_interval_sec": {
          "type": "integer",
          "minimum": 1,
          "description": "Interval in seconds between periodic status summaries"
        },
        "backend": {
          "type": "string",
          "description": "Execution backend identifier"
        },
        "output_mode": {
          "type": "string",
          "enum": ["all", "failures", "summary"],
          "description": "Terminal output verbosity mode"
        },
        "path_style": {
          "type": "string",
          "enum": ["absolute", "relative"],
          "description": "Path export style for environment variables"
        },
        "dry_run": {
          "type": "boolean",
          "description": "Dry run execution without spawning jobs"
        },
        "no_interactive": {
          "type": "boolean",
          "description": "Disable interactive terminal features"
        },
        "verbose_logging": {
          "type": "boolean",
          "description": "Enable verbose logging"
        },
        "count_multiplier": {
          "type": "integer",
          "minimum": 1,
          "description": "Global multiplier for scalable job counts"
        }
      },
      "additionalProperties": false
    },
    "defaults": {
      "type": "object",
      "description": "Default configuration inherited by stages and jobs",
      "properties": {
        "command": { "type": "string" },
        "args": { "type": "string" },
        "append_args": { "type": "boolean" },
        "scalable": { "type": "boolean" },
        "hooks": { "$ref": "#/definitions/HookConfig" },
        "variables": {
          "type": "object",
          "additionalProperties": { "type": ["string", "number", "boolean"] }
        },
        "stage": {
          "type": "object",
          "properties": {
            "hooks": { "$ref": "#/definitions/HookConfig" }
          },
          "additionalProperties": false
        },
        "job": {
          "type": "object",
          "properties": {
            "command": { "type": "string" },
            "args": { "type": "string" },
            "append_args": { "type": "boolean" },
            "scalable": { "type": "boolean" },
            "hooks": { "$ref": "#/definitions/HookConfig" },
            "variables": {
              "type": "object",
              "additionalProperties": { "type": ["string", "number", "boolean"] }
            }
          },
          "additionalProperties": false
        }
      },
      "additionalProperties": false
    },
    "backends": {
      "type": "object",
      "additionalProperties": {
        "type": "object",
        "properties": {
          "type": { "type": "string", "enum": ["local", "template"] },
          "description": { "type": "string" },
          "submit_command": { "type": "string" },
          "poll_command": { "type": "string" },
          "cancel_command": { "type": "string" },
          "job_id_regex": { "type": "string" },
          "success_pattern": { "type": "string" },
          "failure_pattern": { "type": "string" },
          "running_pattern": { "type": "string" },
          "empty_poll_means_done": { "type": "boolean" }
        },
        "additionalProperties": false
      }
    },
    "hooks": {
      "$ref": "#/definitions/HookConfig"
    },
    "stages": {
      "type": "array",
      "minItems": 1,
      "items": { "$ref": "#/definitions/StageConfig" }
    }
  },
  "definitions": {
    "HookConfig": {
      "type": "object",
      "properties": {
        "pre": {
          "oneOf": [
            { "type": "string" },
            { "type": "array", "items": { "type": "string" } }
          ]
        },
        "post": {
          "oneOf": [
            { "type": "string" },
            { "type": "array", "items": { "type": "string" } }
          ]
        },
        "always": { "type": "boolean" },
        "allow_failure": { "type": "boolean" }
      },
      "additionalProperties": false
    },
    "StageConfig": {
      "type": "object",
      "required": ["name"],
      "properties": {
        "name": {
          "type": "string",
          "pattern": "^[a-zA-Z0-9_-]+$",
          "description": "POSIX-safe identifier for stage name"
        },
        "enabled_if": { "type": "string" },
        "allow_failure": { "type": "boolean" },
        "depends_on": {
          "oneOf": [
            { "type": "string" },
            { "type": "array", "items": { "type": "string" } }
          ]
        },
        "hooks": { "$ref": "#/definitions/HookConfig" },
        "include": {
          "oneOf": [
            { "type": "string" },
            { "type": "array", "items": { "type": "string" } }
          ]
        },
        "defaults": {
          "type": "object",
          "properties": {
            "command": { "type": "string" },
            "args": { "type": "string" },
            "append_args": { "type": "boolean" },
            "scalable": { "type": "boolean" },
            "hooks": { "$ref": "#/definitions/HookConfig" },
            "variables": {
              "type": "object",
              "additionalProperties": { "type": ["string", "number", "boolean"] }
            },
            "job": {
              "type": "object",
              "properties": {
                "command": { "type": "string" },
                "args": { "type": "string" },
                "append_args": { "type": "boolean" },
                "scalable": { "type": "boolean" },
                "hooks": { "$ref": "#/definitions/HookConfig" },
                "variables": {
                  "type": "object",
                  "additionalProperties": { "type": ["string", "number", "boolean"] }
                }
              },
              "additionalProperties": false
            }
          },
          "additionalProperties": false
        },
        "jobs": {
          "type": "array",
          "items": { "$ref": "#/definitions/JobConfig" }
        }
      },
      "additionalProperties": false
    },
    "JobConfig": {
      "type": "object",
      "properties": {
        "name": { "type": "string" },
        "command": { "type": "string" },
        "args": { "type": "string" },
        "append_args": { "type": "boolean" },
        "scalable": {
          "type": "boolean",
          "description": "If false, job count is pinned and unaffected by count multipliers"
        },
        "enabled_if": { "type": "string" },
        "allow_failure": { "type": "boolean" },
        "depends_on": {
          "oneOf": [
            { "type": "string" },
            { "type": "array", "items": { "type": "string" } }
          ]
        },
        "hooks": { "$ref": "#/definitions/HookConfig" },
        "variables": {
          "type": "object",
          "additionalProperties": { "type": ["string", "number", "boolean"] }
        },
        "count": { "type": "integer", "minimum": 1 },
        "matrix": {
          "type": "object",
          "description": "Matrix parameter sweep mapping variable names to lists of values",
          "additionalProperties": {
            "type": "array",
            "items": { "type": ["string", "number", "boolean"] },
            "minItems": 1
          }
        },
        "include": {
          "oneOf": [
            { "type": "string" },
            { "type": "array", "items": { "type": "string" } }
          ],
          "description": "External YAML file(s) containing job definitions to merge"
        }
      },
      "additionalProperties": false
    }
  },
  "additionalProperties": false
})json";

std::string_view get_pipeline_schema_string() {
    return PIPELINE_SCHEMA_JSON;
}

nlohmann::json yaml_node_to_json(const YAML::Node& node) {
    if (!node.IsDefined() || node.IsNull()) {
        return nullptr;
    }

    if (node.IsScalar()) {
        const std::string& tag = node.Tag();
        const std::string& val = node.Scalar();

        // Non-plain scalar tags (e.g. quoted string "..." or '...')
        if (tag == "!" || tag == "tag:yaml.org,2002:str") {
            return val;
        }

        // Boolean keywords
        if (val == "true" || val == "True" || val == "TRUE") return true;
        if (val == "false" || val == "False" || val == "FALSE") return false;

        // Null keywords
        if (val == "null" || val == "~" || val == "Null" || val == "NULL") return nullptr;

        // Numeric: Integer (hex or decimal)
        try {
            size_t idx = 0;
            if (val.rfind("0x", 0) == 0 || val.rfind("0X", 0) == 0) {
                int64_t i = std::stoll(val, &idx, 16);
                if (idx == val.size()) return i;
            } else {
                int64_t i = std::stoll(val, &idx, 10);
                if (idx == val.size()) return i;
            }
        } catch (...) {}

        // Numeric: Floating point
        try {
            size_t idx = 0;
            double d = std::stod(val, &idx);
            if (idx == val.size()) return d;
        } catch (...) {}

        // Fallback to scalar string
        return val;
    }

    if (node.IsSequence()) {
        auto arr = nlohmann::json::array();
        for (const auto& item : node) {
            arr.push_back(yaml_node_to_json(item));
        }
        return arr;
    }

    if (node.IsMap()) {
        auto obj = nlohmann::json::object();
        for (const auto& kv : node) {
            obj[kv.first.as<std::string>()] = yaml_node_to_json(kv.second);
        }
        return obj;
    }

    return nullptr;
}

class SchemaErrorCollector : public nlohmann::json_schema::error_handler {
public:
    std::vector<std::string> errors;
    void error(const nlohmann::json::json_pointer &ptr, const nlohmann::json &instance, const std::string &message) override {
        std::string path = ptr.to_string();
        if (path.empty()) path = "/";
        errors.push_back("At '" + path + "': " + message);
    }
};

void validate_yaml_schema(const YAML::Node& root) {
    if (!root || !root.IsMap()) {
        throw ConfigurationError("Invalid YAML configuration: root must be a mapping.");
    }

    static const nlohmann::json schema_json = nlohmann::json::parse(get_pipeline_schema_string());
    static nlohmann::json_schema::json_validator validator([](const nlohmann::json_uri&, nlohmann::json&) {},
                                                           nlohmann::json_schema::default_string_format_check);
    static bool initialized = false;
    static std::mutex validator_mutex;

    {
        std::lock_guard<std::mutex> lock(validator_mutex);
        if (!initialized) {
            validator.set_root_schema(schema_json);
            initialized = true;
        }
    }

    nlohmann::json data = yaml_node_to_json(root);
    SchemaErrorCollector err_handler;

    try {
        std::lock_guard<std::mutex> lock(validator_mutex);
        validator.validate(data, err_handler);
    } catch (const std::exception& e) {
        throw ConfigurationError("Configuration error: Schema validation exception: " + std::string(e.what()));
    }

    if (!err_handler.errors.empty()) {
        std::ostringstream oss;
        oss << "Configuration error: Schema validation failed (" << err_handler.errors.size()
            << (err_handler.errors.size() == 1 ? " error" : " errors") << "):\n";
        for (size_t i = 0; i < err_handler.errors.size(); ++i) {
            oss << "  [" << (i + 1) << "] " << err_handler.errors[i] << "\n";
        }
        throw ConfigurationError(oss.str());
    }
}

} // namespace condor
