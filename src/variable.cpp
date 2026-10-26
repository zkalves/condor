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

#include "condor/variable.hpp"
#include "condor/logging.hpp"
#include <cctype>
#include <sstream>

namespace condor {

void VariableManager::set(std::string_view key, std::string_view value) {
    if (!is_valid_identifier(key)) {
        throw ConfigurationError("Invalid variable name: '" + std::string(key) +
                                 "'. Must match POSIX-safe identifier regex ^[a-zA-Z0-9_-]+$");
    }
    vars_[std::string(key)] = std::string(value);
}

void VariableManager::set_from_definition(std::string_view def) {
    auto pos = def.find('=');
    if (pos == std::string_view::npos) {
        throw ConfigurationError("Invalid variable definition: '" + std::string(def) +
                                 "'. Format must be KEY=VAL");
    }
    std::string_view key = def.substr(0, pos);
    std::string_view val = def.substr(pos + 1);
    set(key, val);
}

std::string VariableManager::get(std::string_view key, std::string_view default_val) const {
    auto it = vars_.find(std::string(key));
    if (it != vars_.end()) {
        return it->second;
    }
    return std::string(default_val);
}

bool VariableManager::has(std::string_view key) const {
    return vars_.find(std::string(key)) != vars_.end();
}

std::string VariableManager::interpolate(std::string_view input) const {
    std::string result;
    result.reserve(input.size());

    size_t i = 0;
    while (i < input.size()) {
        if (input[i] == '$' && i + 1 < input.size() && input[i + 1] == '{') {
            size_t end_pos = input.find('}', i + 2);
            if (end_pos != std::string_view::npos) {
                std::string_view var_name = input.substr(i + 2, end_pos - (i + 2));
                auto it = vars_.find(std::string(var_name));
                if (it != vars_.end()) {
                    result.append(it->second);
                } else {
                    CONDOR_LOG_DEBUG("Undefined variable in interpolation: ${" + std::string(var_name) + "}");
                    // Fall back to empty string
                }
                i = end_pos + 1;
                continue;
            }
        }
        result.push_back(input[i]);
        ++i;
    }
    return result;
}

std::string VariableManager::to_env_key(std::string_view key) {
    std::string env_key = "CONDOR_VAR_";
    for (char c : key) {
        if (c == '-') {
            env_key.push_back('_');
        } else {
            env_key.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        }
    }
    return env_key;
}

std::vector<std::pair<std::string, std::string>> VariableManager::get_env_pairs() const {
    std::vector<std::pair<std::string, std::string>> pairs;
    pairs.reserve(vars_.size());
    for (const auto& [key, val] : vars_) {
        pairs.emplace_back(to_env_key(key), val);
    }
    return pairs;
}

} // namespace condor
