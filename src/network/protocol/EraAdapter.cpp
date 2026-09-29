#include "network/protocol/EraAdapter.hpp"
#include "network/protocol/adapters/LegacyAdapter.hpp"
#include "network/protocol/adapters/ModernAdapter.hpp"
#include "network/protocol/adapters/LatestAdapter.hpp"

namespace LightweightMC::Network::Protocol
{
    // Factory: returns a pointer to a static adapter instance for the era of the
    // given protocol number. Callers do NOT own the returned pointer (the adapters
    // are process-lifetime singletons).
    EraAdapter *EraAdapter::create(int32_t protocol)
    {
        switch (ProtocolVersion::eraFor(protocol))
        {
        case Era::LEGACY:
            return &LegacyAdapter::instance();
        case Era::MODERN:
            return &ModernAdapter::instance();
        case Era::LATEST:
            return &LatestAdapter::instance();
        default:
            return &LegacyAdapter::instance();
        }
    }
}