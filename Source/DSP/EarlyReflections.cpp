#include "EarlyReflections.h"

namespace FDNReverb {

    void EarlyReflections::prepare(const juce::dsp::ProcessSpec& spec) {
        int maxSamples = (int)(0.7 * spec.sampleRate) + 8;
        juce::dsp::ProcessSpec mono = spec;
        mono.numChannels = 1;
        buf.prepare(mono);
        buf.setMaximumDelayInSamples(maxSamples);

        erHPCoeffs = FilterDesign::highPass1st(80.f, spec.sampleRate);
        float K = std::tan(juce::MathConstants<float>::pi * 6000.f / (float)spec.sampleRate);
        erLPCoeffs.b0 = K / (1.f + K);
        erLPCoeffs.b1 = erLPCoeffs.b0;
        erLPCoeffs.b2 = 0.f;
        erLPCoeffs.a1 = (K - 1.f) / (K + 1.f);
        erLPCoeffs.a2 = 0.f;
    }

    void EarlyReflections::buildTaps(const AlgorithmPreset& preset, float roomSizeScale, double sampleRate) {
        float erEnergy50 = preset.acoustics.d50[4];
        float V = preset.volumeM3 > 0.f ? preset.volumeM3 : 10.f;
        float mixTimeMs = std::min(0.0117f * V + 50.1f, 150.f);

        float span = mixTimeMs * roomSizeScale;
        for (int i = 0; i < ER_TAPS; ++i) {
            float t01 = static_cast<float>(i + 1) / static_cast<float>(ER_TAPS);
            float delMs = span * std::pow(t01, 1.5f);
            taps[i].delaySamples = delMs * 0.001f * (float)sampleRate;

            float rt60m = preset.acoustics.rt60[4];
            float amp = std::exp(-6.9f * delMs * 0.001f / rt60m);
            float factor = (i < ER_TAPS / 2) ? std::sqrt(erEnergy50) : std::sqrt(1.f - erEnergy50);
            amp *= factor * std::sqrt(2.f / ER_TAPS);

            float pan = (i % 3 == 0) ? -0.707f : ((i % 3 == 1) ? 0.707f : 0.0f);
            taps[i].gainL = amp * std::sqrt(0.5f - 0.5f * pan);
            taps[i].gainR = amp * std::sqrt(0.5f + 0.5f * pan);
        }
    }

    void EarlyReflections::setPreDelay(float ms, double sampleRate) noexcept {
        preDelaySamples = juce::roundToInt(ms * 0.001 * sampleRate);
    }

    std::pair<float, float> EarlyReflections::tick(float mono) noexcept {
        buf.pushSample(0, mono);
        float L = 0.f, R = 0.f;
        for (const auto& t : taps) {
            float d = buf.popSample(0, t.delaySamples + preDelaySamples, false);
            L += t.gainL * d;
            R += t.gainR * d;
        }
        L = erHPL.tick(L, erHPCoeffs);
        R = erHPR.tick(R, erHPCoeffs);
        return { L, R };
    }

    void EarlyReflections::reset() noexcept {
        buf.reset();
        erHPL.reset(); erHPR.reset();
        erLPL.reset(); erLPR.reset();
    }

} // namespace FDNReverb