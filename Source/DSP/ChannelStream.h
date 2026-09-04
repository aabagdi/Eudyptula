/*
 ==============================================================================

 ChannelStream - per-channel state for the streaming cipher path

 ==============================================================================
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "KWeightingFilter.h"

//==============================================================================
/** Per-channel state the cipher path carries between blocks: the input
    accumulator, the cipher output ring, and carrier, gate, envelope and
    loudness state. One instance per audio channel.
*/
struct ChannelStream
{
    static constexpr int kBlockSamples = 8;

    static constexpr int kRingCapacity = 64;

    std::array<int16_t, (size_t) kBlockSamples> inAccum {};
    int inAccumLen = 0;

    std::array<float, (size_t) kRingCapacity> ring {};
    int ringHead = 0, ringTail = 0, ringCount = 0;

    double phase = 0.0;
    float currentOut = 0.0f;

    std::array<float, (size_t) kBlockSamples> lastBlock {};
    int repeatCounter = 0;

    float gateEnv = 0.0f;
    float gateGain = 0.0f;

    float envSq = 0.0f;

    KWeightingState dryK, outK;

    //==============================================================================
    /** Clears all state and primes the ring with one silent block, so a cipher
        tick can always pop a sample. */
    void reset()
    {
        inAccum.fill (0); inAccumLen = 0;
        ring.fill (0.0f); ringHead = ringTail = ringCount = 0;
        phase = 0.0; currentOut = 0.0f;
        lastBlock.fill (0.0f); repeatCounter = 0;
        gateEnv = 0.0f; gateGain = 0.0f;
        envSq = 0.0f;
        dryK.reset(); outK.reset();

        for (int i = 0; i < kBlockSamples; ++i)
            push (0.0f);
    }

    /** Writes one cipher output sample into the ring. */
    void push (float v)
    {
        ring[(size_t) ringTail] = v;
        ringTail = (ringTail + 1) % kRingCapacity;
        ++ringCount;
    }

    /** Reads the next cipher output sample, holding the last value when the
        ring has run dry. */
    float pop()
    {
        if (ringCount <= 0)
            return currentOut;

        const float v = ring[(size_t) ringHead];
        ringHead = (ringHead + 1) % kRingCapacity;
        --ringCount;
        return v;
    }
};
