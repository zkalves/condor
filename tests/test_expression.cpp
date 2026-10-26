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

extern void register_test(const std::string& name, std::function<void()> func);
#define TEST_CASE(name) \
    void test_##name(); \
    struct Register_##name { \
        Register_##name() { register_test(#name, test_##name); } \
    } global_register_##name; \
    void test_##name()

#define ASSERT_TRUE(cond) if (!(cond)) std::exit(1);
#define ASSERT_FALSE(cond) if (cond) std::exit(1);

using namespace condor;

TEST_CASE(expression_empty) {
    VariableManager vars;
    ASSERT_TRUE(ExpressionEvaluator::evaluate("", vars));
}

TEST_CASE(expression_comparisons) {
    VariableManager vars;
    vars.set("coverage", "1");
    vars.set("mode", "debug");

    ASSERT_TRUE(ExpressionEvaluator::evaluate("${coverage} == '1'", vars));
    ASSERT_FALSE(ExpressionEvaluator::evaluate("${coverage} == '0'", vars));
    ASSERT_TRUE(ExpressionEvaluator::evaluate("${coverage} != '0'", vars));
    ASSERT_TRUE(ExpressionEvaluator::evaluate("${mode} == 'debug'", vars));
    ASSERT_FALSE(ExpressionEvaluator::evaluate("${mode} == 'release'", vars));
}

TEST_CASE(expression_numeric_and_logical) {
    VariableManager vars;
    vars.set("count", "5");

    ASSERT_TRUE(ExpressionEvaluator::evaluate("${count} > 2", vars));
    ASSERT_TRUE(ExpressionEvaluator::evaluate("${count} <= 5", vars));
    ASSERT_FALSE(ExpressionEvaluator::evaluate("${count} < 5", vars));

    ASSERT_TRUE(ExpressionEvaluator::evaluate("true && 1", vars));
    ASSERT_FALSE(ExpressionEvaluator::evaluate("true && false", vars));
    ASSERT_TRUE(ExpressionEvaluator::evaluate("false || true", vars));
    ASSERT_TRUE(ExpressionEvaluator::evaluate("!false", vars));
}
