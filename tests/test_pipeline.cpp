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
#include <sstream>

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

TEST_CASE(pipeline_variables_and_environment) {
    std::string pipeline_yaml = R"(
name: env_pipeline
variables:
  seed: "12345"
  coverage: "0"
stages:
  - name: stage_vars
    jobs:
      - name: dump_env
        command: "echo $CONDOR_VAR_SEED > seed.txt && echo $CONDOR_JOB_ID > jid.txt"
  - name: stage_skipped
    enabled_if: "${coverage} == '1'"
    command: "touch skipped.txt"
  - name: stage_dependent
    depends_on: [stage_skipped]
    command: "echo 'dependent ran' > dep.txt"
)";

    std::string test_dir = "./test_pipe_env";
    std::error_code ec;
    std::filesystem::remove_all(test_dir, ec);

    auto config = YamlParser::parse_string(pipeline_yaml);
    config.settings.no_interactive = true;

    WorkspaceManager ws(test_dir);
    ws.initialize();

    Scheduler sched(config, ws);
    bool ok = sched.run();

    ASSERT_TRUE(ok);

    // Verify seed.txt contains 12345
    std::ifstream seed_file(ws.job_dir("stage_vars", "0001") / "seed.txt");
    std::string seed_val;
    seed_file >> seed_val;
    ASSERT_EQ(seed_val, "12345");

    // Verify jid.txt contains 0001
    std::ifstream jid_file(ws.job_dir("stage_vars", "0001") / "jid.txt");
    std::string jid_val;
    jid_file >> jid_val;
    ASSERT_EQ(jid_val, "0001");

    // Verify stage_skipped did not run (directory should not have skipped.txt)
    ASSERT_FALSE(std::filesystem::exists(ws.job_dir("stage_skipped", "0001") / "skipped.txt"));

    // Verify stage_dependent ran because skipped stage satisfied DAG edge
    ASSERT_TRUE(std::filesystem::exists(ws.job_dir("stage_dependent", "0001") / "dep.txt"));

    // Verify HTML report correctly shows "1/1 skipped" and NOT "0/1 passed"
    auto report_path = ws.run_dir() / "report.html";
    ASSERT_TRUE(std::filesystem::exists(report_path));
    std::ifstream report_ifs(report_path);
    std::string report_html((std::istreambuf_iterator<char>(report_ifs)), std::istreambuf_iterator<char>());
    ASSERT_TRUE(report_html.find("1/1 skipped") != std::string::npos);
    ASSERT_TRUE(report_html.find("0/1 passed") == std::string::npos);

    std::filesystem::remove_all(test_dir, ec);
}

TEST_CASE(per_stage_job_id_sequence) {
    std::string pipeline_yaml = R"(
name: per_stage_ids
stages:
  - name: stage_one
    jobs:
      - name: job_a
        command: "echo 1 > a.txt"
      - name: job_b
        command: "echo 2 > b.txt"
  - name: stage_two
    depends_on: [stage_one]
    jobs:
      - name: job_c
        command: "echo 3 > c.txt"
)";

    std::string test_dir = "./test_pipe_seq";
    std::error_code ec;
    std::filesystem::remove_all(test_dir, ec);

    auto config = YamlParser::parse_string(pipeline_yaml);
    config.settings.no_interactive = true;

    ASSERT_EQ(config.stages[0].jobs[0].job_id, "0001");
    ASSERT_EQ(config.stages[0].jobs[1].job_id, "0002");
    // Verify stage_two resets and starts at 0001
    ASSERT_EQ(config.stages[1].jobs[0].job_id, "0001");

    WorkspaceManager ws(test_dir);
    ws.initialize();

    Scheduler sched(config, ws);
    bool ok = sched.run();
    ASSERT_TRUE(ok);

    ASSERT_TRUE(std::filesystem::exists(ws.job_dir("stage_one", "0001") / "a.txt"));
    ASSERT_TRUE(std::filesystem::exists(ws.job_dir("stage_one", "0002") / "b.txt"));
    ASSERT_TRUE(std::filesystem::exists(ws.job_dir("stage_two", "0001") / "c.txt"));

    std::filesystem::remove_all(test_dir, ec);
}

TEST_CASE(output_mode_yaml_parsing) {
    std::string yaml_mode_all = "settings:\n  output_mode: all\nstages:\n  - name: s1\n    command: echo 1\n";
    auto c1 = YamlParser::parse_string(yaml_mode_all);
    ASSERT_TRUE(c1.settings.output_mode == OutputMode::ALL);

    std::string yaml_mode_fail = "settings:\n  terminal_output: failures\nstages:\n  - name: s1\n    command: echo 1\n";
    auto c2 = YamlParser::parse_string(yaml_mode_fail);
    ASSERT_TRUE(c2.settings.output_mode == OutputMode::FAILURES);

    std::string yaml_mode_summary = "settings:\n  output_mode: summary\nstages:\n  - name: s1\n    command: echo 1\n";
    auto c3 = YamlParser::parse_string(yaml_mode_summary);
    ASSERT_TRUE(c3.settings.output_mode == OutputMode::SUMMARY);
}

