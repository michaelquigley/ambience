#pragma once
// ============================================================
//  FDNCore.h  ―  8-channel Feedback Delay Network Engine
//
//  Architecture (per channel):
//    Input → [pre-delay] → [ER FIR] ──┐
//                                     ↓
//    8× DelayLine ← Householder Mix ← [delayLine output]
//    Each delay:  delay → AbsorptionBiquads → SAPF stages
//
//  References:
//    Jot & Chaigne 1991, Schlecht 2017/2020, Välimäki 2024,
//    Prawda/Schlecht/Välimäki 2019-20, signalsmith-audio.co.uk
// ============================================================
#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include "AlgorithmPresets.h"

namespace FDNReverb {

    // ── Compile-time constants ────────────────────────────────────────────────────
    static constexpr int FDN_N = 8;    // FDN order
    static constexpr int SAPF_STAGES = 3;    // allpass stages per delay line
    static constexpr int ABSO_STAGES = 3;    // biquad absorption stages per line
    static constexpr int ER_TAPS = 16;   // early-reflection FIR taps

    // Mutually-prime base delays (samples @ 48 kHz), log-distributed 30–130 ms
    // Greedy-coprime result; scaled for other sample rates in prepare()
    static constexpr std::array<int, FDN_N> BASE_PRIMES_48K = {
        1451, 1693, 1979, 2311, 2683, 3067, 3491, 3923
    };

    // ─────────────────────────────────────────────────────────────────────────────
    //  Biquad helpers (Direct Form II Transposed — most robust)
    // ─────────────────────────────────────────────────────────────────────────────
    struct BiquadCoeffs {
        float b0{ 1.f }, b1{ 0.f }, b2{ 0.f };
        float       a1{ 0.f }, a2{ 0.f };
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
    //  Schroeder 2-multiplier SAPF  (absorbent, g ≈ 0.618)
    //  y[n] = -g·x[n] + x[n-M] + g·y[n-M]
    // ─────────────────────────────────────────────────────────────────────────────
    class SAPFStage {
    public:
        void prepare(const juce::dsp::ProcessSpec& spec, int delayTargetSamples);
        void setGain(float g) noexcept { gain = juce::jlimit(0.3f, 0.72f, g); }
        float tick(float x) noexcept;
        void  reset() noexcept;

    private:
        juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Thiran> dl;
        float gain{ 0.618f };
        int   M{ 0 };
    };

    // ─────────────────────────────────────────────────────────────────────────────
    //  Single delay-line unit:  delay → absorption biquads → SAPF cascade
    // ─────────────────────────────────────────────────────────────────────────────
    class DelayUnit {
    public:
        void prepare(const juce::dsp::ProcessSpec& spec,
            int  targetDelaySamples,
            int  lfoPhaseOffsetDeg,   // 0-360
            const std::array<int, SAPF_STAGES>& sapfDelays);

        void setAbsorption(const std::array<BiquadCoeffs, ABSO_STAGES>& c) noexcept;
        void setLFO(float rateHz, float depthSamples, double sampleRate) noexcept;
        void setDiffusion(float d) noexcept;

        float tick(float input) noexcept;
        void  reset() noexcept;

        int nominalDelaySamples{ 0 };

    private:
        juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Thiran> delay;

        std::array<BiquadCoeffs, ABSO_STAGES> absorptionCoeffs;
        std::array<BiquadState, ABSO_STAGES> absorptionState;

        std::array<SAPFStage, SAPF_STAGES> sapf;

        // LFO
        float lfoPhase{ 0.f };   // radians
        float lfoIncrement{ 0.f };   // radians per sample
        float lfoDepth{ 0.f };   // samples

        double fs{ 48000.0 };
    };

    // ─────────────────────────────────────────────────────────────────────────────
    //  Early-Reflection FIR tap set
    // ─────────────────────────────────────────────────────────────────────────────
    struct ERTap {
        float delaySamples{ 0.f };
        float gainL{ 0.f };
        float gainR{ 0.f };
    };

    class EarlyReflections {
    public:
        void prepare(const juce::dsp::ProcessSpec& spec);
        void buildTaps(const AlgorithmPreset& preset, float roomSizeScale, double sampleRate);
        void setPreDelay(float ms, double sampleRate) noexcept;

        // Returns {L, R}
        std::pair<float, float> tick(float mono) noexcept;
        void reset() noexcept;

    private:
        juce::dsp::DelayLine<float,
            juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> buf;

        std::array<ERTap, ER_TAPS> taps;
        int preDelaySamples{ 0 };

        // Tone shaping for ER
        BiquadCoeffs erHPCoeffs, erLPCoeffs;
        BiquadState  erHPL, erHPR, erLPL, erLPR;
    };

