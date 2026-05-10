#pragma once
#include "DSPConstants.h"
#include "DelayUnit.h"
#include "EarlyReflections.h"
#include "../PluginParameters.h"
#include <atomic>

namespace FDNReverb {

    class FDNReverbEngine {
    public:
        FDNReverbEngine();

        void prepare(const juce::dsp::ProcessSpec& spec);
        void reset();
        void setParams(const DSPParams& p);

        void processBlock(const float* inL, const float* inR, float* outL, float* outR, int numSamples) noexcept;

        std::array<float, NUM_BANDS> getEffectiveRT60() const noexcept { return effectiveRT60; }

    private:
        void rebuildDelayLengths();
        void rebuildAbsorptionFilters();
        void rebuildERTaps();

        inline void householderMix(std::array<float, FDN_N>& v) noexcept {
            float s = 0.f;
            for (auto x : v) s += x;
            s *= (2.0f / static_cast<float>(FDN_N));
            for (auto& x : v) x -= s;
        }

        inline void applySignFlip(std::array<float, FDN_N>& v) noexcept {
            static constexpr std::array<float, FDN_N> flip = { 1.f, -1.f, 1.f, -1.f, -1.f, 1.f, -1.f, 1.f };
            for (int i = 0; i < FDN_N; ++i) v[i] *= flip[i];
        }

        inline float softClip(float x, float amount) noexcept {
            if (amount < 0.001f) return x;
            float drive = 1.f + amount * 9.f;
            float y = x * drive;
            float y2 = y * y;
            return y * (27.f + y2) / (27.f + 9.f * y2) / drive;
        }

        double sampleRate{ 48000.0 };
        juce::dsp::ProcessSpec spec;

        std::array<DelayUnit, FDN_N> delayUnits;
        EarlyReflections er;
        std::array<float, FDN_N> bIn, cOut;
        juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> preDelay;

        std::array<BiquadCoeffs, FDN_N> dcCoeffs;
        std::array<BiquadState, FDN_N> dcState;

        float duckEnv{ 0.f }, duckAtt{ 0.f }, duckRel{ 0.f };
        std::array<float, FDN_N> fbVec;

        DSPParams activeParams;
        std::array<int, FDN_N> delaySamples;
        mutable std::array<float, NUM_BANDS> effectiveRT60;

        std::atomic<bool> paramsNeedUpdate{ false };
        DSPParams pendingParams;
        juce::SpinLock paramLock;
    };

} // namespace FDNReverb