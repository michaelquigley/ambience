#include "DelayUnit.h"

namespace FDNReverb {

    void DelayUnit::prepare(const juce::dsp::ProcessSpec& spec,
        int targetDelaySamples, int lfoPhaseOffsetDeg,
        const std::array<int, SAPF_STAGES>& sapfDelays)
    {
        fs = spec.sampleRate;
        nominalDelaySamples = targetDelaySamples;

        delay.prepare(spec);
        delay.setMaximumDelayInSamples(targetDelaySamples + 512 + 4);
        delay.setDelay(static_cast<float>(targetDelaySamples));

        lfoPhase = lfoPhaseOffsetDeg * juce::MathConstants<float>::pi / 180.f;

        juce::dsp::ProcessSpec monoSpec = spec;
        monoSpec.numChannels = 1;
        for (int i = 0; i < SAPF_STAGES; ++i)
            sapf[i].prepare(monoSpec, sapfDelays[i]);

        for (auto& c : absorptionCoeffs) c = BiquadCoeffs{};
        for (auto& s : absorptionState)  s.reset();
    }

    void DelayUnit::setAbsorption(const std::array<BiquadCoeffs, ABSO_STAGES>& c) noexcept {
        absorptionCoeffs = c;
    }

    void DelayUnit::setLFO(float rateHz, float depthSamples, double sampleRate) noexcept {
        lfoIncrement = 2.f * juce::MathConstants<float>::pi * rateHz / (float)sampleRate;
        lfoDepth = depthSamples;
    }

    void DelayUnit::setDiffusion(float d) noexcept {
        float g = 0.4f + d * 0.32f;
        for (auto& ap : sapf) ap.setGain(g);
    }

    float DelayUnit::tick(float input) noexcept {
        lfoPhase += lfoIncrement;
        if (lfoPhase > juce::MathConstants<float>::twoPi)
            lfoPhase -= juce::MathConstants<float>::twoPi;

        float modDelay = static_cast<float>(nominalDelaySamples) + lfoDepth * std::sin(lfoPhase);
        delay.setDelay(std::max(1.f, modDelay));

        float s = delay.popSample(0);

        for (int i = 0; i < ABSO_STAGES; ++i)
            s = absorptionState[i].tick(s, absorptionCoeffs[i]);

        for (auto& ap : sapf)
            s = ap.tick(s);

        delay.pushSample(0, input);
        return s;
    }

    void DelayUnit::reset() noexcept {
        delay.reset();
        for (auto& s : absorptionState) s.reset();
        for (auto& ap : sapf) ap.reset();
    }

} // namespace FDNReverb