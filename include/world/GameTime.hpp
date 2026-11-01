#pragma once

// -------------------------------------------------------------
// GameTime — day/night cycle constants + predicates (Java Edition
// 1.8 semantics, user-specified phase layout).
//
// The master clock is a per-plane int64 tick counter advanced at
// 20 Hz by Server.cpp while the `advance_time` gamerule holds; it
// wraps at gamerule `day_length` (default 24 000 ticks = 20 real
// minutes). Phase boundaries follow vanilla-1.8's fixed absolute
// offsets scaled proportionally when day_length is changed:
//
//   [0,        D*1/2)          day      (sunrise at tick 0)
//   [D*1/2,    D*13/24)        sunset   (12 000 .. 13 000 @ D=24 000)
//   [D*13/24,  D*23/24)        night    (13 000 .. 23 000)
//   [D*23/24,  D)              sunrise  (23 000 .. 24 000)
//
// Predicates are pure integer math — no allocations, safe on hot
// paths (EntityManager spawn gating, zombie sunlight check).
// -------------------------------------------------------------

#include <cstdint>

namespace LightweightMC::World
{
    namespace GameTime
    {
        // Vanilla-1.8 defaults (ticks @ 20 Hz).
        inline constexpr int64_t kDefaultDayLength = 24000;
        // Absolute phase offsets for the default day length; derivePhase
        // scales them proportionally when day_length differs.
        inline constexpr int64_t kSunsetStart = 12000; // D/2
        inline constexpr int64_t kNightStart = 13000;  // D*13/24
        inline constexpr int64_t kSunriseStart = 23000; // D*23/24

        // Wrap `tick` into [0, dayLength) then classify the phase:
        // 0=day 1=sunset 2=night 3=sunrise. Integer-only, branch-light.
        inline constexpr int64_t phaseBoundary(int64_t dayLength, int64_t offsetAtDefaultDay)
        {
            return offsetAtDefaultDay * dayLength / kDefaultDayLength;
        }

        inline int phaseOf(int64_t tick, int64_t dayLength)
        {
            if (dayLength <= 0)
                dayLength = kDefaultDayLength;
            int64_t t = tick % dayLength;
            if (t < 0)
                t += dayLength;
            const int64_t sunsetStart = phaseBoundary(dayLength, kSunsetStart);
            const int64_t nightStart = phaseBoundary(dayLength, kNightStart);
            const int64_t sunriseStart = phaseBoundary(dayLength, kSunriseStart);
            if (t < sunsetStart)
                return 0; // day
            if (t < nightStart)
                return 1; // sunset
            if (t < sunriseStart)
                return 2; // night
            return 3;     // sunrise
        }

        // Convenience predicates. Vanilla treats sunset as "day-ish" for
        // mob despawn (mobs burn at dawn = sunrise/day boundary) and night
        // as the hostile-spawn window; sunrise still counts as dark enough
        // to spawn, matching vanilla's `skyBrightness` behaviour where the
        // client sky lerps but server-side spawn uses tick >= 13000 || < ...
        // We keep spawn gating strict: night + sunrise count as "dark".
        inline bool isDay(int64_t tick, int64_t dayLength)
        {
            return phaseOf(tick, dayLength) == 0;
        }
        inline bool isSunset(int64_t tick, int64_t dayLength)
        {
            return phaseOf(tick, dayLength) == 1;
        }
        inline bool isNight(int64_t tick, int64_t dayLength)
        {
            const int p = phaseOf(tick, dayLength);
            return p == 2 || p == 3; // night + sunrise are the dark window
        }

        // Vanilla "mobs burn in sunlight" trigger fires at the day phase
        // (post-sunrise), i.e. everything except the dark window.
        inline bool isSunlight(int64_t tick, int64_t dayLength)
        {
            return !isNight(tick, dayLength);
        }

        // Canonical anchor ticks for /time shortcuts at day_length=24000,
        // scaled proportionally like phaseBoundary does.
        inline int64_t tickAtNoon(int64_t dayLength)
        {
            return phaseBoundary(dayLength, kDefaultDayLength / 2);
        }
        inline int64_t tickAtMidnight(int64_t dayLength)
        {
            return phaseBoundary(dayLength, kNightStart + (kSunriseStart - kNightStart) / 2);
        }
        inline int64_t tickAtDawn(int64_t dayLength)
        {
            return phaseBoundary(dayLength, kSunriseStart); // sunrise start
        }
    } // namespace GameTime
} // namespace LightweightMC::World
