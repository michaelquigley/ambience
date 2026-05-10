#include "BiquadFilters.h"
#include <JuceHeader.h>
#include <cmath>
#include <algorithm>

namespace FDNReverb {
    namespace FilterDesign {

        static float tanPi(float f, double fs) noexcept {
            return std::tan(juce::MathConstants<float>::pi * (float)(f / fs));
        }

        BiquadCoeffs lowShelf(float fcHz, float gainDB, double sampleRate) {
            float A = std::pow(10.f, gainDB / 40.f);
            float K = tanPi(fcHz, sampleRate);
            BiquadCoeffs c;
            if (gainDB >= 0.f) {
                float norm = 1.f / (1.f + K);
                c.b0 = (1.f + A * K) * norm;
                c.b1 = (A * K - 1.f) * norm;
                c.b2 = 0.f;
                c.a1 = (K - 1.f) * norm;
                c.a2 = 0.f;
            }
            else {
                c.b0 = (1.f + K / A) / (1.f + K);
                c.b1 = (K / A - 1.f) / (1.f + K);
                c.b2 = 0.f;
                c.a1 = (K - 1.f) / (1.f + K);
                c.a2 = 0.f;
            }
            return c;
        }

        BiquadCoeffs highShelf(float fcHz, float gainDB, double sampleRate) {
            float A = std::pow(10.f, gainDB / 40.f);
            float K = tanPi(fcHz, sampleRate);
            BiquadCoeffs c;
            if (gainDB >= 0.f) {
                float norm = 1.f / (1.f + K);
                c.b0 = (A + K) * norm;
                c.b1 = (K - A) * norm;
                c.b2 = 0.f;
                c.a1 = (K - 1.f) * norm;
                c.a2 = 0.f;
            }
            else {
                float norm = 1.f / (1.f + K);
                c.b0 = (1.f + A * K) * norm;
                c.b1 = (A * K - 1.f) * norm;
                c.b2 = 0.f;
                c.a1 = (K - 1.f) * norm;
                c.a2 = 0.f;
            }
            return c;
        }

        BiquadCoeffs peak(float fcHz, float gainDB, float Q, double sampleRate) {
            float A = std::pow(10.f, gainDB / 40.f);
            float w0 = 2.f * juce::MathConstants<float>::pi * fcHz / (float)sampleRate;
            float alpha = std::sin(w0) / (2.f * Q);
            float cos0 = std::cos(w0);
            BiquadCoeffs c;
            c.a1 = 2.f * cos0 / (1.f + alpha / A);
            c.a2 = (1.f - alpha / A) / (1.f + alpha / A);
            c.b0 = (1.f + alpha * A) / (1.f + alpha / A);
            c.b1 = -2.f * cos0 / (1.f + alpha / A);
            c.b2 = (1.f - alpha * A) / (1.f + alpha / A);
            return c;
        }

        BiquadCoeffs highPass1st(float fcHz, double sampleRate) {
            float K = tanPi(fcHz, sampleRate);
            float n = 1.f + K;
            BiquadCoeffs c;
            c.b0 = 1.f / n; c.b1 = -1.f / n; c.b2 = 0.f;
            c.a1 = (K - 1.f) / n; c.a2 = 0.f;
            return c;
        }

        std::array<BiquadCoeffs, ABSO_STAGES> designAbsorption(
            int delaySamples, double sampleRate,
            const std::array<float, NUM_BANDS>& rt60,
            float hfDamping, float lfAbsorption)
        {
            std::array<BiquadCoeffs, ABSO_STAGES> c;
            float rt60_mid = std::max(0.01f, rt60[4]);
            float rt60_lf = std::max(0.01f, rt60[2]);   // 125 Hz
            float rt60_hf = std::max(0.01f, rt60[7]);   // 4 kHz
            float m = static_cast<float>(delaySamples);
            float fs = static_cast<float>(sampleRate);

            float g_mid = std::pow(10.f, -3.f * m / (fs * rt60_mid));
            float g_lf = std::pow(10.f, -3.f * m / (fs * rt60_lf));
            float g_hf = std::pow(10.f, -3.f * m / (fs * rt60_hf));

            c[0].b0 = g_mid; c[0].b1 = 0.f; c[0].b2 = 0.f;
            c[0].a1 = 0.f;   c[0].a2 = 0.f;

            float lfGainRel = 20.f * std::log10(std::max(1e-6f, g_lf / g_mid)) - lfAbsorption * 3.f;
            c[1] = lowShelf(150.f, lfGainRel, sampleRate);

            float hfGainRel = 20.f * std::log10(std::max(1e-6f, g_hf / g_mid)) - hfDamping * 6.f;
            c[2] = highShelf(4000.f, hfGainRel, sampleRate);

            return c;
        }

    } // namespace FilterDesign
} // namespace FDNReverb