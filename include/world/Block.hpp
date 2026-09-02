#pragma once
#include <cstdint>

namespace LightweightMC::World
{

#pragma pack(push, 1)
    struct Block
    {
        uint16_t idAndState{0};
    };
#pragma pack(pop)

} // namespace LightweightMC::World