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

extern void register_test(const std::string& name, std::function<void()> func);
#define TEST_CASE(name) \
    void test_##name(); \
    struct Register_##name { \
        Register_##name() { register_test(#name, test_##name); } \
    } global_register_##name; \
    void test_##name()

#define ASSERT_TRUE(cond) if (!(cond)) std::exit(1);
#define ASSERT_FALSE(cond) if (cond) std::exit(1);
#define ASSERT_EQ(a, b) if (!((a) == (b))) std::exit(1);
#define ASSERT_THROWS(expr, ExceptionType) \
    do { \
        bool threw = false; \
        try { expr; } catch (const ExceptionType&) { threw = true; } \
        if (!threw) std::exit(1); \
    } while(0)

using namespace condor;

TEST_CASE(variable_interpolation) {
    VariableManager vars;
    vars.set("arch", "x86_64");
    vars.set("opt_level", "O3");

    std::string interpolated = vars.interpolate("g++ -${opt_level} -march=${arch} main.cpp");
    ASSERT_EQ(interpolated, "g++ -O3 -march=x86_64 main.cpp");
}

TEST_CASE(variable_cli_definitions) {
    VariableManager vars;
    vars.set_from_definition("coverage=1");
    vars.set_from_definition("seed=42");

    ASSERT_EQ(vars.get("coverage"), "1");
    ASSERT_EQ(vars.get("seed"), "42");
}

TEST_CASE(variable_env_export) {
    ASSERT_EQ(VariableManager::to_env_key("coverage"), "CONDOR_VAR_COVERAGE");
    ASSERT_EQ(VariableManager::to_env_key("my-var_test"), "CONDOR_VAR_MY_VAR_TEST");

    VariableManager vars;
    vars.set("coverage", "1");
    auto pairs = vars.get_env_pairs();
    ASSERT_EQ(pairs.size(), 1);
    ASSERT_EQ(pairs[0].first, "CONDOR_VAR_COVERAGE");
    ASSERT_EQ(pairs[0].second, "1");
}

TEST_CASE(variable_invalid_name) {
    VariableManager vars;
    ASSERT_THROWS(vars.set("invalid key", "1"), ConfigurationError);
    ASSERT_THROWS(vars.set_from_definition("invalid key=1"), ConfigurationError);
}