    // ─────────────────────────────────────────────────────────────────────────────
    //  Filter design utilities (static helpers)
    // ─────────────────────────────────────────────────────────────────────────────
    namespace FilterDesign {
        BiquadCoeffs lowShelf(float fcHz, float gainDB, double sampleRate);
        BiquadCoeffs highShelf(float fcHz, float gainDB, double sampleRate);
        BiquadCoeffs peak(float fcHz, float gainDB, float Q, double sampleRate);
        BiquadCoeffs highPass1st(float fcHz, double sampleRate);
        BiquadCoeffs allpass1st(float fcHz, double sampleRate);
        // Design absorption filter for delay line i
        std::array<BiquadCoeffs, ABSO_STAGES>
            designAbsorption(int delaySamples, double sampleRate,
                const std::array<float, NUM_BANDS>& rt60,
                float hfDamping, float lfAbsorption);
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  Parameter snapshot (lock-free swap via atomic pointer)
    // ─────────────────────────────────────────────────────────────────────────────
    struct DSPParams {
        int   algorithmIndex{ 0 };
        float decayScale{ 1.0f };   // multiplier on preset RT60
        float roomSizeScale{ 1.0f };   // delay length multiplier 0.5–2.0
        float hfDamping{ 0.5f };   // 0-1
        float lfAbsorption{ 0.5f };   // 0-1
        float diffusion{ 0.70f };  // SAPF gain modifier
        float preDelayMs{ 10.f };
        float modAmount{ 0.25f };  // 0-1 → LFO depth
        float modRate{ 0.5f };   // Hz
        float stereoWidth{ 0.80f };  // 0-1
        float crossFeed{ 0.15f };  // 0-1
        float erLevel{ 0.6f };   // 0-1
        float lateLevel{ 1.0f };   // 0-1
        float wetDB{ -6.f };
        float dryDB{ 0.f };
        float saturation{ 0.0f };
        float duckingAmount{ 0.0f };   // dB
        float duckingAttackMs{ 10.f };
        float duckingRelMs{ 200.f };
        float duckingThreshDB{ -20.f };
    };

    // ─────────────────────────────────────────────────────────────────────────────
    //  Main FDN Reverb Engine
    // ─────────────────────────────────────────────────────────────────────────────
    class FDNReverbEngine {
    public:
        FDNReverbEngine();
        ~FDNReverbEngine() = default;

        void prepare(const juce::dsp::ProcessSpec& spec);
        void reset();

        // Call from message thread; engine picks up on next process() call
        void setParams(const DSPParams& p);

        // Process interleaved stereo block (in-place for wet signal)
        // Returns wet L/R into outL / outR; caller does dry/wet mix
        void processBlock(const float* inL, const float* inR,
            float* outL, float* outR,
            int numSamples) noexcept;

        // Read-only RT60 snapshot for GUI
        std::array<float, NUM_BANDS> getEffectiveRT60() const noexcept;

    private:
        // ── rebuild when algorithm or room-size changes ───────────────────────
        void rebuildDelayLengths();
        void rebuildAbsorptionFilters();
        void rebuildERTaps();

        // ── Householder mix (8-ch, O(N) additions) ───────────────────────────
        inline void householderMix(std::array<float, FDN_N>& v) noexcept {
            float s = 0.f;
            for (auto x : v) s += x;
            s *= (2.0f / static_cast<float>(FDN_N));
            for (auto& x : v) x -= s;
        }

        // Sign-flip pattern to break symmetry & suppress DC bias (doc §4.1)
        inline void applySignFlip(std::array<float, FDN_N>& v) noexcept {
            // Signs chosen so each neighbouring pair is opposite
            static constexpr std::array<float, FDN_N> flip = {
                 1.f,-1.f, 1.f,-1.f,-1.f, 1.f,-1.f, 1.f };
            for (int i = 0; i < FDN_N; ++i) v[i] *= flip[i];
        }

        // Soft-clip saturation (tanh approximation for CPU efficiency)
        inline float softClip(float x, float amount) noexcept {
            if (amount < 0.001f) return x;
            float drive = 1.f + amount * 9.f;   // 1–10×
            float y = x * drive;
            // Padé approx of tanh: good up to |x|≈3
            float y2 = y * y;
            return y * (27.f + y2) / (27.f + 9.f * y2) / drive;
        }

        // ── State ─────────────────────────────────────────────────────────────
        double              sampleRate{ 48000.0 };
        juce::dsp::ProcessSpec spec;
        bool                prepared{ false };

        std::array<DelayUnit, FDN_N> delayUnits;
        EarlyReflections             er;

        // Input/output gain vectors (populated from D50/C80 energy budget)
        std::array<float, FDN_N> bIn, cOut;

        // Pre-delay before FDN input
        juce::dsp::DelayLine<float,
            juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> preDelay;

        // Output tone correction (Jot E(z))
        BiquadCoeffs toneCorrCoeffsL, toneCorrCoeffsR;
        BiquadState  toneCorrStateL, toneCorrStateR;

        // DC blocker on each FDN output
        std::array<BiquadCoeffs, FDN_N> dcCoeffs;
        std::array<BiquadState, FDN_N> dcState;

        // Ducking compressor (simple level follower)
        float duckEnv{ 0.f };
        float duckAtt{ 0.f };   // per-sample coefficient
        float duckRel{ 0.f };

        // Working buffer for Householder (current feedback vector)
        std::array<float, FDN_N> fbVec;

        // Current live params (updated atomically from setParams)
        DSPParams activeParams;

        // Rebuilt when params change
        std::array<int, FDN_N> delaySamples;  // current delay lengths

        // Effective RT60 per band (for visualizer)
        mutable std::array<float, NUM_BANDS> effectiveRT60;

        // Pending param update flag
        std::atomic<bool> paramsNeedUpdate{ false };
        DSPParams         pendingParams;
        juce::SpinLock    paramLock;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FDNReverbEngine)
    };

} // namespace FDNReverb