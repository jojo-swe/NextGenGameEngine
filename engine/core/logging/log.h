#pragma once

#include "engine/core/types.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <memory>

namespace nge {

// ─── Structured Logging System ────────────────────────────────────────────
// Wraps spdlog initially; will be replaced with custom ring-buffer logger.
class Log {
public:
    static void Init() {
        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        consoleSink->set_pattern("[%T.%e] [%^%l%$] [%s:%#] %v");

        auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("nge_engine.log", true);
        fileSink->set_pattern("[%Y-%m-%d %T.%e] [%l] [%s:%#] %v");

        std::vector<spdlog::sink_ptr> sinks = { consoleSink, fileSink };

        s_engineLogger = std::make_shared<spdlog::logger>("NGE", sinks.begin(), sinks.end());
        s_engineLogger->set_level(spdlog::level::trace);
        s_engineLogger->flush_on(spdlog::level::warn);

        spdlog::set_default_logger(s_engineLogger);
    }

    static void Shutdown() {
        spdlog::shutdown();
    }

    static std::shared_ptr<spdlog::logger>& GetLogger() { return s_engineLogger; }

private:
    inline static std::shared_ptr<spdlog::logger> s_engineLogger;
};

} // namespace nge

// ─── Logging Macros ──────────────────────────────────────────────────────
#define NGE_LOG_TRACE(...)  SPDLOG_LOGGER_TRACE(::nge::Log::GetLogger(), __VA_ARGS__)
#define NGE_LOG_DEBUG(...)  SPDLOG_LOGGER_DEBUG(::nge::Log::GetLogger(), __VA_ARGS__)
#define NGE_LOG_INFO(...)   SPDLOG_LOGGER_INFO(::nge::Log::GetLogger(), __VA_ARGS__)
#define NGE_LOG_WARN(...)   SPDLOG_LOGGER_WARN(::nge::Log::GetLogger(), __VA_ARGS__)
#define NGE_LOG_ERROR(...)  SPDLOG_LOGGER_ERROR(::nge::Log::GetLogger(), __VA_ARGS__)
#define NGE_LOG_FATAL(...)  SPDLOG_LOGGER_CRITICAL(::nge::Log::GetLogger(), __VA_ARGS__)
