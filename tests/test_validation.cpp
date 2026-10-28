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

#include "condor/common.hpp"
#include "condor/parser.hpp"
#include "condor/menu.hpp"
#include "condor/backend.hpp"
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
#define ASSERT_THROWS(expr, ExceptionType) \
    do { \
        bool threw = false; \
        try { expr; } catch (const ExceptionType&) { threw = true; } \
        if (!threw) std::exit(1); \
    } while(0)

using namespace condor;

TEST_CASE(identifier_validation) {
    ASSERT_TRUE(is_valid_identifier("build"));
    ASSERT_TRUE(is_valid_identifier("build_rtl"));
    ASSERT_TRUE(is_valid_identifier("stage-1"));
    ASSERT_TRUE(is_valid_identifier("Stage_2-TEST"));
    ASSERT_TRUE(is_valid_identifier("0123_abc-XYZ"));

    // Invalid identifiers: spaces, slashes, punctuation, empty
    ASSERT_FALSE(is_valid_identifier(""));
    ASSERT_FALSE(is_valid_identifier("build rtl"));
    ASSERT_FALSE(is_valid_identifier("stage/1"));
    ASSERT_FALSE(is_valid_identifier("stage.1"));
    ASSERT_FALSE(is_valid_identifier("test@stage"));
    ASSERT_FALSE(is_valid_identifier("test:stage"));
}

TEST_CASE(invalid_stage_name_yaml) {
    std::string invalid_yaml = R"(
stages:
  - name: "invalid stage name"
    command: "echo test"
)";
    ASSERT_THROWS(YamlParser::parse_string(invalid_yaml), ConfigurationError);
}

TEST_CASE(dag_cycle_detection) {
    std::string cycle_yaml = R"(
stages:
  - name: stage_a
    depends_on: [stage_b]
    command: "echo a"
  - name: stage_b
    depends_on: [stage_a]
    command: "echo b"
)";
    ASSERT_THROWS(YamlParser::parse_string(cycle_yaml), ConfigurationError);
}

TEST_CASE(dag_unknown_dependency) {
    std::string unknown_dep_yaml = R"(
stages:
  - name: stage_a
    depends_on: [non_existent_stage]
    command: "echo a"
)";
    ASSERT_THROWS(YamlParser::parse_string(unknown_dep_yaml), ConfigurationError);
}

TEST_CASE(seed_and_seeds_rejection) {
    // 1. job with 'seed'
    std::string yaml_seed = R"(
stages:
  - name: sim
    jobs:
      - name: fuzz
        command: "run --test=fuzz"
        seed: 42
)";
    ASSERT_THROWS(YamlParser::parse_string(yaml_seed), ConfigurationError);

    // 2. job with 'seeds'
    std::string yaml_seeds = R"(
stages:
  - name: sim
    jobs:
      - name: fuzz
        command: "run --test=fuzz"
        seeds: [42, 1337]
)";
    ASSERT_THROWS(YamlParser::parse_string(yaml_seeds), ConfigurationError);
}

TEST_CASE(matrix_expansion_and_defaults) {
    std::string yaml = R"(
defaults:
  job:
    command: "sim --test=${TEST_NAME} --seed=${SEED}"
stages:
  - name: sim
    jobs:
      - name: arithmetic
        variables:
          SEED: "100"
      - name: fuzz
        matrix:
          SEED: [42, 1337]
)";
    auto config = YamlParser::parse_string(yaml);
    ASSERT_TRUE(config.stages.size() == 1);
    const auto& stage = config.stages[0];
    // arithmetic (1 job) + fuzz (2 matrix items) = 3 jobs
    ASSERT_TRUE(stage.jobs.size() == 3);

    // Job 0: arithmetic
    ASSERT_TRUE(stage.jobs[0].name == "arithmetic");
    ASSERT_TRUE(stage.jobs[0].command == "sim --test=${TEST_NAME} --seed=${SEED}");
    ASSERT_TRUE(stage.jobs[0].variables.at("TEST_NAME") == "arithmetic");
    ASSERT_TRUE(stage.jobs[0].variables.at("SEED") == "100");

    // Job 1: fuzz_SEED_42
    ASSERT_TRUE(stage.jobs[1].name == "fuzz_SEED_42");
    ASSERT_TRUE(stage.jobs[1].variables.at("TEST_NAME") == "fuzz");
    ASSERT_TRUE(stage.jobs[1].variables.at("SEED") == "42");

    // Job 2: fuzz_SEED_1337
    ASSERT_TRUE(stage.jobs[2].name == "fuzz_SEED_1337");
    ASSERT_TRUE(stage.jobs[2].variables.at("TEST_NAME") == "fuzz");
    ASSERT_TRUE(stage.jobs[2].variables.at("SEED") == "1337");
}

