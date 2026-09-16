#include <fstream>
#include <sys/sysinfo.h>
#include <unistd.h>
#define SECTION "\xC2\xA7"

namespace LightweightMC::Core
{
    // Helper to retrieve RAM consumed by the process (in MB)
    static inline double getProcessRAM()
    {
        std::ifstream statm("/proc/self/statm");
        long pages = 0;
        if (statm >> pages)
        {
            return (pages * sysconf(_SC_PAGESIZE)) / (1024.0 * 1024.0);
        }
        return 0.0;
    }

    // Helper to retrieve the system CPU load
    static inline double getCPUUsage()
    {
        double load[1];
        if (getloadavg(load, 1) != -1)
        {
            return load[0];
        }
        return 0.0;
    }

} // namespace LightweightMC::Core