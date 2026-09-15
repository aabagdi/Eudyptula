/*
 ==============================================================================

 PitchTracker - monophonic YIN-style pitch estimation for the cipher carrier

 ==============================================================================
 */

#pragma once

#include <array>
#include <cmath>
#include <cstddef>

//==============================================================================
/** Estimates the pitch of a mono signal fed one sample at a time, covering
    roughly 43 Hz to 1.4 kHz. 
*/
struct PitchTracker
{
    static constexpr int kDecim  = 8;
    static constexpr int kWindow = 256;
    static constexpr int kMaxLag = 128;
    static constexpr int kMinLag = 4;
    static constexpr int kHop    = 64;

    float freqHz = 0.0f;

    void reset()
    {
        lpState = 0.0f; decimCount = 0;
        buf.fill (0.0f); writePos = 0; sinceAnalysis = 0;
        lin.fill (0.0f); diff.fill (0.0f); cmnd.fill (0.0f);
        freqHz = 0.0f;
    }

    /** Feeds in one host-rate sample, low-passed and decimated, running an
        analysis every kHop decimated samples. */
    void push (float x, double hostRate)
    {
        lpState += (x - lpState) * 0.25f;

        if (++decimCount < kDecim)
            return;
        decimCount = 0;

        buf[(size_t) writePos] = lpState;
        writePos = (writePos + 1) % kWindow;

        if (++sinceAnalysis >= kHop)
        {
            sinceAnalysis = 0;
            analyse (hostRate);
        }
    }

    /** Runs one YIN pass over the buffered window and updates 'freqHz', with
        parabolic interpolation for sub-sample accuracy. */
    void analyse (double hostRate)
    {
        for (int i = 0; i < kWindow; ++i)
            lin[(size_t) i] = buf[(size_t) ((writePos + i) % kWindow)];

        const int n = kWindow - kMaxLag;

        diff[0] = 0.0f;
        for (int tau = 1; tau <= kMaxLag; ++tau)
        {
            float sum = 0.0f;
            for (int j = 0; j < n; ++j)
            {
                const float d = lin[(size_t) j] - lin[(size_t) (j + tau)];
                sum += d * d;
            }
            diff[(size_t) tau] = sum;
        }

        cmnd[0] = 1.0f;
        float running = 0.0f;
        for (int tau = 1; tau <= kMaxLag; ++tau)
        {
            running += diff[(size_t) tau];
            cmnd[(size_t) tau] = running > 0.0f
                               ? diff[(size_t) tau] * (float) tau / running
                               : 1.0f;
        }

        constexpr float kThreshold = 0.15f;
        int best = -1;
        for (int tau = kMinLag; tau <= kMaxLag; ++tau)
        {
            if (cmnd[(size_t) tau] < kThreshold)
            {
                while (tau + 1 <= kMaxLag && cmnd[(size_t) (tau + 1)] < cmnd[(size_t) tau])
                    ++tau;
                best = tau;
                break;
            }
        }

        if (best < 0)
        {
            int minTau = kMinLag;
            for (int tau = kMinLag; tau <= kMaxLag; ++tau)
                if (cmnd[(size_t) tau] < cmnd[(size_t) minTau])
                    minTau = tau;

            if (cmnd[(size_t) minTau] > 0.35f)
            {
                freqHz = 0.0f;
                return;
            }
            best = minTau;
        }

        float tauEst = (float) best;
        if (best > kMinLag && best < kMaxLag)
        {
            const float a = cmnd[(size_t) (best - 1)];
            const float b = cmnd[(size_t) best];
            const float c = cmnd[(size_t) (best + 1)];
            const float denom = 2.0f * (2.0f * b - a - c);
            if (std::abs (denom) > 1.0e-9f)
                tauEst += (c - a) / denom;
        }

        const double analysisRate = hostRate / (double) kDecim;
        const float f = tauEst > 0.0f ? (float) (analysisRate / (double) tauEst) : 0.0f;

        freqHz = (f >= 40.0f && f <= 2000.0f) ? f : 0.0f;
    }

private:
    float lpState = 0.0f;
    int decimCount = 0;
    std::array<float, (size_t) kWindow> buf {};
    int writePos = 0;
    int sinceAnalysis = 0;

    std::array<float, (size_t) kWindow> lin {};
    std::array<float, (size_t) kMaxLag + 1> diff {};
    std::array<float, (size_t) kMaxLag + 1> cmnd {};
};