TEST_CASE(matrix_multidimensional_cartesian_product) {
    std::string yaml = R"(
stages:
  - name: build
    jobs:
      - name: compile
        command: "build -arch=${ARCH} -opt=${OPT}"
        matrix:
          ARCH: ["x86", "arm"]
          OPT: ["O0", "O2"]
)";
    auto config = YamlParser::parse_string(yaml);
    ASSERT_TRUE(config.stages[0].jobs.size() == 4);
    const auto& jobs = config.stages[0].jobs;
    ASSERT_TRUE(jobs[0].variables.at("ARCH") == "x86" && jobs[0].variables.at("OPT") == "O0");
    ASSERT_TRUE(jobs[1].variables.at("ARCH") == "x86" && jobs[1].variables.at("OPT") == "O2");
    ASSERT_TRUE(jobs[2].variables.at("ARCH") == "arm" && jobs[2].variables.at("OPT") == "O0");
    ASSERT_TRUE(jobs[3].variables.at("ARCH") == "arm" && jobs[3].variables.at("OPT") == "O2");
}

TEST_CASE(count_multiplier_and_scalable_flag) {
    std::string yaml = R"(
settings:
  count_multiplier: 3
stages:
  - name: test_stage
    jobs:
      - name: fuzz_scalable
        count: 2
        scalable: true
      - name: fixed_repro
        count: 2
        scalable: false
      - name: matrix_fixed
        matrix:
          SEED: [42, 1337]
)";
    auto config = YamlParser::parse_string(yaml);
    const auto& jobs = config.stages[0].jobs;
    // fuzz_scalable: 2 * 3 = 6 jobs
    // fixed_repro: exactly 2 jobs (scalable: false)
    // matrix_fixed: exactly 2 jobs (no count specified)
    // Total: 6 + 2 + 2 = 10 jobs
    ASSERT_TRUE(jobs.size() == 10);

    // Verify CLI multiplier override
    auto config_override = YamlParser::parse_string(yaml, ".", 5);
    // fuzz_scalable: 2 * 5 = 10 jobs
    // fixed_repro: exactly 2 jobs (scalable: false)
    // matrix_fixed: exactly 2 jobs
    // Total: 10 + 2 + 2 = 14 jobs
    ASSERT_TRUE(config_override.stages[0].jobs.size() == 14);
}

TEST_CASE(count_expansion) {
    std::string yaml = R"(
defaults:
  job:
    command: "sim --test=${TEST_NAME} --seed=${SEED}"
stages:
  - name: sim
    jobs:
      - name: stress
        count: 3
)";
    auto config = YamlParser::parse_string(yaml);
    ASSERT_TRUE(config.stages[0].jobs.size() == 3);
    ASSERT_TRUE(config.stages[0].jobs[0].name == "stress_0");
    ASSERT_TRUE(config.stages[0].jobs[0].variables.at("INDEX") == "0");
    ASSERT_TRUE(config.stages[0].jobs[0].variables.at("SEED") == "1");

    ASSERT_TRUE(config.stages[0].jobs[1].name == "stress_1");
    ASSERT_TRUE(config.stages[0].jobs[1].variables.at("INDEX") == "1");
    ASSERT_TRUE(config.stages[0].jobs[1].variables.at("SEED") == "2");

    ASSERT_TRUE(config.stages[0].jobs[2].name == "stress_2");
    ASSERT_TRUE(config.stages[0].jobs[2].variables.at("INDEX") == "2");
    ASSERT_TRUE(config.stages[0].jobs[2].variables.at("SEED") == "3");
}

TEST_CASE(gpl_interactive_notice) {
    std::ostringstream oss;
    RuntimeMenu::print_notice(oss);
    std::string text = oss.str();
    ASSERT_TRUE(text.find("condor  Copyright (C) 2026  Ezequiel Alves") != std::string::npos);
    ASSERT_TRUE(text.find("This program comes with ABSOLUTELY NO WARRANTY; for details type `condor --warranty'.") != std::string::npos);
    ASSERT_TRUE(text.find("This is free software, and you are welcome to redistribute it") != std::string::npos);
    ASSERT_TRUE(text.find("under certain conditions; type `condor --license' for details.") != std::string::npos);
}

