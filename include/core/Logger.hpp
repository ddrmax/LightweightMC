#pragma once
#include <string_view>
#include <iostream>

namespace LightweightMC::Core
{

    enum class LogLevel
    {
        INFO,
        WARNING,
        ERROR,
        DEBUG
    };

    class Logger
    {
    public:
        static void log(LogLevel level, std::string_view msg);
        static void info(std::string_view msg) { log(LogLevel::INFO, msg); }
        static void warn(std::string_view msg) { log(LogLevel::WARNING, msg); }
        static void error(std::string_view msg) { log(LogLevel::ERROR, msg); }
    };

} // namespace LightweightMC::Core