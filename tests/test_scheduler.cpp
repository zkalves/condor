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

#include "condor/scheduler.hpp"
#include "condor/parser.hpp"
#include <filesystem>
#include <fstream>

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

TEST_CASE(scheduler_local_execution) {
    std::string test_yaml = R"(
name: test_run
settings:
  max_parallel_jobs: 2
  on_failure_action: abort
stages:
  - name: test_stage
    jobs:
      - name: job_pass
        command: "echo 'hello condor' && pwd > cwd.txt"
)";

    std::string test_dir = "./test_sched_run";
    std::error_code ec;
    std::filesystem::remove_all(test_dir, ec);

    auto config = YamlParser::parse_string(test_yaml);
    config.settings.no_interactive = true;

    WorkspaceManager ws(test_dir);
    ws.initialize();

    Scheduler sched(config, ws);
    bool ok = sched.run();

    ASSERT_TRUE(ok);
    ASSERT_TRUE(std::filesystem::exists(ws.job_dir("test_stage", "0001") / "cwd.txt"));
    ASSERT_TRUE(std::filesystem::exists(ws.job_dir("test_stage", "0001") / "stdout.log"));
    ASSERT_TRUE(std::filesystem::exists(ws.job_dir("test_stage", "0001") / "job_status.txt"));

    std::filesystem::remove_all(test_dir, ec);
}

TEST_CASE(scheduler_allow_failure) {
    std::string test_yaml = R"(
name: test_allow_fail
settings:
  on_failure_action: abort
stages:
  - name: fail_stage
    jobs:
      - name: job_allowed_fail
        command: "exit 1"
        allow_failure: true
)";

    std::string test_dir = "./test_sched_fail";
    std::error_code ec;
    std::filesystem::remove_all(test_dir, ec);

    auto config = YamlParser::parse_string(test_yaml);
    config.settings.no_interactive = true;

    WorkspaceManager ws(test_dir);
    ws.initialize();

    Scheduler sched(config, ws);
    bool ok = sched.run();

    // With allow_failure: true, the pipeline should succeed
    ASSERT_TRUE(ok);

    std::filesystem::remove_all(test_dir, ec);
}
