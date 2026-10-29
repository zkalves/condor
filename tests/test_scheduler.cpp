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

TEST_CASE(scheduler_stage_and_job_hooks) {
    std::string test_yaml = R"(
name: test_hooks_pipeline
settings:
  max_parallel_jobs: 1
  on_failure_action: abort
stages:
  - name: hook_stage
    hooks:
      pre:
        - "echo 'stage_pre_1'"
        - "echo 'stage_pre_2'"
      post:
        - "echo 'stage_post_done'"
      always: true
    jobs:
      - name: test_job
        hooks:
          pre:
            - "echo 'job_pre_step'"
          post:
            - "echo 'job_post_step'"
          always: true
        command: "echo 'main_command_done'"
)";

    std::string test_dir = "./test_sched_hooks";
    std::error_code ec;
    std::filesystem::remove_all(test_dir, ec);

    auto config = YamlParser::parse_string(test_yaml);
    config.settings.no_interactive = true;

    WorkspaceManager ws(test_dir);
    ws.initialize();

    Scheduler sched(config, ws);
    bool ok = sched.run();
    ASSERT_TRUE(ok);

    // Verify stage hook log files
    auto stage_dir = ws.stage_dir("hook_stage");
    ASSERT_TRUE(std::filesystem::exists(stage_dir / "pre_hook.log"));
    ASSERT_TRUE(std::filesystem::exists(stage_dir / "post_hook.log"));

    // Verify job hook log files
    auto job_dir = ws.job_dir("hook_stage", "0001");
    ASSERT_TRUE(std::filesystem::exists(job_dir / "pre_hook.log"));
    ASSERT_TRUE(std::filesystem::exists(job_dir / "post_hook.log"));

    std::filesystem::remove_all(test_dir, ec);
}

TEST_CASE(scheduler_hook_allow_failure) {
    std::string test_yaml = R"(
name: test_hook_failure
settings:
  max_parallel_jobs: 1
  on_failure_action: abort
stages:
  - name: stage_allowed
    hooks:
      pre: "exit 42"
      allow_failure: true
    jobs:
      - name: job_hook_allowed
        hooks:
          pre: "exit 99"
          allow_failure: true
        command: "echo 'job running despite hook failure'"
)";

    std::string test_dir = "./test_sched_hook_fail";
    std::error_code ec;
    std::filesystem::remove_all(test_dir, ec);

    auto config = YamlParser::parse_string(test_yaml);
    config.settings.no_interactive = true;

    WorkspaceManager ws(test_dir);
    ws.initialize();

    Scheduler sched(config, ws);
    bool ok = sched.run();
    ASSERT_TRUE(ok);

    std::filesystem::remove_all(test_dir, ec);
}

TEST_CASE(scheduler_stage_defaults_hooks) {
    std::string test_yaml = R"(
name: test_stage_defaults_pipeline
settings:
  max_parallel_jobs: 1
  on_failure_action: abort
defaults:
  stage:
    hooks:
      pre: "echo 'default_stage_pre' > stage_marker.txt"
      post: "echo 'default_stage_post' >> stage_marker.txt"
      always: true
stages:
  - name: stage_alpha
    jobs:
      - name: job_1
        command: "echo 'job_1_running'"
  - name: stage_beta
    depends_on: [stage_alpha]
    jobs:
      - name: job_2
        command: "echo 'job_2_running'"
)";

    std::string test_dir = "./test_sched_stage_defaults";
    std::error_code ec;
    std::filesystem::remove_all(test_dir, ec);

    auto config = YamlParser::parse_string(test_yaml);
    config.settings.no_interactive = true;

    WorkspaceManager ws(test_dir);
    ws.initialize();

    Scheduler sched(config, ws);
    bool ok = sched.run();
    ASSERT_TRUE(ok);

    // Both stages should have pre_hook.log and post_hook.log
    auto alpha_dir = ws.stage_dir("stage_alpha");
    auto beta_dir = ws.stage_dir("stage_beta");
    ASSERT_TRUE(std::filesystem::exists(alpha_dir / "pre_hook.log"));
    ASSERT_TRUE(std::filesystem::exists(alpha_dir / "post_hook.log"));
    ASSERT_TRUE(std::filesystem::exists(beta_dir / "pre_hook.log"));
    ASSERT_TRUE(std::filesystem::exists(beta_dir / "post_hook.log"));

    // Jobs should NOT have pre_hook.log or post_hook.log
    auto job1_dir = ws.job_dir("stage_alpha", "0001");
    auto job2_dir = ws.stage_dir("stage_beta") / "0001";
    ASSERT_FALSE(std::filesystem::exists(job1_dir / "pre_hook.log"));
    ASSERT_FALSE(std::filesystem::exists(job2_dir / "pre_hook.log"));

    std::filesystem::remove_all(test_dir, ec);
}

