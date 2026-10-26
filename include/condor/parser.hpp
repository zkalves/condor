/**
 * @file parser.hpp
 * @brief YAML configuration loader, include resolver, and DAG cycle validator.
 * @author Ezequiel Alves
 * @copyright Copyright (C) 2026 Ezequiel Alves. All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
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
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <string>
#include <string_view>
#include "condor/config.hpp"

namespace condor {

/**
 * @class YamlParser
 * @brief Parses pipeline specifications from YAML files or strings into strongly-typed PipelineConfig models.
 *
 * Handles hierarchical defaults inheritance, matrix test replication, explicit seed parameter sweeps,
 * recursive YAML file inclusion, and DAG dependency graph cycle validation via Tarjan's / Kahn's algorithms.
 */
class YamlParser {
public:
    /**
     * @brief Parses a pipeline YAML configuration file from disk.
     * @param filepath Path to the root YAML configuration file.
     * @return Complete, validated PipelineConfig data model.
     * @throws ConfigurationError If file cannot be read, syntax is invalid, or cyclic dependencies exist.
     */
    static PipelineConfig parse_file(const std::string& filepath);

    /**
     * @brief Parses a pipeline configuration from an in-memory YAML string.
     * @param yaml_content In-memory YAML content to parse.
     * @param base_dir Directory used to resolve relative file includes (default: ".").
     * @return Complete, validated PipelineConfig data model.
     * @throws ConfigurationError If syntax is invalid or configuration rules are violated.
     */
    static PipelineConfig parse_string(std::string_view yaml_content, const std::string& base_dir = ".");

    /**
     * @brief Validates identifiers, variables, backend assignments, and dependency references in a pipeline model.
     * @param config Pipeline configuration to validate.
     * @throws ConfigurationError If invalid identifiers, unknown backends, or unresolved dependencies are found.
     */
    static void validate(const PipelineConfig& config);

    /**
     * @brief Checks the pipeline stage and job dependency graph for cycles.
     * @param config Pipeline configuration to check.
     * @throws ConfigurationError If a cycle is detected in stage dependencies.
     */
    static void check_dag_cycles(const PipelineConfig& config);

private:
    static void populate_default_backends(PipelineConfig& config);
};

} // namespace condor