TEST_CASE(build_dir_and_test_dir_environment) {
    std::string pipeline_yaml = R"(
name: build_test_env_pipeline
stages:
  - name: build_stage
    jobs:
      - name: compile
        command: "mkdir -p bin && echo 'bin_content' > bin/tool"
  - name: test_stage
    depends_on: [build_stage]
    jobs:
      - name: run_test
        command: "cat ${BUILD_DIR}/bin/tool > out.txt && echo $TEST_DIR > test_dir.txt && echo $BUILD_DIR > build_dir.txt"
)";

    std::string test_dir = "./test_pipe_build_test";
    std::error_code ec;
    std::filesystem::remove_all(test_dir, ec);

    auto config = YamlParser::parse_string(pipeline_yaml);
    config.settings.no_interactive = true;

    WorkspaceManager ws(test_dir);
    ws.initialize();

    Scheduler sched(config, ws);
    bool ok = sched.run();
    ASSERT_TRUE(ok);

    // Verify build artifact is in the build job folder
    ASSERT_TRUE(std::filesystem::exists(ws.job_dir("build_stage", "0001") / "bin" / "tool"));

    // Verify test job ran and read tool from ${BUILD_DIR}
    ASSERT_TRUE(std::filesystem::exists(ws.job_dir("test_stage", "0001") / "out.txt"));
    std::ifstream out_file(ws.job_dir("test_stage", "0001") / "out.txt");
    std::string content;
    out_file >> content;
    ASSERT_EQ(content, "bin_content");

    // Verify TEST_DIR points to test job dir
    std::ifstream td_file(ws.job_dir("test_stage", "0001") / "test_dir.txt");
    std::string td_val;
    td_file >> td_val;
    ASSERT_EQ(td_val, ws.job_dir("test_stage", "0001").string());

    // Verify BUILD_DIR points to build job dir
    std::ifstream bd_file(ws.job_dir("test_stage", "0001") / "build_dir.txt");
    std::string bd_val;
    bd_file >> bd_val;
    ASSERT_EQ(bd_val, ws.job_dir("build_stage", "0001").string());

    // Verify no files directly under stage dirs outside job folder
    ASSERT_FALSE(std::filesystem::exists(ws.stage_dir("build_stage") / "bin"));
    ASSERT_FALSE(std::filesystem::exists(ws.stage_dir("test_stage") / "out.txt"));

    // Verify job_runner.sh contains literal command and exports rather than eval CONDOR_COMMAND
    auto runner_path = ws.job_dir("test_stage", "0001") / "job_runner.sh";
    ASSERT_TRUE(std::filesystem::exists(runner_path));
    std::ifstream runner_ifs(runner_path);
    std::stringstream runner_ss;
    runner_ss << runner_ifs.rdbuf();
    std::string runner_content = runner_ss.str();
    ASSERT_TRUE(runner_content.find("export CONDOR_RUN_DIR=") != std::string::npos);
    ASSERT_TRUE(runner_content.find("export BUILD_DIR=") != std::string::npos);
    ASSERT_TRUE(runner_content.find("export TEST_DIR=") != std::string::npos);
    ASSERT_TRUE(runner_content.find("eval \"$CONDOR_COMMAND\"") == std::string::npos); // No opaque eval!

    // Verify HTML report was generated with per-stage elements
    auto report_path = ws.run_dir() / "report.html";
    ASSERT_TRUE(std::filesystem::exists(report_path));
    std::ifstream report_ifs(report_path);
    std::string report_html((std::istreambuf_iterator<char>(report_ifs)), std::istreambuf_iterator<char>());
    ASSERT_TRUE(report_html.find("class=\"stage-nav-bar\"") != std::string::npos);
    ASSERT_TRUE(report_html.find("filterByStage('build_stage')") != std::string::npos);
    ASSERT_TRUE(report_html.find("filterByStage('test_stage')") != std::string::npos);
    ASSERT_TRUE(report_html.find("id=\"stage-sec-build_stage\"") != std::string::npos);
    ASSERT_TRUE(report_html.find("id=\"stage-sec-test_stage\"") != std::string::npos);
    ASSERT_TRUE(report_html.find("id=\"activeStageBanner\"") != std::string::npos);

    std::filesystem::remove_all(test_dir, ec);
}

