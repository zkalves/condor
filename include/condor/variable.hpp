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
 * @file variable.hpp
 * @brief Variable interpolation manager and environment variable exporter.
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
#include <unordered_map>
#include <vector>
#include "condor/common.hpp"

namespace condor {

/**
 * @class VariableManager
 * @brief Manages pipeline variables, recursive `${VAR}` interpolation, and `CONDOR_VAR_*` environment exports.
 *
 * Variables can be declared in YAML files (at pipeline, stage, or job level) or overridden on the command line
 * via `--define-variable KEY=VAL` or `-D KEY=VAL`. The manager enforces POSIX-safe identifier names and
 * provides automatic prefixing when exporting variables to process environments.
 */
class VariableManager {
public:
    VariableManager() = default;

    /**
     * @brief Sets or updates a variable key-value pair after validating identifier syntax.
     * @param key Variable name (must match `^[a-zA-Z0-9_-]+$`).
     * @param value String value to store.
     * @throws ConfigurationError If key contains illegal characters.
     */
    void set(std::string_view key, std::string_view value);

    /**
     * @brief Parses and stores a variable definition formatted as `KEY=VALUE`.
     * @param def String definition from command-line argument.
     * @throws ConfigurationError If definition lacks an `=` delimiter or key is invalid.
     */
    void set_from_definition(std::string_view def);

    /**
     * @brief Retrieves a variable value by key.
     * @param key Variable name to look up.
     * @param default_val Fallback value if key is not defined.
     * @return Stored value string or default_val.
     */
    std::string get(std::string_view key, std::string_view default_val = "") const;

    /**
     * @brief Checks if a variable key is registered.
     * @param key Variable name.
     * @return True if key exists, false otherwise.
     */
    bool has(std::string_view key) const;

    /**
     * @brief Replaces all `${KEY}` tokens in input with their defined variable values.
     * @param input Input template string.
     * @return String with all defined tokens substituted.
     */
    std::string interpolate(std::string_view input) const;

    /**
     * @brief Converts a variable key into its standard uppercase environment format (`CONDOR_VAR_<UPPER_KEY>`).
     * @param key Variable name.
     * @return Environment variable name string.
     */
    static std::string to_env_key(std::string_view key);

    /**
     * @brief Exports all variables as key-value pairs formatted for POSIX process environments.
     * @return Vector of pairs containing environment variable names and their values.
     */
    std::vector<std::pair<std::string, std::string>> get_env_pairs() const;

    /**
     * @brief Direct read-only access to underlying map of variables.
     * @return Reference to unordered_map of variable keys and values.
     */
    const std::unordered_map<std::string, std::string>& variables() const { return vars_; }

private:
    std::unordered_map<std::string, std::string> vars_;
};

} // namespace condor
