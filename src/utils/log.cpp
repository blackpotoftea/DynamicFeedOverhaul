#include "log.h"

void ClearLog() {
    auto logsFolder = SKSE::log::log_directory();
    if (!logsFolder) return;
    auto pluginName = SKSE::PluginDeclaration::GetSingleton()->GetName();
    auto logFilePath = *logsFolder / std::format("{}.log", pluginName);

    auto level = spdlog::get_level();  // preserve the currently configured verbosity

    // Drop and recreate logger - the 'true' param truncates the file
    spdlog::drop("log");
    auto fileLoggerPtr = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath.string(), true);
    auto loggerPtr = std::make_shared<spdlog::logger>("log", std::move(fileLoggerPtr));
    spdlog::set_default_logger(std::move(loggerPtr));
    ApplyLogLevel(level);

    // Log at the active level so the marker shows regardless of verbosity.
    spdlog::log(level, "=== Log Cleared ===");
}
