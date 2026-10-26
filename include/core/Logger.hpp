#pragma once
#include <string_view>
#include <iostream>
// #define LOG_LEVEL_TRACE
namespace LightweightMC::Core
{

    enum class LogLevel
    {
        INFO = 0,
        WARN = 1,
        ERROR = 2,
        DEBUG = 3,
        BENCH = 4,
        TRACE = 5
    };

    // Verbosity levels mapping:
    // Undefined / Default: SHOW_INFO_WARN_ERR (0) -> INFO, WARN, ERROR
    // LOG_LEVEL_DEBUG (1)                        -> INFO, WARN, ERROR, DEBUG
    // LOG_LEVEL_BENCH (2)                        -> INFO, WARN, ERROR, DEBUG, BENCH
    // LOG_LEVEL_TRACE (3)                        -> ALL (INFO, WARN, ERROR, DEBUG, BENCH, TRACE)

#if defined(LOG_LEVEL_TRACE)
#define CURRENT_LOG_LEVEL 5
#elif defined(LOG_LEVEL_BENCH)
#define CURRENT_LOG_LEVEL 4
#elif defined(LOG_LEVEL_DEBUG)
#define CURRENT_LOG_LEVEL 3
#else
#define CURRENT_LOG_LEVEL 2 // Only INFO, WARN, ERROR by default
#endif

    class Logger
    {
    public:
        static void log(LogLevel level, std::string_view msg);

        static void info(std::string_view msg)
        {
            if constexpr (static_cast<int>(LogLevel::INFO) <= CURRENT_LOG_LEVEL)
                log(LogLevel::INFO, msg);
        }

        static void warn(std::string_view msg)
        {
            if constexpr (static_cast<int>(LogLevel::WARN) <= CURRENT_LOG_LEVEL)
                log(LogLevel::WARN, msg);
        }

        static void error(std::string_view msg)
        {
            if constexpr (static_cast<int>(LogLevel::ERROR) <= CURRENT_LOG_LEVEL)
                log(LogLevel::ERROR, msg);
        }

        static void debug(std::string_view msg)
        {
            if constexpr (static_cast<int>(LogLevel::DEBUG) <= CURRENT_LOG_LEVEL)
                log(LogLevel::DEBUG, msg);
        }

        static void bench(std::string_view msg)
        {
            if constexpr (static_cast<int>(LogLevel::BENCH) <= CURRENT_LOG_LEVEL)
                log(LogLevel::BENCH, msg);
        }

        static void trace(std::string_view msg)
        {
            if constexpr (static_cast<int>(LogLevel::TRACE) <= CURRENT_LOG_LEVEL)
                log(LogLevel::TRACE, msg);
        }
    };

} // namespace LightweightMC::Core