#pragma once
#include "DSPConstants.h"
#include "../AlgorithmPresets.h"
#include <array>

namespace FDNReverb {

    // ─────────────────────────────────────────────────────────────────────────────
    //  Biquad helpers (Direct Form II Transposed — most robust)
    // ─────────────────────────────────────────────────────────────────────────────
    struct BiquadCoeffs {
        float b0{ 1.f }, b1{ 0.f }, b2{ 0.f };
        float            a1{ 0.f }, a2{ 0.f };
    };

    struct BiquadState {
        float s1{ 0.f }, s2{ 0.f };

        inline float tick(float x, const BiquadCoeffs& c) noexcept {
            float y = c.b0 * x + s1;
            s1 = c.b1 * x - c.a1 * y + s2;
            s2 = c.b2 * x - c.a2 * y;
            return y;
        }
        void reset() noexcept { s1 = s2 = 0.f; }
    };

    // ─────────────────────────────────────────────────────────────────────────────
    //  Filter design utilities
    // ─────────────────────────────────────────────────────────────────────────────
    namespace FilterDesign {
        BiquadCoeffs lowShelf(float fcHz, float gainDB, double sampleRate);
        BiquadCoeffs highShelf(float fcHz, float gainDB, double sampleRate);
        BiquadCoeffs peak(float fcHz, float gainDB, float Q, double sampleRate);
        BiquadCoeffs highPass1st(float fcHz, double sampleRate);
        BiquadCoeffs allpass1st(float fcHz, double sampleRate);

        // Design absorption filter cascade for delay lines
        std::array<BiquadCoeffs, ABSO_STAGES> designAbsorption(
            int delaySamples, double sampleRate,
            const std::array<float, NUM_BANDS>& rt60,
            float hfDamping, float lfAbsorption);
    }

} // namespace FDNReverb