TEST_CASE(gpl_warranty_text) {
    std::ostringstream oss;
    RuntimeMenu::print_warranty(oss);
    std::string text = oss.str();
    ASSERT_TRUE(text.find("15. Disclaimer of Warranty.") != std::string::npos);
    ASSERT_TRUE(text.find("16. Limitation of Liability.") != std::string::npos);
    ASSERT_TRUE(text.find("THERE IS NO WARRANTY FOR THE PROGRAM") != std::string::npos);
}

TEST_CASE(gpl_conditions_text) {
    std::ostringstream oss;
    RuntimeMenu::print_conditions(oss);
    std::string text = oss.str();
    ASSERT_TRUE(text.find("4. Conveying Verbatim Copies.") != std::string::npos);
    ASSERT_TRUE(text.find("5. Conveying Modified Source Versions.") != std::string::npos);
    ASSERT_TRUE(text.find("GNU General Public License") != std::string::npos);
}

TEST_CASE(backend_registry_discovery) {
    auto backends = BackendRegistry::discover_backends();
    // Verify required grid backends are discovered from external definition files
    ASSERT_TRUE(backends.find("slurm") != backends.end());
    ASSERT_TRUE(backends.find("sge") != backends.end());
    ASSERT_TRUE(backends.find("gridengine") != backends.end());
    ASSERT_TRUE(backends.find("lsf") != backends.end());
    ASSERT_TRUE(backends.find("pbs") != backends.end());
    ASSERT_TRUE(backends.find("torque") != backends.end());
    ASSERT_TRUE(backends.find("spark") != backends.end());
}

TEST_CASE(backend_load_custom_and_patterns) {
    // 1. IBM Spectrum LSF
    auto lsf = BackendRegistry::load_backend("lsf");
    ASSERT_TRUE(lsf.has_value());
    ASSERT_TRUE(lsf->name == "lsf");
    ASSERT_TRUE(lsf->type == BackendType::TEMPLATE);
    ASSERT_TRUE(lsf->submit_command.find("bsub") != std::string::npos);
    ASSERT_TRUE(lsf->poll_command.find("bjobs") != std::string::npos);
    ASSERT_TRUE(lsf->cancel_command.find("bkill") != std::string::npos);
    ASSERT_TRUE(lsf->success_pattern == "DONE");

    // 2. PBS Professional
    auto pbs = BackendRegistry::load_backend("pbs");
    ASSERT_TRUE(pbs.has_value());
    ASSERT_TRUE(pbs->name == "pbs");
    ASSERT_TRUE(pbs->submit_command.find("qsub") != std::string::npos);
    ASSERT_TRUE(pbs->poll_command.find("qstat") != std::string::npos);
    ASSERT_TRUE(pbs->cancel_command.find("qdel") != std::string::npos);

    // 3. TORQUE Resource Manager
    auto torque = BackendRegistry::load_backend("torque");
    ASSERT_TRUE(torque.has_value());
    ASSERT_TRUE(torque->name == "torque");

    // 4. Apache Spark
    auto spark = BackendRegistry::load_backend("spark");
    ASSERT_TRUE(spark.has_value());
    ASSERT_TRUE(spark->name == "spark");
    ASSERT_TRUE(spark->submit_command.find("spark-submit") != std::string::npos);
    ASSERT_TRUE(spark->poll_command.find("spark-submit") != std::string::npos);
    ASSERT_TRUE(!spark->empty_poll_means_done);

    // 5. Grid Engine Family (SGE / GridEngine)
    auto sge = BackendRegistry::load_backend("sge");
    ASSERT_TRUE(sge.has_value());
    ASSERT_TRUE(sge->name == "sge");
    ASSERT_TRUE(sge->submit_command.find("qsub -terse") != std::string::npos);

    auto ge = BackendRegistry::load_backend("gridengine");
    ASSERT_TRUE(ge.has_value());
    ASSERT_TRUE(ge->name == "gridengine");

    // 6. Slurm
    auto slurm = BackendRegistry::load_backend("slurm");
    ASSERT_TRUE(slurm.has_value());
    ASSERT_TRUE(slurm->name == "slurm");
    ASSERT_TRUE(slurm->submit_command.find("sbatch --parsable") != std::string::npos);
}

