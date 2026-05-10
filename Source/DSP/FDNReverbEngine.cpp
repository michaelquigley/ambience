#include "FDNReverbEngine.h"
#include <numeric>

namespace FDNReverb {

    FDNReverbEngine::FDNReverbEngine() {
        bIn.fill(1.f / std::sqrt((float)FDN_N));
        cOut.fill(1.f / std::sqrt((float)FDN_N));
        fbVec.fill(0.f);
    }

    void FDNReverbEngine::prepare(const juce::dsp::ProcessSpec& sp) {
        spec = sp;
        sampleRate = sp.sampleRate;

        juce::dsp::ProcessSpec mono = sp; mono.numChannels = 1;
        preDelay.prepare(mono);
        preDelay.setMaximumDelayInSamples((int)(0.505 * sampleRate));

        er.prepare(sp);
        rebuildDelayLengths();
        rebuildERTaps();
        rebuildAbsorptionFilters();

        for (int i = 0; i < FDN_N; ++i)
            dcCoeffs[i] = FilterDesign::highPass1st(10.f, sampleRate);

        reset();
    }

    void FDNReverbEngine::reset() {
        for (auto& u : delayUnits) u.reset();
        er.reset();
        preDelay.reset();
        fbVec.fill(0.f);
        for (auto& s : dcState) s.reset();
        duckEnv = 0.f;
    }

    void FDNReverbEngine::setParams(const DSPParams& p) {
        juce::SpinLock::ScopedLockType lock(paramLock);
        pendingParams = p;
        paramsNeedUpdate.store(true, std::memory_order_release);
    }

    void FDNReverbEngine::rebuildDelayLengths() {
        float scale = 0.5f + activeParams.roomSizeScale;
        for (int i = 0; i < FDN_N; ++i) {
            float scaled = BASE_PRIMES_48K[i] * scale * (float)(sampleRate / 48000.0);
            delaySamples[i] = std::max(64, (int)std::round(scaled));
        }

        static constexpr std::array<std::array<int, SAPF_STAGES>, FDN_N> SAPF_DELAYS = { {
            {{25,41,63}}, {{29,47,71}}, {{31,53,79}}, {{37,59,83}},
            {{43,61,89}}, {{47,67,97}}, {{53,73,101}},{{59,79,107}}
        } };

        juce::dsp::ProcessSpec mono = spec; mono.numChannels = 1;
        for (int i = 0; i < FDN_N; ++i) {
            std::array<int, SAPF_STAGES> sapfSamples;
            for (int s = 0; s < SAPF_STAGES; ++s)
                sapfSamples[s] = juce::roundToInt(SAPF_DELAYS[i][s] * sampleRate / 10000.0);
            delayUnits[i].prepare(mono, delaySamples[i], i * 360 / FDN_N, sapfSamples);
        }
    }

    void FDNReverbEngine::rebuildAbsorptionFilters() {
        auto& preset = *ALL_PRESETS[activeParams.algorithmIndex];
        std::array<float, NUM_BANDS> scaledRT60 = preset.acoustics.rt60;
        for (auto& v : scaledRT60) v *= activeParams.decayScale;
        effectiveRT60 = scaledRT60;

        for (int i = 0; i < FDN_N; ++i) {
            auto coeffs = FilterDesign::designAbsorption(delaySamples[i], sampleRate, scaledRT60, activeParams.hfDamping, activeParams.lfAbsorption);
            delayUnits[i].setAbsorption(coeffs);
        }

        float depthSmp = activeParams.modAmount * (0.184f / std::max(0.01f, activeParams.modRate)) * 0.001f * (float)sampleRate;
        for (auto& u : delayUnits) {
            u.setLFO(activeParams.modRate, depthSmp, sampleRate);
            u.setDiffusion(activeParams.diffusion);
        }

        float e_er80 = std::pow(10.f, preset.acoustics.c80[4] / 10.f) / (1.f + std::pow(10.f, preset.acoustics.c80[4] / 10.f));
        float lateAmp = std::sqrt(1.f - e_er80) * activeParams.lateLevel;
        bIn.fill(lateAmp / std::sqrt((float)FDN_N));
        cOut.fill(lateAmp / std::sqrt((float)FDN_N));

        duckAtt = std::exp(-1.f / (activeParams.duckingAttackMs * 0.001f * (float)sampleRate));
        duckRel = std::exp(-1.f / (activeParams.duckingRelMs * 0.001f * (float)sampleRate));
    }

    void FDNReverbEngine::rebuildERTaps() {
        auto& preset = *ALL_PRESETS[activeParams.algorithmIndex];
        er.buildTaps(preset, activeParams.roomSizeScale, sampleRate);
        er.setPreDelay(activeParams.preDelayMs, sampleRate);
    }

    void FDNReverbEngine::processBlock(const float* inL, const float* inR, float* outL, float* outR, int numSamples) noexcept {
        if (paramsNeedUpdate.load(std::memory_order_acquire)) {
            {
                juce::SpinLock::ScopedTryLockType tryLock(paramLock);
                if (tryLock.isLocked()) {
                    activeParams = pendingParams;
                    paramsNeedUpdate.store(false, std::memory_order_release);
                }
            }
            rebuildAbsorptionFilters();
            rebuildERTaps();
        }

        float wetGain = juce::Decibels::decibelsToGain(activeParams.wetDB);
        float duckThreshLin = juce::Decibels::decibelsToGain(activeParams.duckingThreshDB);
        float duckAmount = juce::Decibels::decibelsToGain(-std::abs(activeParams.duckingAmount));

        for (int n = 0; n < numSamples; ++n) {
            float mono = (inL[n] + inR[n]) * 0.5f;
            auto [erL, erR] = er.tick(mono);

            preDelay.pushSample(0, mono);
            float xFDN = preDelay.popSample(0, static_cast<float>(activeParams.preDelayMs * 0.001 * sampleRate));
            householderMix(fbVec);
            applySignFlip(fbVec);

            float fdnOutL = 0.f, fdnOutR = 0.f;
            std::array<float, FDN_N> nextFb;
            for (int i = 0; i < FDN_N; ++i) {
                float out = delayUnits[i].tick(fbVec[i] + xFDN * bIn[i]);
                out = dcState[i].tick(out, dcCoeffs[i]);
                out = softClip(out, activeParams.saturation);
                nextFb[i] = out;

                float width = activeParams.stereoWidth;
                if (i % 2 == 0) { fdnOutL += cOut[i] * out; fdnOutR += cOut[i] * out * (1.f - width); }
                else { fdnOutR += cOut[i] * out; fdnOutL += cOut[i] * out * (1.f - width); }
            }
            fbVec = nextFb;

            float inLevel = std::abs(mono);
            duckEnv = (inLevel > duckThreshLin) ? (duckAtt * duckEnv + (1.f - duckAtt) * inLevel) : (duckRel * duckEnv);
            float duckGain = (activeParams.duckingAmount > 0.1f && duckEnv > duckThreshLin * 0.5f)
                ? juce::jmap(duckEnv, duckThreshLin * 0.5f, duckThreshLin * 2.f, 1.f, duckAmount) : 1.f;

            outL[n] = (fdnOutL + erL * activeParams.erLevel) * duckGain * wetGain;
            outR[n] = (fdnOutR + erR * activeParams.erLevel) * duckGain * wetGain;
        }
    }

} // namespace FDNReverb