TEST_CASE(scheduler_unified_defaults_and_pipeline_hooks) {
    std::string test_yaml = R"(
name: test_unified_defaults
settings:
  max_parallel_jobs: 1
  on_failure_action: abort
hooks:
  pre: "echo 'pipeline_pre_run'"
defaults:
  stage:
    hooks:
      pre: "echo 'stage_hook_pre' > stage_marker.txt"
      always: true
  job:
    command: "echo 'job_running' > job_marker.txt"
    hooks:
      pre: "echo 'job_hook_pre' > job_hook_marker.txt"
      always: true
stages:
  - name: stage_unified
    jobs:
      - name: test_unified_job
)";

    std::string test_dir = "./test_sched_unified";
    std::error_code ec;
    std::filesystem::remove_all(test_dir, ec);

    auto config = YamlParser::parse_string(test_yaml);
    config.settings.no_interactive = true;

    WorkspaceManager ws(test_dir);
    ws.initialize();

    Scheduler sched(config, ws);
    bool ok = sched.run();
    ASSERT_TRUE(ok);

    // Verify stage hook executed from defaults.stage.hooks
    auto stage_dir = ws.stage_dir("stage_unified");
    ASSERT_TRUE(std::filesystem::exists(stage_dir / "pre_hook.log"));
    ASSERT_TRUE(std::filesystem::exists(stage_dir / "stage_marker.txt"));

    // Verify global pipeline pre-hook and summary in pipeline/
    ASSERT_TRUE(std::filesystem::exists(ws.pipeline_dir() / "pre_hook.log"));
    ASSERT_TRUE(std::filesystem::exists(ws.pipeline_dir() / "pipeline_summary.yaml"));

    // Verify job hook executed from defaults.job.hooks
    auto job_dir = ws.job_dir("stage_unified", "0001");
    ASSERT_TRUE(std::filesystem::exists(job_dir / "pre_hook.log"));
    ASSERT_TRUE(std::filesystem::exists(job_dir / "job_hook_marker.txt"));
    ASSERT_TRUE(std::filesystem::exists(job_dir / "job_marker.txt"));

    std::filesystem::remove_all(test_dir, ec);
}

TEST_CASE(scheduler_job_html_report_link) {
    std::string test_yaml = R"(
name: test_html_link
settings:
  max_parallel_jobs: 1
  on_failure_action: abort
stages:
  - name: sim_stage
    jobs:
      - name: gen_report
        command: "mkdir -p cov_report && echo '<html><body>Coverage: 98%</body></html>' > cov_report/index.html && echo 'coverage: 98%' > cov.yaml"
)";

    std::string test_dir = "./test_sched_html_link";
    std::error_code ec;
    std::filesystem::remove_all(test_dir, ec);

    auto config = YamlParser::parse_string(test_yaml);
    config.settings.no_interactive = true;

    WorkspaceManager ws(test_dir);
    ws.initialize();

    Scheduler sched(config, ws);
    bool ok = sched.run();
    ASSERT_TRUE(ok);

    // Verify report.html contains link to job's HTML report
    std::filesystem::path report_path = ws.artifacts_dir() / "report.html";
    ASSERT_TRUE(std::filesystem::exists(report_path));

    std::ifstream ifs(report_path);
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ASSERT_TRUE(content.find("cov_report/index.html") != std::string::npos);
    ASSERT_TRUE(content.find("HTML Report") != std::string::npos);

    std::filesystem::remove_all(test_dir, ec);
}

