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

#include <iostream>
#include <string>
#include <vector>
#include <functional>

// Lightweight test framework for condor
struct TestCase {
    std::string name;
    std::function<void()> func;
};

std::vector<TestCase>& get_test_registry() {
    static std::vector<TestCase> registry;
    return registry;
}

void register_test(const std::string& name, std::function<void()> func) {
    get_test_registry().push_back({name, func});
}

#define TEST_CASE(name) \
    void test_##name(); \
    struct Register_##name { \
        Register_##name() { register_test(#name, test_##name); } \
    } global_register_##name; \
    void test_##name()

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed at " << __FILE__ << ":" << __LINE__ \
                      << ": " << #cond << std::endl; \
            std::exit(1); \
        } \
    } while(0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

#define ASSERT_EQ(a, b) \
    do { \
        if (!((a) == (b))) { \
            std::cerr << "Assertion failed at " << __FILE__ << ":" << __LINE__ \
                      << ": " << #a << " == " << #b << " (left: " << (a) \
                      << ", right: " << (b) << ")" << std::endl; \
            std::exit(1); \
        } \
    } while(0)

#define ASSERT_THROWS(expr, ExceptionType) \
    do { \
        bool threw = false; \
        try { \
            expr; \
        } catch (const ExceptionType&) { \
            threw = true; \
        } catch (...) { \
            std::cerr << "Assertion failed: unexpected exception thrown at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
        if (!threw) { \
            std::cerr << "Assertion failed: expected exception " << #ExceptionType \
                      << " not thrown at " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while(0)

int main() {
    const auto& tests = get_test_registry();
    std::cout << "[TEST RUNNER] Running " << tests.size() << " test cases...\n";

    int passed = 0;
    for (const auto& test : tests) {
        std::cout << "  - Running " << test.name << "... " << std::flush;
        try {
            test.func();
            std::cout << "PASSED\n";
            passed++;
        } catch (const std::exception& e) {
            std::cout << "FAILED (exception: " << e.what() << ")\n";
            return 1;
        }
    }

    std::cout << "\n[TEST RUNNER] All " << passed << " tests passed successfully!\n";
    return 0;
}