TEST_CASE(pipeline_level_backend_configuration) {
    // 1. Valid: settings: backend: lsf
    std::string yaml_settings = R"(
name: test_backend_settings
settings:
  backend: lsf
stages:
  - name: test_stage
    jobs:
      - name: job_2
        command: "echo 2"
)";
    auto config_settings = YamlParser::parse_string(yaml_settings);
    ASSERT_TRUE(config_settings.settings.backend == "lsf");
    ASSERT_TRUE(config_settings.settings.default_backend == "lsf");

    // 2. Valid: settings: backend: slurm
    std::string yaml_slurm_settings = R"(
name: test_slurm_backend_settings
settings:
  backend: slurm
stages:
  - name: test_stage
    jobs:
      - name: job_3
        command: "echo 3"
)";
    auto config_def = YamlParser::parse_string(yaml_slurm_settings);
    ASSERT_TRUE(config_def.settings.backend == "slurm");

    // 3. Error: alias default_backend is rejected
    std::string yaml_default_settings = R"(
name: test_default_backend_settings
settings:
  default_backend: slurm
stages:
  - name: test_stage
    jobs:
      - name: job_3
        command: "echo 3"
)";
    ASSERT_THROWS(YamlParser::parse_string(yaml_default_settings), ConfigurationError);

    // 3. Error: top-level backend: slurm is rejected
    std::string yaml_top = R"(
name: test_backend_top
backend: slurm
stages:
  - name: test_stage
    jobs:
      - name: job_1
        command: "echo 1"
)";
    bool caught_top = false;
    try {
        YamlParser::parse_string(yaml_top);
    } catch (const ConfigurationError& e) {
        caught_top = true;
    }
    ASSERT_TRUE(caught_top);

    // 4. Error: job-level backend is rejected
    std::string yaml_job = R"(
name: test_backend_job
stages:
  - name: test_stage
    jobs:
      - name: job_probe
        command: "echo probe"
        backend: local
)";
    bool caught_job = false;
    try {
        YamlParser::parse_string(yaml_job);
    } catch (const ConfigurationError& e) {
        caught_job = true;
    }
    ASSERT_TRUE(caught_job);

    // 5. Error: stage-level backend is rejected
    std::string yaml_stage = R"(
name: test_backend_stage
stages:
  - name: test_stage
    backend: local
    jobs:
      - name: job_s
        command: "echo s"
)";
    bool caught_stage = false;
    try {
        YamlParser::parse_string(yaml_stage);
    } catch (const ConfigurationError& e) {
        caught_stage = true;
    }
    ASSERT_TRUE(caught_stage);

    // 6. Error: defaults backend is rejected
    std::string yaml_defaults = R"(
name: test_backend_defaults
defaults:
  job:
    backend: local
stages:
  - name: test_stage
    jobs:
      - name: job_d
        command: "echo d"
)";
    bool caught_defaults = false;
    try {
        YamlParser::parse_string(yaml_defaults);
    } catch (const ConfigurationError& e) {
        caught_defaults = true;
    }
    ASSERT_TRUE(caught_defaults);
}

TEST_CASE(schema_validation_unknown_property) {
    std::string yaml_unknown_prop = R"(
name: test_unknown
unknown_top_level_key: 123
stages:
  - name: test_stage
    jobs:
      - name: test_job
        command: "echo test"
)";
    bool caught = false;
    try {
        YamlParser::parse_string(yaml_unknown_prop);
    } catch (const ConfigurationError& e) {
        caught = true;
        std::string msg = e.what();
        ASSERT_TRUE(msg.find("Schema validation failed") != std::string::npos);
    }
    ASSERT_TRUE(caught);
}

TEST_CASE(schema_validation_type_mismatch) {
    std::string yaml_bad_type = R"(
name: test_type
settings:
  max_parallel_jobs: "invalid_string_slot"
stages:
  - name: test_stage
    jobs:
      - name: test_job
        command: "echo test"
)";
    bool caught = false;
    try {
        YamlParser::parse_string(yaml_bad_type);
    } catch (const ConfigurationError& e) {
        caught = true;
        std::string msg = e.what();
        ASSERT_TRUE(msg.find("Schema validation failed") != std::string::npos);
    }
    ASSERT_TRUE(caught);
}

