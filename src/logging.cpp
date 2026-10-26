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

#include "condor/logging.hpp"

namespace condor {

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

void Logger::set_verbose(bool verbose) {
    std::lock_guard<std::mutex> lock(mutex_);
    verbose_ = verbose;
}

bool Logger::is_verbose() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return verbose_;
}

std::string Logger::current_timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
    localtime_r(&tt, &tm_buf);
    std::ostringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

void Logger::log(LogLevel level, std::string_view message) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (level == LogLevel::DEBUG && !verbose_) {
        return;
    }

    std::string_view level_str;
    switch (level) {
        case LogLevel::DEBUG: level_str = "[DEBUG]"; break;
        case LogLevel::INFO:  level_str = "[INFO] "; break;
        case LogLevel::WARN:  level_str = "[WARN] "; break;
        case LogLevel::ERROR: level_str = "[ERROR]"; break;
    }

    std::ostream& out = (level == LogLevel::ERROR) ? std::cerr : std::cout;
    out << "[" << current_timestamp() << "] " << level_str << " " << message << std::endl;
}

} // namespace condor
