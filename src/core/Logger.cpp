#include "core/Logger.hpp"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>

namespace LightweightMC::Core
{

    static std::string getFormattedTimestamp()
    {
        using namespace std::chrono;

        auto now = system_clock::now();
        auto now_c = system_clock::to_time_t(now);
        std::tm local_tm = *std::localtime(&now_c);

        auto duration = now.time_since_epoch();
        auto millis = duration_cast<milliseconds>(duration) % 1000;
        auto micros = duration_cast<microseconds>(duration) % 1000;

        std::ostringstream oss;
        oss << std::put_time(&local_tm, "%H:%M:%S")
            << '.' << std::setfill('0') << std::setw(3) << millis.count()
            << '.' << std::setfill('0') << std::setw(3) << micros.count();

        return oss.str();
    }

    void Logger::log(LogLevel level, std::string_view msg)
    {
        std::string timestamp = getFormattedTimestamp();

        switch (level)
        {
        case LogLevel::INFO:
            std::cout << "[" << timestamp << "] \033[32m[INFO]\033[0m " << msg << "\n";
            break;
        case LogLevel::WARNING:
            std::cout << "[" << timestamp << "] \033[33m[WARN]\033[0m " << msg << "\n";
            break;
        case LogLevel::ERROR:
            std::cerr << "[" << timestamp << "] \033[31m[ERR]\033[0m  " << msg << "\n";
            break;
        case LogLevel::DEBUG:
            std::cout << "[" << timestamp << "] \033[34m[DBG]\033[0m  " << msg << "\n";
            break;
        }
    }

}