TEST_CASE(schema_validation_invalid_enum) {
    std::string yaml_bad_enum = R"(
name: test_enum
settings:
  on_failure_action: "unsupported_action"
stages:
  - name: test_stage
    jobs:
      - name: test_job
        command: "echo test"
)";
    bool caught = false;
    try {
        YamlParser::parse_string(yaml_bad_enum);
    } catch (const ConfigurationError& e) {
        caught = true;
        std::string msg = e.what();
        ASSERT_TRUE(msg.find("Schema validation failed") != std::string::npos);
    }
    ASSERT_TRUE(caught);
}

TEST_CASE(schema_validation_missing_required_stages) {
    std::string yaml_missing_stages = R"(
name: test_no_stages
settings:
  max_parallel_jobs: 2
)";
    bool caught = false;
    try {
        YamlParser::parse_string(yaml_missing_stages);
    } catch (const ConfigurationError& e) {
        caught = true;
        std::string msg = e.what();
        ASSERT_TRUE(msg.find("Schema validation failed") != std::string::npos);
    }
    ASSERT_TRUE(caught);
}

TEST_CASE(schema_validation_invalid_job_field) {
    std::string yaml_bad_job_field = R"(
name: test_bad_job
stages:
  - name: test_stage
    jobs:
      - name: test_job
        command: "echo 1"
        disallowed_custom_field: true
)";
    bool caught = false;
    try {
        YamlParser::parse_string(yaml_bad_job_field);
    } catch (const ConfigurationError& e) {
        caught = true;
        std::string msg = e.what();
        ASSERT_TRUE(msg.find("Schema validation failed") != std::string::npos);
    }
    ASSERT_TRUE(caught);
}

TEST_CASE(schema_validation_dual_format_options) {
    // Valid dual-format usage: single arguments and sequences for depends_on, hooks, include
    std::string yaml_dual_formats = R"(
name: dual_format_pipeline
hooks:
  pre: ["echo hook 1", "echo hook 2"]
  post: "echo hook done"
stages:
  - name: stage_single_dep
    jobs:
      - name: job_1
        command: "echo 1"
  - name: stage_seq_dep
    depends_on: ["stage_single_dep"]
    hooks:
      pre: "echo pre"
      post:
        - "echo post 1"
        - "echo post 2"
    jobs:
      - name: job_single_dep
        command: "echo test"
        depends_on: "0001"
      - name: job_seq_deps
        command: "echo tests"
        depends_on:
          - "0001"
)";
    auto config = YamlParser::parse_string(yaml_dual_formats);
    ASSERT_TRUE(config.stages.size() == 2);
    ASSERT_TRUE(config.stages[1].depends_on.size() == 1);
    ASSERT_TRUE(config.stages[1].depends_on[0] == "stage_single_dep");
    ASSERT_TRUE(config.hooks.pre_commands.size() == 2);
    ASSERT_TRUE(config.stages[1].hooks.post_commands.size() == 2);
}

TEST_CASE(schema_validation_scalar_only_rejection) {
    // Supplying a sequence/list to scalar-only properties like command must fail schema validation
    std::string yaml_bad_scalar = R"(
name: test_scalar_rejection
stages:
  - name: stage1
    jobs:
      - name: job1
        command: ["echo", "not", "allowed", "as", "list"]
)";
    bool caught = false;
    try {
        YamlParser::parse_string(yaml_bad_scalar);
    } catch (const ConfigurationError& e) {
        caught = true;
        std::string msg = e.what();
        ASSERT_TRUE(msg.find("Schema validation failed") != std::string::npos);
    }
    ASSERT_TRUE(caught);
}

TEST_CASE(schema_validation_stage_command_rejected) {
    // Stage must not declare command directly; commands belong to jobs
    std::string yaml_stage_cmd = R"(
name: test_stage_cmd
stages:
  - name: build
    command: "make build"
)";
    bool caught = false;
    try {
        YamlParser::parse_string(yaml_stage_cmd);
    } catch (const ConfigurationError& e) {
        caught = true;
        std::string msg = e.what();
        ASSERT_TRUE(msg.find("Schema validation failed") != std::string::npos);
    }
    ASSERT_TRUE(caught);
}

