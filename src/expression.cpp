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

#include "condor/expression.hpp"
#include <cctype>
#include <algorithm>
#include <charconv>

namespace condor {

std::string ExpressionEvaluator::trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return std::string(s);
}

std::string ExpressionEvaluator::unquote(std::string_view s) {
    std::string str = trim(s);
    if (str.size() >= 2) {
        if ((str.front() == '\'' && str.back() == '\'') ||
            (str.front() == '"' && str.back() == '"')) {
            return str.substr(1, str.size() - 2);
        }
    }
    return str;
}

bool ExpressionEvaluator::is_truthy(std::string_view val) {
    std::string s = unquote(val);
    std::string lower;
    lower.reserve(s.size());
    for (char c : s) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    if (lower.empty() || lower == "0" || lower == "false" || lower == "no" || lower == "off") {
        return false;
    }
    return true;
}

bool ExpressionEvaluator::eval_simple_comparison(std::string_view left_raw,
                                                 std::string_view op,
                                                 std::string_view right_raw) {
    std::string left = unquote(left_raw);
    std::string right = unquote(right_raw);

    // Try numeric comparison if both can be parsed as double
    char* end1 = nullptr;
    char* end2 = nullptr;
    double d1 = std::strtod(left.c_str(), &end1);
    double d2 = std::strtod(right.c_str(), &end2);

    bool is_num = (!left.empty() && end1 == left.c_str() + left.size()) &&
                  (!right.empty() && end2 == right.c_str() + right.size());

    if (op == "==") {
        return is_num ? (d1 == d2) : (left == right);
    } else if (op == "!=") {
        return is_num ? (d1 != d2) : (left != right);
    } else if (op == "<") {
        return is_num ? (d1 < d2) : (left < right);
    } else if (op == "<=") {
        return is_num ? (d1 <= d2) : (left <= right);
    } else if (op == ">") {
        return is_num ? (d1 > d2) : (left > right);
    } else if (op == ">=") {
        return is_num ? (d1 >= d2) : (left >= right);
    }

    return false;
}

bool ExpressionEvaluator::evaluate_interpolated(std::string_view expr) {
    std::string s = trim(expr);
    if (s.empty()) {
        return true;
    }

    // Split on || (lowest precedence)
    size_t or_pos = s.find("||");
    if (or_pos != std::string::npos) {
        return evaluate_interpolated(s.substr(0, or_pos)) ||
               evaluate_interpolated(s.substr(or_pos + 2));
    }

    // Split on &&
    size_t and_pos = s.find("&&");
    if (and_pos != std::string::npos) {
        return evaluate_interpolated(s.substr(0, and_pos)) &&
               evaluate_interpolated(s.substr(and_pos + 2));
    }

    // Check unary negation !
    if (s.front() == '!') {
        return !evaluate_interpolated(s.substr(1));
    }

    // Check comparison operators
    static const std::vector<std::string> ops = {"==", "!=", "<=", ">=", "<", ">"};
    for (const auto& op : ops) {
        size_t pos = s.find(op);
        if (pos != std::string::npos) {
            std::string left = trim(s.substr(0, pos));
            std::string right = trim(s.substr(pos + op.size()));
            return eval_simple_comparison(left, op, right);
        }
    }

    // Fallback: truthiness check
    return is_truthy(s);
}

bool ExpressionEvaluator::evaluate(std::string_view expression, const VariableManager& vars) {
    if (expression.empty()) {
        return true;
    }
    std::string interpolated = vars.interpolate(expression);
    return evaluate_interpolated(interpolated);
}

} // namespace condor
