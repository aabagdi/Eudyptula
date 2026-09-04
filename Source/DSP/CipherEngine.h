/*
 ==============================================================================

 CipherEngine - AES key schedules and per-block encoding

 ==============================================================================
 */

#pragma once

#include <JuceHeader.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "../ThirdParty/tiny_aes.h"

//==============================================================================
/** Owns the encrypt and decrypt AES-256 key schedules and runs one 16-byte
    (8-sample) block at a time, publishing schedules from the message thread
    without ever blocking the audio thread. The enc/dec key selects a timbre.  Each
    passphrase also derives a KeyProfile of ordered macro axes.
*/
class CipherEngine
{
public:
    static constexpr int kBlockSamples = 8;

    static constexpr int kBlockBytes = 2 * kBlockSamples;

    static constexpr int kKeyExpSize = AES_keyExpSize;

    static constexpr float kSampleScale = 30000.0f;

    static constexpr float kCipherRms = 32768.0f / (1.7320508f * kSampleScale);

    using Block = std::array<float, (size_t) kBlockSamples>;

    //==============================================================================
    /** The key-derived structure applied around the cipher itself, every field
        of it expanded from the schedule. */
    struct KeyProfile
    {
        std::array<uint8_t, (size_t) kBlockSamples> slotOrder {};

        std::array<uint8_t, (size_t) kBlockSamples> slotPick {};

        uint8_t repeats = 1;

        int8_t tone = 0;

        uint8_t gaps = 0;
        uint8_t gapStart = 0;

        std::array<uint8_t, (size_t) kBlockBytes> whitening {};
    };

    CipherEngine() = default;

    //==============================================================================
    /** Message thread: rebuilds both schedules from the current passphrases,
        expanding outside the lock to keep the try-lock window tiny. */
    void setKeys (const juce::String& encKey, const juce::String& decKey)
    {
        KeySetup e, d;
        buildSetup (encKey, e);
        buildSetup (decKey, d);

        const juce::SpinLock::ScopedLockType sl (lock);
        pendingEnc = e;
        pendingDec = d;
        dirty.store (true, std::memory_order_release);
    }

    /** Construction only: publishes immediately, before any audio thread
        exists. */
    void primeNow()
    {
        activeEnc = pendingEnc;
        activeDec = pendingDec;
        dirty.store (false);
    }

    //==============================================================================
    /** Audio thread: adopts pending schedules if they are ready, never
        blocking. */
    void refreshIfDirty()
    {
        if (! dirty.load (std::memory_order_acquire))
            return;

        const juce::SpinLock::ScopedTryLockType sl (lock);
        if (sl.isLocked())
        {
            activeEnc = pendingEnc;
            activeDec = pendingDec;
            dirty.store (false, std::memory_order_release);
        }
    }

    /** Audio thread: latches the schedule, profile and direction for the coming
        block. The profile is copied so a refresh cannot disturb encode(). */
    void beginBlock (bool decryptMode)
    {
        decrypting = decryptMode;
        const auto& src = decryptMode ? activeDec : activeEnc;
        std::memcpy (ctx.RoundKey, src.roundKey.data(), (size_t) kKeyExpSize);
        blockProfile = src.profile;
    }

