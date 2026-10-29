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

#include "condor/workspace.hpp"
#include <regex>
#include <fstream>
#include <filesystem>

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

using namespace condor;

TEST_CASE(workspace_folder_format) {
    std::string folder = WorkspaceManager::generate_run_folder_name();
    std::regex re(R"(^\d{8}_\d{6}_\d{6}$)");
    ASSERT_TRUE(std::regex_match(folder, re));
}

TEST_CASE(workspace_initialization_and_symlink) {
    std::string test_base = "./test_condor_runs";
    std::error_code ec;
    std::filesystem::remove_all(test_base, ec);

    WorkspaceManager ws(test_base);
    ws.initialize();

    ASSERT_TRUE(std::filesystem::exists(ws.run_dir()));
    ASSERT_TRUE(std::filesystem::exists(ws.artifacts_dir()));
    ASSERT_TRUE(std::filesystem::exists(ws.pipeline_dir()));
    ASSERT_TRUE(std::filesystem::exists(ws.stages_dir()));
    ASSERT_TRUE(ws.artifacts_dir() == ws.run_dir() / "artifacts");
    ASSERT_TRUE(ws.pipeline_dir() == ws.run_dir() / "pipeline");
    ASSERT_TRUE(ws.stages_dir() == ws.run_dir() / "stages");
    ASSERT_TRUE(std::filesystem::exists(std::filesystem::path(test_base) / "latest"));
    ASSERT_TRUE(std::filesystem::is_symlink(std::filesystem::path(test_base) / "latest"));

    // Check stage and job creation
    ws.create_stage_dir("build_rtl");
    ws.create_job_dir("build_rtl", "0001");
    ASSERT_TRUE(std::filesystem::exists(ws.job_dir("build_rtl", "0001")));
    ASSERT_TRUE(ws.stage_dir("build_rtl") == ws.run_dir() / "stages" / "build_rtl");
    ASSERT_TRUE(ws.job_dir("build_rtl", "0001") == ws.run_dir() / "stages" / "build_rtl" / "0001");

    // Manifest test
    std::vector<StageManifestEntry> entries = {
        {"0001", "compile", "gcc main.c", JobStatus::PASSED}
    };
    ws.update_stage_manifest("build_rtl", entries);
    ASSERT_TRUE(std::filesystem::exists(ws.stage_dir("build_rtl") / "stage_manifest.txt"));

    // Cleanup
    std::filesystem::remove_all(test_base, ec);
}

TEST_CASE(workspace_prune_old_runs) {
    std::string test_base = "./test_prune_runs";
    std::error_code ec;
    std::filesystem::remove_all(test_base, ec);
    std::filesystem::create_directories(test_base);

    // Create 5 dummy timestamped runs
    std::vector<std::string> dummy_runs = {
        "20261029_100000_000001",
        "20261029_100000_000002",
        "20261029_100000_000003",
        "20261029_100000_000004",
        "20261029_100000_000005"
    };
    for (const auto& r : dummy_runs) {
        std::filesystem::create_directories(std::filesystem::path(test_base) / r);
    }

    // Active run dir is the 5th
    WorkspaceManager ws(test_base, (std::filesystem::path(test_base) / dummy_runs.back()).string());

    // Prune keeping 2
    ws.prune_old_runs(2);

    ASSERT_FALSE(std::filesystem::exists(std::filesystem::path(test_base) / dummy_runs[0]));
    ASSERT_FALSE(std::filesystem::exists(std::filesystem::path(test_base) / dummy_runs[1]));
    ASSERT_FALSE(std::filesystem::exists(std::filesystem::path(test_base) / dummy_runs[2]));
    ASSERT_TRUE(std::filesystem::exists(std::filesystem::path(test_base) / dummy_runs[3]));
    ASSERT_TRUE(std::filesystem::exists(std::filesystem::path(test_base) / dummy_runs[4]));

    std::filesystem::remove_all(test_base, ec);
}

TEST_CASE(workspace_emergency_kill_script) {
    std::string test_dir = "./test_ws_kill";
    std::error_code ec;
    std::filesystem::remove_all(test_dir, ec);

    WorkspaceManager ws(test_dir);
    ws.initialize();

    auto script_path = ws.kill_script_path();
    ASSERT_TRUE(std::filesystem::exists(script_path));

    // Verify file permissions include execution
    auto perms = std::filesystem::status(script_path).permissions();
    ASSERT_TRUE((perms & std::filesystem::perms::owner_exec) != std::filesystem::perms::none);

    // Verify PID tracking
    ws.record_active_pid(12345);
    ws.record_active_pid(12346);
    auto pids = ws.get_active_pids();
    ASSERT_TRUE(pids.size() == 2);
    ASSERT_TRUE(pids[0] == 12345);
    ASSERT_TRUE(pids[1] == 12346);

    ws.remove_active_pid(12345);
    pids = ws.get_active_pids();
    ASSERT_TRUE(pids.size() == 1);
    ASSERT_TRUE(pids[0] == 12346);

    // Test WorkspaceManager::kill_run execution
    WorkspaceManager::kill_run(ws.run_dir());

    std::filesystem::remove_all(test_dir, ec);
}
