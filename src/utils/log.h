#pragma once

#include <spdlog/sinks/basic_file_sink.h>
#include <string_view>

// Map an INI log-level string to a spdlog level. Unknown values fall back to info.
inline spdlog::level::level_enum ParseLogLevel(std::string_view name) {
    if (name == "trace") return spdlog::level::trace;
    if (name == "debug") return spdlog::level::debug;
    if (name == "info") return spdlog::level::info;
    if (name == "warn" || name == "warning") return spdlog::level::warn;
    if (name == "error" || name == "err") return spdlog::level::err;
    return spdlog::level::info;
}

// Set the log level and the flush-on threshold together.
inline void ApplyLogLevel(spdlog::level::level_enum level) {
    spdlog::set_level(level);
    spdlog::flush_on(level);
}

inline void SetupLog() {
    auto logsFolder = SKSE::log::log_directory();
    if (!logsFolder) SKSE::stl::report_and_fail("SKSE log_directory not provided, logs disabled.");
    auto pluginName = SKSE::PluginDeclaration::GetSingleton()->GetName();
    auto logFilePath = *logsFolder / std::format("{}.log", pluginName);
    auto fileLoggerPtr = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath.string(), true);
    auto loggerPtr = std::make_shared<spdlog::logger>("log", std::move(fileLoggerPtr));
    spdlog::set_default_logger(std::move(loggerPtr));
    ApplyLogLevel(spdlog::level::info);
}

void ClearLog();
