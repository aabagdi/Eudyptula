/*
 ==============================================================================

 KWeightingFilter - ITU-R BS.1770 / EBU R128 K-weighting

 -----------------------------------------------------------------------------

 The sample-rate adaptation below is derived from SecondOrderIIRFilter by
 Samuel Gaehwiler (Klangfreund), used in the klangfreund.com/lufsmeter/.
 BS.1770 only publishes coefficients at 48 kHz; this back-solves the filter's
 Q / VH / VB / VL from those and re-derives correct coefficients at any rate.

 The MIT License (MIT)

 Copyright (c) 2018 Klangfreund, Samuel Gaehwiler

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.

 ==============================================================================
 */

#pragma once

#include <cmath>

//==============================================================================
/** One biquad's coefficients, in the BS.1770 Figure 3 structure. */
struct BiquadCoeffs
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    /** Re-derives these coefficients for 'sampleRate' from a set given at
        48 kHz. Passing 48000 reproduces the input exactly. */
    void setFrom48k (double b0_48, double b1_48, double b2_48,
                     double a1_48, double a2_48,
                     double sampleRate)
    {
        constexpr double kRef = 48000.0;

        if (std::abs (sampleRate - kRef) < 1.0e-9)
        {
            b0 = b0_48; b1 = b1_48; b2 = b2_48; a1 = a1_48; a2 = a2_48;
            return;
        }

        const double denomH  = a2_48 - a1_48 + 1.0;
        const double denomL  = a1_48 + a2_48 + 1.0;
        const double KoverQ  = (2.0 - 2.0 * a2_48) / denomH;
        const double K48     = std::sqrt (denomL / denomH);
        const double Q       = K48 / KoverQ;
        const double arctanK = std::atan (K48);

        const double VB = (b0_48 - b2_48) / (1.0 - a2_48);
        const double VH = (b0_48 - b1_48 + b2_48) / denomH;
        const double VL = (b0_48 + b1_48 + b2_48) / denomL;

        const double K  = std::tan (arctanK * kRef / sampleRate);
        const double cf = 1.0 / (1.0 + K / Q + K * K);

        b0 = (VH + VB * K / Q + VL * K * K) * cf;
        b1 = 2.0 * (VL * K * K - VH) * cf;
        b2 = (VH - VB * K / Q + VL * K * K) * cf;
        a1 = 2.0 * (K * K - 1.0) * cf;
        a2 = (1.0 - K / Q + K * K) * cf;
    }
};

//==============================================================================
/** The two K-weighting stages' coefficients: a +4 dB high shelf followed by
    the RLB high-pass.
*/
struct KWeightingCoeffs
{
    BiquadCoeffs shelf;
    BiquadCoeffs highpass;

    /** Derives both stages for 'sampleRate' from the 48 kHz coefficients in
        BS.1770-4 Tables 1 and 2. */
    void prepare (double sampleRate)
    {
        shelf.setFrom48k (1.53512485958697,
                         -2.69169618940638,
                          1.19839281085285,
                         -1.69065929318241,
                          0.73248077421585,
                          sampleRate);

        highpass.setFrom48k (1.0,
                            -2.0,
                             1.0,
                            -1.99004745483398,
                             0.99007225036621,
                             sampleRate);
    }
};

//==============================================================================
/** Per-signal, per-channel filter state for one K-weighting chain, the only
    part of the chain that is not shared. Keep one for every signal measured on
    every channel.
*/
struct KWeightingState
{
    void reset()
    {
        shelfZ1 = shelfZ2 = hpZ1 = hpZ2 = 0.0;
    }

    /** Filters one sample through both stages, in double precision because the
        RLB stage's poles sit very close to the unit circle. */
    float process (const KWeightingCoeffs& c, float in)
    {
        const double s = biquad (c.shelf, (double) in, shelfZ1, shelfZ2);
        return (float) biquad (c.highpass, s, hpZ1, hpZ2);
    }

private:
    /** One biquad in the BS.1770 Figure 3 structure: f = in - a1*z1 - a2*z2,
        out = b0*f + b1*z1 + b2*z2, then z2 = z1 and z1 = f. */
    static double biquad (const BiquadCoeffs& c, double in, double& z1, double& z2)
    {
        const double f   = in - c.a1 * z1 - c.a2 * z2;
        const double out = c.b0 * f + c.b1 * z1 + c.b2 * z2;

        z2 = z1;
        z1 = f;

        return out;
    }

    double shelfZ1 = 0.0, shelfZ2 = 0.0;
    double hpZ1 = 0.0, hpZ2 = 0.0;
};