TEST_CASE(scheduler_rerun_failed_only_canonical) {
    std::string yaml_base = R"(
name: base_run
settings:
  on_failure_action: continue
stages:
  - name: sim
    mode: failed_only
    jobs:
      - name: job_pass
        command: "echo pass"
      - name: job_fail
        command: "exit 42"
)";

    std::string base_dir = "./test_sched_rerun_base";
    std::string rerun_dir = "./test_sched_rerun_next";
    std::error_code ec;
    std::filesystem::remove_all(base_dir, ec);
    std::filesystem::remove_all(rerun_dir, ec);

    auto base_config = YamlParser::parse_string(yaml_base);
    base_config.settings.no_interactive = true;

    WorkspaceManager ws_base(base_dir);
    ws_base.initialize();

    Scheduler sched_base(base_config, ws_base);
    sched_base.run();

    // Verify canonical base directory paths
    ASSERT_TRUE(std::filesystem::exists(ws_base.run_dir() / "stages" / "sim" / "0001" / "job_status.yaml"));
    ASSERT_TRUE(std::filesystem::exists(ws_base.run_dir() / "stages" / "sim" / "0002" / "job_status.yaml"));

    // Second run: rerun failed only from base_dir
    std::string yaml_rerun = R"(
name: rerun_run
stages:
  - name: sim
    mode: failed_only
    jobs:
      - name: job_pass
        command: "echo pass"
      - name: job_fail
        command: "echo now_passing"
)";

    auto rerun_config = YamlParser::parse_string(yaml_rerun);
    rerun_config.settings.no_interactive = true;
    rerun_config.settings.rerun_base_dir = ws_base.run_dir().string();

    WorkspaceManager ws_rerun(rerun_dir);
    ws_rerun.initialize();

    Scheduler sched_rerun(rerun_config, ws_rerun);
    bool rerun_ok = sched_rerun.run();
    ASSERT_TRUE(rerun_ok);

    // Job 0001 should be cached (not executed in rerun_dir, so stdout.log does not exist)
    ASSERT_FALSE(std::filesystem::exists(ws_rerun.run_dir() / "stages" / "sim" / "0001" / "stdout.log"));

    // Job 0002 should have been re-executed and passed
    ASSERT_TRUE(std::filesystem::exists(ws_rerun.run_dir() / "stages" / "sim" / "0002" / "stdout.log"));

    std::filesystem::remove_all(base_dir, ec);
    std::filesystem::remove_all(rerun_dir, ec);
}

TEST_CASE(scheduler_resource_semaphore_throttling) {
    std::string test_yaml = R"(
name: test_resource_throttling
resources:
  license_token: 1
settings:
  max_parallel_jobs: 4
stages:
  - name: sim
    jobs:
      - name: sim_a
        command: "echo res=$CONDOR_RESOURCE_LICENSE_TOKEN all=$CONDOR_RESOURCES > out_a.txt && sleep 0.1"
        resources:
          license_token: 1
      - name: sim_b
        command: "echo res=$CONDOR_RESOURCE_LICENSE_TOKEN all=$CONDOR_RESOURCES > out_b.txt && sleep 0.1"
        resources:
          license_token: 1
)";
    std::string test_dir = "./test_sched_res";
    std::error_code ec;
    std::filesystem::remove_all(test_dir, ec);

    auto config = YamlParser::parse_string(test_yaml);
    config.settings.no_interactive = true;

    WorkspaceManager ws(test_dir);
    ws.initialize();

    Scheduler scheduler(config, ws);
    bool ok = scheduler.run();
    ASSERT_TRUE(ok);

    // Verify output files and environment variables
    auto out_a = ws.run_dir() / "stages" / "sim" / "0001" / "out_a.txt";
    auto out_b = ws.run_dir() / "stages" / "sim" / "0002" / "out_b.txt";
    ASSERT_TRUE(std::filesystem::exists(out_a));
    ASSERT_TRUE(std::filesystem::exists(out_b));

    std::ifstream fa(out_a);
    std::string line_a;
    std::getline(fa, line_a);
    ASSERT_TRUE(line_a.find("res=1") != std::string::npos);
    ASSERT_TRUE(line_a.find("all=license_token=1") != std::string::npos);

    std::ifstream fb(out_b);
    std::string line_b;
    std::getline(fb, line_b);
    ASSERT_TRUE(line_b.find("res=1") != std::string::npos);
    ASSERT_TRUE(line_b.find("all=license_token=1") != std::string::npos);

    std::filesystem::remove_all(test_dir, ec);
}