TEST_CASE(relative_paths_configuration) {
    std::string pipeline_yaml = R"(
name: relative_pipeline
settings:
  path_style: relative
stages:
  - name: build_stage
    jobs:
      - name: compile
        command: "mkdir -p bin && echo 'bin_content' > bin/tool"
  - name: test_stage
    depends_on: [build_stage]
    jobs:
      - name: run_test
        command: "cat ${BUILD_DIR}/bin/tool > out.txt && echo $CONDOR_JOB_DIR > job_dir.txt && echo $CONDOR_STAGE_DIR > stage_dir.txt && echo $CONDOR_RUN_DIR > run_dir.txt && echo $BUILD_DIR > build_dir.txt && echo $TEST_DIR > test_dir.txt"
)";

    std::string test_dir = "./test_pipe_relative";
    std::error_code ec;
    std::filesystem::remove_all(test_dir, ec);

    auto config = YamlParser::parse_string(pipeline_yaml);
    ASSERT_TRUE(config.settings.path_style == PathStyle::RELATIVE);
    config.settings.no_interactive = true;

    WorkspaceManager ws(test_dir);
    ws.initialize();

    Scheduler sched(config, ws);
    bool ok = sched.run();
    ASSERT_TRUE(ok);

    // Verify build artifact in build job folder
    ASSERT_TRUE(std::filesystem::exists(ws.job_dir("build_stage", "0001") / "bin" / "tool"));

    // Verify test job ran and read tool from ${BUILD_DIR}
    ASSERT_TRUE(std::filesystem::exists(ws.job_dir("test_stage", "0001") / "out.txt"));
    std::ifstream out_file(ws.job_dir("test_stage", "0001") / "out.txt");
    std::string content;
    out_file >> content;
    ASSERT_EQ(content, "bin_content");

    // Verify CONDOR_JOB_DIR was "."
    std::ifstream jd_file(ws.job_dir("test_stage", "0001") / "job_dir.txt");
    std::string jd_val;
    jd_file >> jd_val;
    ASSERT_EQ(jd_val, ".");

    // Verify CONDOR_STAGE_DIR was ".."
    std::ifstream sd_file(ws.job_dir("test_stage", "0001") / "stage_dir.txt");
    std::string sd_val;
    sd_file >> sd_val;
    ASSERT_EQ(sd_val, "..");

    // Verify CONDOR_RUN_DIR was "../.."
    std::ifstream rd_file(ws.job_dir("test_stage", "0001") / "run_dir.txt");
    std::string rd_val;
    rd_file >> rd_val;
    ASSERT_EQ(rd_val, "../..");

    // Verify TEST_DIR was "."
    std::ifstream td_file(ws.job_dir("test_stage", "0001") / "test_dir.txt");
    std::string td_val;
    td_file >> td_val;
    ASSERT_EQ(td_val, ".");

    // Verify BUILD_DIR was "../../build_stage/0001"
    std::ifstream bd_file(ws.job_dir("test_stage", "0001") / "build_dir.txt");
    std::string bd_val;
    bd_file >> bd_val;
    ASSERT_EQ(bd_val, "../../build_stage/0001");

    // Verify job_runner.sh has relative exports
    auto runner_path = ws.job_dir("test_stage", "0001") / "job_runner.sh";
    ASSERT_TRUE(std::filesystem::exists(runner_path));
    std::ifstream runner_ifs(runner_path);
    std::stringstream runner_ss;
    runner_ss << runner_ifs.rdbuf();
    std::string runner_content = runner_ss.str();
    ASSERT_TRUE(runner_content.find("export CONDOR_RUN_DIR=\"../..\"") != std::string::npos);
    ASSERT_TRUE(runner_content.find("export CONDOR_STAGE_DIR=\"..\"") != std::string::npos);
    ASSERT_TRUE(runner_content.find("export CONDOR_JOB_DIR=\".\"") != std::string::npos);
    ASSERT_TRUE(runner_content.find("export BUILD_DIR=\"../../build_stage/0001\"") != std::string::npos);
    ASSERT_TRUE(runner_content.find("export TEST_DIR=\".\"") != std::string::npos);

    // Verify job_status.yaml has working_directory: "."
    auto yaml_path = ws.job_dir("test_stage", "0001") / "job_status.yaml";
    ASSERT_TRUE(std::filesystem::exists(yaml_path));
    std::ifstream yaml_ifs(yaml_path);
    std::stringstream yaml_ss;
    yaml_ss << yaml_ifs.rdbuf();
    std::string yaml_content = yaml_ss.str();
    ASSERT_TRUE(yaml_content.find("working_directory: \".\"") != std::string::npos);

    // Verify HTML report contains "Path Style: relative"
    auto report_path = ws.run_dir() / "report.html";
    ASSERT_TRUE(std::filesystem::exists(report_path));
    std::ifstream report_ifs(report_path);
    std::string report_html((std::istreambuf_iterator<char>(report_ifs)), std::istreambuf_iterator<char>());
    ASSERT_TRUE(report_html.find("Path Style:</strong> relative") != std::string::npos);

    std::filesystem::remove_all(test_dir, ec);
}