TEST_CASE(stage_defaults_and_job_command_args_inheritance) {
    std::string yaml_stage_defaults = R"(
name: test_stage_defs
stages:
  - name: test_stage
    defaults:
      command: "python run.py"
      args: "--quick"
    jobs:
      - name: j1
      - name: j2
        command: "python custom.py"
        args: "--detailed"
)";
    PipelineConfig cfg = YamlParser::parse_string(yaml_stage_defaults);
    ASSERT_TRUE(cfg.stages.size() == 1UL);
    ASSERT_TRUE(cfg.stages[0].jobs.size() == 2UL);

    const auto& j1 = cfg.stages[0].jobs[0];
    ASSERT_TRUE(j1.name == "j1");
    ASSERT_TRUE(j1.command == "python run.py --quick");
    ASSERT_TRUE(j1.variables.find("ARGS") != j1.variables.end());
    ASSERT_TRUE(j1.variables.at("ARGS") == "--quick");

    const auto& j2 = cfg.stages[0].jobs[1];
    ASSERT_TRUE(j2.name == "j2");
    ASSERT_TRUE(j2.command == "python custom.py --detailed");
    ASSERT_TRUE(j2.variables.find("ARGS") != j2.variables.end());
    ASSERT_TRUE(j2.variables.at("ARGS") == "--detailed");
}

TEST_CASE(append_args_three_tier_resolution) {
    std::string yaml_tiers = R"(
name: test_append_tiers
defaults:
  job:
    command: "runner"
    args: "--global"
    append_args: true
stages:
  - name: build
    defaults:
      command: "make"
      args: "-j4"
      append_args: true
    jobs:
      - name: compile
        args: "TARGET=all"
  - name: sim
    defaults:
      command: "./sim"
      args: "--fast"
      append_args: false
    jobs:
      - name: t1
        args: "--seed 100"
        append_args: true
      - name: t2
        args: "--seed 200"
        append_args: false
)";
    PipelineConfig cfg = YamlParser::parse_string(yaml_tiers);
    ASSERT_TRUE(cfg.stages.size() == 2UL);

    // Stage 1 (build):
    // Stage defaults: command="make", args="--global -j4" (appends because append_args: true)
    // Job compile: args="TARGET=all", append_args inherited (true) -> final_args="--global -j4 TARGET=all"
    const auto& j_compile = cfg.stages[0].jobs[0];
    ASSERT_TRUE(j_compile.command == "make --global -j4 TARGET=all");
    ASSERT_TRUE(j_compile.variables.at("ARGS") == "--global -j4 TARGET=all");

    // Stage 2 (sim):
    // Stage defaults: command="./sim", args="--fast" (overrides global because append_args: false)
    // Job t1: args="--seed 100", append_args: true -> final_args="--fast --seed 100"
    const auto& j_t1 = cfg.stages[1].jobs[0];
    ASSERT_TRUE(j_t1.command == "./sim --fast --seed 100");
    ASSERT_TRUE(j_t1.variables.at("ARGS") == "--fast --seed 100");

    // Job t2: args="--seed 200", append_args: false -> final_args="--seed 200" (overrides stage args)
    const auto& j_t2 = cfg.stages[1].jobs[1];
    ASSERT_TRUE(j_t2.command == "./sim --seed 200");
    ASSERT_TRUE(j_t2.variables.at("ARGS") == "--seed 200");
}

TEST_CASE(args_auto_append_vs_explicit_interpolation) {
    std::string yaml_args = R"(
name: test_auto_append
stages:
  - name: s1
    jobs:
      - name: explicit_args
        command: "exec_tool ${ARGS} --after"
        args: "--before"
      - name: auto_append_args
        command: "exec_tool"
        args: "--flag 1"
)";
    PipelineConfig cfg = YamlParser::parse_string(yaml_args);
    ASSERT_TRUE(cfg.stages[0].jobs.size() == 2UL);

    const auto& j_exp = cfg.stages[0].jobs[0];
    // Contains ${ARGS}, so not auto-appended
    ASSERT_TRUE(j_exp.command == "exec_tool ${ARGS} --after");
    ASSERT_TRUE(j_exp.variables.at("ARGS") == "--before");

    const auto& j_auto = cfg.stages[0].jobs[1];
    // Does not contain ${ARGS}, so auto-appended
    ASSERT_TRUE(j_auto.command == "exec_tool --flag 1");
    ASSERT_TRUE(j_auto.variables.at("ARGS") == "--flag 1");
}