    /** Audio thread: one 8-sample block in, wet floats out. 'corruptSamples'
        is how many samples come from the ciphered bytes; the rest pass through
        as plaintext. */
    void encode (const int16_t* plain, int corruptSamples, Block& out) const
    {
        uint8_t P[kBlockBytes];
        std::memcpy (P, plain, (size_t) kBlockBytes);

        uint8_t bytes[kBlockBytes];
        std::memcpy (bytes, P, (size_t) kBlockBytes);

        if (corruptSamples > 0)
        {
            if (decrypting)
            {
                AES_ECB_decrypt (&ctx, bytes);

                for (int i = 0; i < kBlockBytes; ++i)
                    bytes[i] ^= blockProfile.whitening[(size_t) i];
            }
            else
            {
                for (int i = 0; i < kBlockBytes; ++i)
                    bytes[i] ^= blockProfile.whitening[(size_t) i];

                AES_ECB_encrypt (&ctx, bytes);
            }
        }

        int16_t cipherS16[kBlockSamples], plainS16[kBlockSamples];
        std::memcpy (cipherS16, bytes, (size_t) kBlockBytes);
        std::memcpy (plainS16,  P,     (size_t) kBlockBytes);

        float v[kBlockSamples];
        for (int j = 0; j < kBlockSamples; ++j)
            v[j] = (float) cipherS16[blockProfile.slotOrder[(size_t) j]] / kSampleScale;

        applyMacroAxes (blockProfile, v);

        bool ciphered[kBlockSamples] = {};
        for (int k = 0; k < corruptSamples && k < kBlockSamples; ++k)
            ciphered[blockProfile.slotPick[(size_t) k]] = true;

        for (int j = 0; j < kBlockSamples; ++j)
            out[(size_t) j] = ciphered[j] ? v[j]
                                          : (float) plainS16[j] / kSampleScale;
    }

private:
    //==============================================================================
    /** Applies subdivision, then tone, then sparsity, each stage
        level-compensated, then pulls the block half way back to nominal. */
    static void applyMacroAxes (const KeyProfile& p, float* v)
    {
        if (p.repeats > 1)
        {
            const int len = kBlockSamples / p.repeats;
            for (int j = 0; j < kBlockSamples; ++j)
                v[j] = v[j % len];
        }

        for (int pass = 0; pass < std::abs ((int) p.tone); ++pass)
        {
            float t[kBlockSamples];
            for (int j = 0; j < kBlockSamples; ++j)
            {
                const float prev = v[(j + kBlockSamples - 1) % kBlockSamples];
                t[j] = 0.70710678f * (p.tone < 0 ? v[j] + prev : v[j] - prev);
            }
            std::memcpy (v, t, sizeof t);
        }

        if (p.gaps > 0 && p.gaps < kBlockSamples)
        {
            for (int g = 0; g < (int) p.gaps; ++g)
                v[(p.gapStart + g) % kBlockSamples] = 0.0f;

            const float comp = std::sqrt ((float) kBlockSamples
                                        / (float) (kBlockSamples - p.gaps));
            for (int j = 0; j < kBlockSamples; ++j)
                v[j] *= comp;
        }

        float sq = 0.0f;
        for (int j = 0; j < kBlockSamples; ++j)
            sq += v[j] * v[j];

        const float rms = std::sqrt (sq / (float) kBlockSamples);

        if (rms > 1.0e-6f)
        {
            const float g = std::sqrt (kCipherRms / rms);
            for (int j = 0; j < kBlockSamples; ++j)
                v[j] *= g;
        }
    }

    /** One passphrase's published state: the round key the cipher runs on plus
        the structure derived from it. */
    struct KeySetup
    {
        std::array<uint8_t, (size_t) kKeyExpSize> roundKey {};
        KeyProfile profile;
    };

    /** Expands one passphrase into its AES-256 round key: raw characters,
        zero-padded or truncated to 32 bytes, then derives its profile. */
    static void buildSetup (const juce::String& key, KeySetup& out)
    {
        std::array<uint8_t, 32> aesKey {};
        for (int i = 0; i < key.length() && i < 32; ++i)
            aesKey[(size_t) i] = (uint8_t) key[i];

        AES_ctx local;
        AES_init_ctx (&local, aesKey.data());
        std::memcpy (out.roundKey.data(), local.RoundKey, out.roundKey.size());

        buildProfile (out.roundKey, out.profile);
    }

    /** Derives the slot layout, macro axes and mask from the expanded
        schedule, so every character of the passphrase reaches every field. */
    static void buildProfile (const std::array<uint8_t, (size_t) kKeyExpSize>& schedule,
                              KeyProfile& p)
    {
        uint32_t h = 2166136261u;
        for (auto b : schedule)
        {
            h ^= b;
            h *= 16777619u;
        }

        auto next = [&h]
        {
            h ^= h << 13;
            h ^= h >> 17;
            h ^= h << 5;
            return h;
        };

        for (int i = 0; i < kBlockSamples; ++i)
        {
            p.slotOrder[(size_t) i] = (uint8_t) i;
            p.slotPick [(size_t) i] = (uint8_t) i;
        }

        for (int i = kBlockSamples - 1; i > 0; --i)
        {
            std::swap (p.slotOrder[(size_t) i], p.slotOrder[next() % (uint32_t) (i + 1)]);
            std::swap (p.slotPick [(size_t) i], p.slotPick [next() % (uint32_t) (i + 1)]);
        }

        switch (next() % 4u)
        {
            case 2:  p.repeats = 2; break;
            case 3:  p.repeats = 4; break;
            default: p.repeats = 1; break;
        }

        p.tone     = (int8_t) ((int) (next() % 5u) - 2);
        p.gaps     = (uint8_t) (next() % 6u);
        p.gapStart = (uint8_t) (next() % (uint32_t) kBlockSamples);

        for (int i = 0; i < kBlockBytes; ++i)
            p.whitening[(size_t) i] = (uint8_t) (next() & 0xffu);
    }

    juce::SpinLock lock;
    KeySetup pendingEnc, activeEnc;
    KeySetup pendingDec, activeDec;
    std::atomic<bool> dirty { true };

    AES_ctx ctx {};
    KeyProfile blockProfile;
    bool decrypting = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CipherEngine)
};
