/**
 * @file expression.hpp
 * @brief Zero-dependency boolean condition and arithmetic comparison expression evaluator.
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
#include "condor/variable.hpp"

namespace condor {

/**
 * @class ExpressionEvaluator
 * @brief Evaluates conditional expressions used in `enabled_if` directives for stages and jobs.
 *
 * Supports comparison operators (`==`, `!=`, `<`, `<=`, `>`, `>=`), boolean literals (`true`, `false`),
 * truthy integer evaluation (`0`, `1`), string comparisons (`"${mode}" == "fast"`), and compound
 * logic (`&&`, `||`).
 */
class ExpressionEvaluator {
public:
    /**
     * @brief Evaluates an expression string after interpolating variables against a VariableManager.
     * @param expression Conditional expression string (e.g. `"${coverage} == 1"`).
     * @param vars VariableManager providing variable bindings.
     * @return True if condition evaluates to true, false otherwise. Returns true if expression is empty.
     */
    static bool evaluate(std::string_view expression, const VariableManager& vars);

    /**
     * @brief Evaluates a raw expression string whose variables have already been substituted.
     * @param expr Pre-interpolated expression string (e.g. `"1 == 1"`).
     * @return True if expression evaluates to true, false otherwise.
     */
    static bool evaluate_interpolated(std::string_view expr);

private:
    static std::string trim(std::string_view s);
    static std::string unquote(std::string_view s);
    static bool eval_simple_comparison(std::string_view left, std::string_view op, std::string_view right);
    static bool is_truthy(std::string_view val);
};

} // namespace condor
