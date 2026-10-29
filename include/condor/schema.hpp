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

/**
 * @file schema.hpp
 * @brief Declarative JSON Schema validation and YAML-to-JSON conversion for pipeline files.
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
 * along with Condor.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <string>
#include <string_view>
#include <yaml-cpp/yaml.h>
#include <nlohmann/json.hpp>

namespace condor {

/**
 * @brief Returns the embedded JSON schema string defining a valid Condor pipeline.
 * @return String view of the JSON schema.
 */
std::string_view get_pipeline_schema_string();

/**
 * @brief Converts a YAML::Node hierarchy into an equivalent nlohmann::json structure.
 *
 * Accurately preserves scalar typing (integers, floats, booleans, strings) while respecting
 * YAML explicit tags and quoted strings.
 *
 * @param node Root YAML node to convert.
 * @return Equivalent nlohmann::json object/value.
 */
nlohmann::json yaml_node_to_json(const YAML::Node& node);

/**
 * @brief Validates a YAML configuration node against the Condor JSON Schema specification.
 *
 * @param root Parsed YAML root node.
 * @throws ConfigurationError If schema validation fails, detailing exact JSON paths and errors.
 */
void validate_yaml_schema(const YAML::Node& root);

} // namespace condor
