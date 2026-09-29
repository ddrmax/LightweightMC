#include <fstream>
#include <sys/sysinfo.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string>
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

    // Helper to retrieve the peer IP and port of a connected socket as "IP:Port"
    static inline std::string getIpAndPortFromFd(int fd)
    {
        sockaddr_in addr{};
        socklen_t addrLen = sizeof(addr);
        if (getpeername(fd, (struct sockaddr *)&addr, &addrLen) != 0)
        {
            return "Unknown IP:0";
        }

        char ipBuf[INET_ADDRSTRLEN];
        if (!inet_ntop(AF_INET, &addr.sin_addr, ipBuf, sizeof(ipBuf)))
        {
            return "Unknown IP:0";
        }

        return std::string(ipBuf) + ":" + std::to_string(ntohs(addr.sin_port));
    }
    // Helper to retrieve the peer IP and port of a connected socket whith IP as a string and Port as unsigned 16 bit integer
    static inline bool getIpAndPortFromFd(int fd, std::string &outIp, uint16_t &outPort)
    {
        sockaddr_in addr{};
        socklen_t addrLen = sizeof(addr);

        if (getpeername(fd, reinterpret_cast<struct sockaddr *>(&addr), &addrLen) != 0)
        {
            return false;
        }

        char ipBuf[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &addr.sin_addr, ipBuf, sizeof(ipBuf)) == nullptr)
        {
            return false;
        }

        outIp = ipBuf;
        outPort = ntohs(addr.sin_port);

        return true;
    }
} // namespace LightweightMC::Core