#include "core/Logger.hpp"

namespace LightweightMC::Core {

void Logger::log(LogLevel level, std::string_view msg) {
    switch (level) {
        case LogLevel::INFO:    std::cout << "\033[32m[INFO]\033[0m " << msg << "\n"; break;
        case LogLevel::WARNING: std::cout << "\033[33m[WARN]\033[0m " << msg << "\n"; break;
        case LogLevel::ERROR:   std::cerr << "\033[31m[ERR]\033[0m  " << msg << "\n"; break;
        case LogLevel::DEBUG:   std::cout << "\033[34m[DBG]\033[0m  " << msg << "\n"; break;
    }
}

} // namespace LightweightMC::Core