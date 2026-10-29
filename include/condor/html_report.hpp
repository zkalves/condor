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

/**
 * @file html_report.hpp
 * @brief Zero-dependency standalone HTML execution report generator.
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
#include <vector>
#include <unordered_map>
#include <filesystem>
#include "condor/config.hpp"
#include "condor/workspace.hpp"
#include "condor/progress.hpp"

namespace condor {

struct JobRuntimeState;
struct StageRuntimeState;

/**
 * @class HtmlReportGenerator
 * @brief Generates self-contained, interactive HTML reports with stage tabs, job logs, and status badges.
 *
 * The generated HTML file requires zero external network dependencies, CDN scripts, or external CSS files.
 * It features responsive Solarized styling, interactive stage filter tabs (`All Stages` + per-stage buttons),
 * job status filter buttons (`All`, `Passed`, `Failed`, `Skipped`), embedded terminal logs, and job metadata.
 */
class HtmlReportGenerator {
public:
    /**
     * @brief Generates the complete HTML report content as an in-memory string.
     * @param config Pipeline configuration model.
     * @param workspace WorkspaceManager providing file paths and run directories.
     * @param stages Vector of stage runtime states.
     * @param jobs Map of job runtime states.
     * @param stats Overall pipeline execution statistics.
     * @param success Pipeline success flag.
     * @return Full HTML document string.
     */
    static std::string generate(
        const PipelineConfig& config,
        const WorkspaceManager& workspace,
        const std::vector<StageRuntimeState>& stages,
        const std::unordered_map<std::string, JobRuntimeState>& jobs,
        const ProgressStats& stats,
        bool success
    );

    /**
     * @brief Writes the generated HTML report directly to a file on disk.
     * @param output_path Target filesystem path for the HTML report file.
     * @param config Pipeline configuration model.
     * @param workspace WorkspaceManager providing file paths and run directories.
     * @param stages Vector of stage runtime states.
     * @param jobs Map of job runtime states.
     * @param stats Overall pipeline execution statistics.
     * @param success Pipeline success flag.
     * @return True if file was successfully written, false on I/O error.
     */
    static bool write_report(
        const std::filesystem::path& output_path,
        const PipelineConfig& config,
        const WorkspaceManager& workspace,
        const std::vector<StageRuntimeState>& stages,
        const std::unordered_map<std::string, JobRuntimeState>& jobs,
        const ProgressStats& stats,
        bool success
    );

    /**
     * @brief Writes a standard JUnit/xUnit XML report to disk for CI/CD ingestion.
     * @param output_path Target filesystem path for the XML file.
     * @param config Pipeline configuration model.
     * @param stages Vector of stage runtime states.
     * @param jobs Map of job runtime states.
     * @param stats Overall pipeline execution statistics.
     * @param success Pipeline success flag.
     * @return True if file was successfully written, false on I/O error.
     */
    static bool write_junit_xml(
        const std::filesystem::path& output_path,
        const PipelineConfig& config,
        const std::vector<StageRuntimeState>& stages,
        const std::unordered_map<std::string, JobRuntimeState>& jobs,
        const ProgressStats& stats,
        bool success
    );
};

} // namespace condor