TEST_CASE(scheduler_flaky_retry_success) {
    std::string test_yaml = R"(
name: test_retry_pipeline
stages:
  - name: test_flaky
    jobs:
      - name: flaky_job
        command: "if [ ! -f marker.tmp ]; then touch marker.tmp && exit 42; else echo retry=$CONDOR_RETRY_COUNT > ok.txt && exit 0; fi"
        retry: 2
        retry_delay_sec: 0
        retry_on_exit_codes: [42]
)";
    std::string test_dir = "./test_sched_retry";
    std::error_code ec;
    std::filesystem::remove_all(test_dir, ec);

    auto config = YamlParser::parse_string(test_yaml);
    config.settings.no_interactive = true;

    WorkspaceManager ws(test_dir);
    ws.initialize();

    Scheduler scheduler(config, ws);
    bool ok = scheduler.run();
    ASSERT_TRUE(ok);

    auto ok_txt = ws.run_dir() / "stages" / "test_flaky" / "0001" / "ok.txt";
    ASSERT_TRUE(std::filesystem::exists(ok_txt));

    std::ifstream ifs(ok_txt);
    std::string line;
    std::getline(ifs, line);
    ASSERT_TRUE(line.find("retry=1") != std::string::npos);

    // Verify job status yaml recorded retries
    auto status_yaml = ws.run_dir() / "stages" / "test_flaky" / "0001" / "job_status.yaml";
    ASSERT_TRUE(std::filesystem::exists(status_yaml));
    std::ifstream yfs(status_yaml);
    std::string yline;
    bool found_retries = false;
    while (std::getline(yfs, yline)) {
        if (yline.find("retries: 1") != std::string::npos) found_retries = true;
    }
    ASSERT_TRUE(found_retries);

    std::filesystem::remove_all(test_dir, ec);
}

TEST_CASE(scheduler_purge_successful_logs) {
    std::string test_yaml = R"(
name: test_purge_pipeline
settings:
  purge_successful_logs: true
stages:
  - name: stage_purge
    jobs:
      - name: pass_job
        command: "echo 'passing job output'"
      - name: fail_job
        command: "echo 'failing job output' >&2 && exit 1"
        allow_failure: true
)";
    std::string test_dir = "./test_sched_purge";
    std::error_code ec;
    std::filesystem::remove_all(test_dir, ec);

    auto config = YamlParser::parse_string(test_yaml);
    config.settings.no_interactive = true;

    WorkspaceManager ws(test_dir);
    ws.initialize();

    Scheduler scheduler(config, ws);
    bool ok = scheduler.run();
    ASSERT_TRUE(ok);

    auto pass_stdout = ws.run_dir() / "stages" / "stage_purge" / "0001" / "stdout.log";
    auto fail_stderr = ws.run_dir() / "stages" / "stage_purge" / "0002" / "stderr.err";

    // Successful job log purged
    ASSERT_FALSE(std::filesystem::exists(pass_stdout));

    // Failing job log preserved
    ASSERT_TRUE(std::filesystem::exists(fail_stderr));

    std::filesystem::remove_all(test_dir, ec);
}

TEST_CASE(scheduler_tail_job) {
    std::string test_yaml = R"(
name: test_tail_pipeline
stages:
  - name: test_stage
    jobs:
      - name: output_job
        command: "echo 'Line A' && echo 'Line B' && echo 'Line C'"
)";
    std::string test_dir = "./test_sched_tail";
    std::error_code ec;
    std::filesystem::remove_all(test_dir, ec);

    auto config = YamlParser::parse_string(test_yaml);
    config.settings.no_interactive = true;

    WorkspaceManager ws(test_dir);
    ws.initialize();

    Scheduler scheduler(config, ws);
    bool ok = scheduler.run();
    ASSERT_TRUE(ok);

    // Call tail_job on finished job
    scheduler.tail_job("0001", 10);
    // Also tail non-existent job to verify graceful message
    scheduler.tail_job("9999", 10);

    std::filesystem::remove_all(test_dir, ec);
}
