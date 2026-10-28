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

TEST_CASE(seed_and_count_mutual_exclusion) {
    // 1. count + seed
    std::string yaml_count_seed = R"(
stages:
  - name: sim
    jobs:
      - name: fuzz
        command: "run --test=fuzz"
        count: 5
        seed: 42
)";
    ASSERT_THROWS(YamlParser::parse_string(yaml_count_seed), ConfigurationError);

    // 2. count + seeds
    std::string yaml_count_seeds = R"(
stages:
  - name: sim
    jobs:
      - name: fuzz
        command: "run --test=fuzz"
        count: 5
        seeds: [42, 1337]
)";
    ASSERT_THROWS(YamlParser::parse_string(yaml_count_seeds), ConfigurationError);

    // 3. seed + seeds
    std::string yaml_seed_seeds = R"(
stages:
  - name: sim
    jobs:
      - name: fuzz
        command: "run --test=fuzz"
        seed: 42
        seeds: [42, 1337]
)";
    ASSERT_THROWS(YamlParser::parse_string(yaml_seed_seeds), ConfigurationError);
}

TEST_CASE(seed_expansion_and_defaults) {
    std::string yaml = R"(
stages:
  - name: sim
    defaults:
      command: "sim --test=${TEST_NAME} --seed=${SEED}"
    jobs:
      - name: arithmetic
        seed: 100
      - name: fuzz
        seeds: [42, 1337]
)";
    auto config = YamlParser::parse_string(yaml);
    ASSERT_TRUE(config.stages.size() == 1);
    const auto& stage = config.stages[0];
    // arithmetic (1 job) + fuzz (2 seeds) = 3 jobs
    ASSERT_TRUE(stage.jobs.size() == 3);

    // Job 0: arithmetic
    ASSERT_TRUE(stage.jobs[0].name == "arithmetic");
    ASSERT_TRUE(stage.jobs[0].command == "sim --test=${TEST_NAME} --seed=${SEED}");
    ASSERT_TRUE(stage.jobs[0].variables.at("TEST_NAME") == "arithmetic");
    ASSERT_TRUE(stage.jobs[0].variables.at("SEED") == "100");

    // Job 1: fuzz_seed_42
    ASSERT_TRUE(stage.jobs[1].name == "fuzz_seed_42");
    ASSERT_TRUE(stage.jobs[1].variables.at("TEST_NAME") == "fuzz");
    ASSERT_TRUE(stage.jobs[1].variables.at("SEED") == "42");

    // Job 2: fuzz_seed_1337
    ASSERT_TRUE(stage.jobs[2].name == "fuzz_seed_1337");
    ASSERT_TRUE(stage.jobs[2].variables.at("TEST_NAME") == "fuzz");
    ASSERT_TRUE(stage.jobs[2].variables.at("SEED") == "1337");
}

TEST_CASE(count_expansion) {
    std::string yaml = R"(
stages:
  - name: sim
    defaults:
      command: "sim --test=${TEST_NAME} --seed=${SEED}"
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

    // 2. Valid: settings: default_backend: slurm
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
    auto config_def = YamlParser::parse_string(yaml_default_settings);
    ASSERT_TRUE(config_def.settings.backend == "slurm");
    ASSERT_TRUE(config_def.settings.default_backend == "slurm");

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
