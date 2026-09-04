/*
 ==============================================================================

 SoftLimiter - memoryless output ceiling.

 ==============================================================================
 */

#pragma once

#include <cmath>

//==============================================================================
/** Waveshaping ceiling: everything below the threshold passes untouched, and
    everything above is folded through a tanh knee that asymptotes to 1.0.
*/
struct SoftLimiter
{
    /** Level below which the limiter is bit-transparent (~-3.1 dBFS). */
    static constexpr float kThreshold = 0.70f;

    /** Applies the ceiling to one sample. */
    static float process (float x)
    {
        const float mag = std::abs (x);

        if (mag <= kThreshold)
            return x;

        constexpr float range = 1.0f - kThreshold;
        const float shaped = kThreshold + range * std::tanh ((mag - kThreshold) / range);

        return x < 0.0f ? -shaped : shaped;
    }
};
