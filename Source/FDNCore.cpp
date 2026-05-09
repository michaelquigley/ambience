// ============================================================
//  FDNCore.cpp  ―  FDN Reverb Engine Implementation
//  See FDNCore.h for architecture overview
// ============================================================
#include "FDNCore.h"
#include <cmath>
#include <numeric>
#include <algorithm>

namespace FDNReverb {

    // ─────────────────────────────────────────────────────────────────────────────
    //  Filter Design Utilities
    // ─────────────────────────────────────────────────────────────────────────────
    namespace FilterDesign {

        static float tanPi(float f, double fs) noexcept {
            return std::tan(juce::MathConstants<float>::pi * (float)(f / fs));
        }

        BiquadCoeffs lowShelf(float fcHz, float gainDB, double sampleRate) {
            float A = std::pow(10.f, gainDB / 40.f);
            float t = tanPi(fcHz, sampleRate);
            float t2 = t * t;
            float sq = std::sqrt(A);
            float n = t2 + sq * t * 2.f + A * A;   // Shelving 1st-order: use simpler form
            // 1st-order low shelf (Zölzer)
            float K = tanPi(fcHz, sampleRate);
            BiquadCoeffs c;
            if (gainDB >= 0.f) {
                float Ksq = K * K;
                float norm = 1.f / (1.f + K);
                c.b0 = (1.f + A * K) * norm;
                c.b1 = (A * K - 1.f) * norm;
                c.b2 = 0.f;
                c.a1 = (K - 1.f) * norm;
                c.a2 = 0.f;
            }
            else {
                float norm2 = 1.f / (A + K);
                c.b0 = (A + K) / (A + K);  // = 1 but keep form
                c.b0 = (1.f + K / A) / (1.f + K);
                c.b1 = (K / A - 1.f) / (1.f + K);
                c.b2 = 0.f;
                c.a1 = (K - 1.f) / (1.f + K);
                c.a2 = 0.f;
            }
            return c;
        }

        BiquadCoeffs highShelf(float fcHz, float gainDB, double sampleRate) {
            // Map high-shelf via bilinear substitution s → 1/s (swap LP↔HP)
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
            float norm = 1.f / (1.f + alpha / A);
            c.b0 = (1.f + alpha * A) * norm;
            c.b1 = (-2.f * cos0) * norm;
            c.b2 = (1.f - alpha * A) * norm;
            c.a1 = (-2.f * cos0) * norm;  // note: stored as -a1 for DF2T
            c.a2 = (1.f - alpha / A) * norm;
            // Convert to standard sign convention (y = b0 x + b1 x[-1] ... - a1 y[-1] ...)
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

        // ─────────────────────────────────────────────────────────────────────────────
        //  Design absorption filter for delay line i
        //
        //  Jot 1991 §3 method:
        //    Target per-loop gain at frequency ω:
        //      g(ω) = 10^(-3 m_i / (fs · T60(ω)))
        //    We approximate with:
        //      Stage 0: overall gain from T60 at 1 kHz (reference)
        //      Stage 1: low-shelf  at 125 Hz  (lfAbsorption control)
        //      Stage 2: high-shelf at 4 kHz   (hfDamping control)
        // ─────────────────────────────────────────────────────────────────────────────
        std::array<BiquadCoeffs, ABSO_STAGES>
            designAbsorption(int delaySamples, double sampleRate,
                const std::array<float, NUM_BANDS>& rt60,
                float hfDamping, float lfAbsorption)
        {
            std::array<BiquadCoeffs, ABSO_STAGES> c;

            // Reference RT60 at mid (500 Hz, index 4)
            float rt60_mid = std::max(0.01f, rt60[4]);
            float rt60_lf = std::max(0.01f, rt60[2]);   // 125 Hz band
            float rt60_hf = std::max(0.01f, rt60[7]);   // 4 kHz band

            float m = static_cast<float>(delaySamples);
            float fs = static_cast<float>(sampleRate);

            // Per-loop gain at reference band
            float g_mid = std::pow(10.f, -3.f * m / (fs * rt60_mid));
            float g_lf = std::pow(10.f, -3.f * m / (fs * rt60_lf));
            float g_hf = std::pow(10.f, -3.f * m / (fs * rt60_hf));

            // Stage 0: flat gain at g_mid (implemented as peak at DC = lowShelf @ very low fc)
            float overallGainDB = 20.f * std::log10(std::max(1e-6f, g_mid));
            // Use high-pass (actually allpass gain): just scale b0
            c[0].b0 = g_mid; c[0].b1 = 0.f; c[0].b2 = 0.f;
            c[0].a1 = 0.f;   c[0].a2 = 0.f;

            // Stage 1: low-shelf — relative gain  LF vs mid
            float lfGainRel = 20.f * std::log10(std::max(1e-6f, g_lf / g_mid));
            // Modulate by user lfAbsorption (0 = no extra damping, 1 = extra damping)
            lfGainRel -= lfAbsorption * 3.f;   // up to -3 dB extra in low end
            c[1] = FilterDesign::lowShelf(150.f, lfGainRel, sampleRate);

            // Stage 2: high-shelf — relative gain  HF vs mid
            float hfGainRel = 20.f * std::log10(std::max(1e-6f, g_hf / g_mid));
            hfGainRel -= hfDamping * 6.f;      // user damping: up to -6 dB extra in HF
            c[2] = FilterDesign::highShelf(4000.f, hfGainRel, sampleRate);

            return c;
        }

    } // namespace FilterDesign

    // ─────────────────────────────────────────────────────────────────────────────
    //  SAPFStage
    // ─────────────────────────────────────────────────────────────────────────────
    void SAPFStage::prepare(const juce::dsp::ProcessSpec& spec, int delayTargetSamples) {
        M = delayTargetSamples;
        dl.prepare(spec);
        dl.setMaximumDelayInSamples(M + 4);
        dl.setDelay(static_cast<float>(M));
    }

    float SAPFStage::tick(float x) noexcept {
        float d = dl.popSample(0);
        float w = x + gain * d;
        dl.pushSample(0, w);
        return d - gain * w;
    }

    void SAPFStage::reset() noexcept { dl.reset(); }

    // ─────────────────────────────────────────────────────────────────────────────
    //  DelayUnit
    // ─────────────────────────────────────────────────────────────────────────────
    void DelayUnit::prepare(const juce::dsp::ProcessSpec& spec,
        int  targetDelaySamples,
        int  lfoPhaseOffsetDeg,
        const std::array<int, SAPF_STAGES>& sapfDelays)
    {
        fs = spec.sampleRate;
        nominalDelaySamples = targetDelaySamples;

        delay.prepare(spec);
        delay.setMaximumDelayInSamples(targetDelaySamples + 512 + 4);
        delay.setDelay(static_cast<float>(targetDelaySamples));

        // LFO offset
        lfoPhase = lfoPhaseOffsetDeg * juce::MathConstants<float>::pi / 180.f;

        // SAPF stages — each stage gets its own spec (single channel)
        juce::dsp::ProcessSpec monoSpec = spec;
        monoSpec.numChannels = 1;
        for (int i = 0; i < SAPF_STAGES; ++i)
            sapf[i].prepare(monoSpec, sapfDelays[i]);

        // Reset absorption to identity
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
        float g = 0.4f + d * 0.32f;   // maps 0-1 → 0.40–0.72
        for (auto& ap : sapf) ap.setGain(g);
    }

    float DelayUnit::tick(float input) noexcept {
        // LFO modulation (Doppler-safe: ΔD·f_LFO ≤ 0.184 → ≤ 2 cents)
        lfoPhase += lfoIncrement;
        if (lfoPhase > juce::MathConstants<float>::twoPi)
            lfoPhase -= juce::MathConstants<float>::twoPi;
        float modDelay = static_cast<float>(nominalDelaySamples)
            + lfoDepth * std::sin(lfoPhase);
        delay.setDelay(std::max(1.f, modDelay));

        // Read feedback from delay
        float s = delay.popSample(0);

        // Absorption filter cascade (biquad chain)
        for (int i = 0; i < ABSO_STAGES; ++i)
            s = absorptionState[i].tick(s, absorptionCoeffs[i]);

        // SAPF cascade (diffusion / smearing)
        for (auto& ap : sapf)
            s = ap.tick(s);

        // Write new input into delay
        delay.pushSample(0, input);

        return s;
    }

    void DelayUnit::reset() noexcept {
        delay.reset();
        for (auto& s : absorptionState) s.reset();
        for (auto& ap : sapf) ap.reset();
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  EarlyReflections
    // ─────────────────────────────────────────────────────────────────────────────
    void EarlyReflections::prepare(const juce::dsp::ProcessSpec& spec) {
        // Max delay: 500 ms pre-delay + 200 ms ER
        int maxSamples = (int)(0.7 * spec.sampleRate) + 8;
        juce::dsp::ProcessSpec mono = spec;
        mono.numChannels = 1;
        buf.prepare(mono);
        buf.setMaximumDelayInSamples(maxSamples);

        erHPCoeffs = FilterDesign::highPass1st(80.f, spec.sampleRate);
        // Simple first-order LP for HF roll-off on ER
        float K = std::tan(juce::MathConstants<float>::pi * 6000.f / (float)spec.sampleRate);
        erLPCoeffs.b0 = K / (1.f + K);
        erLPCoeffs.b1 = erLPCoeffs.b0;
        erLPCoeffs.b2 = 0.f;
        erLPCoeffs.a1 = (K - 1.f) / (K + 1.f);
        erLPCoeffs.a2 = 0.f;
    }

    void EarlyReflections::buildTaps(const AlgorithmPreset& preset,
        float roomSizeScale, double sampleRate)
    {
        // Build 16-tap ISM-inspired ER pattern
        // Pattern derived from D50/C80 energy budget (docs §E)
        // D50 = fraction of energy in first 50 ms
        float d50_ref = preset.acoustics.d50[4];  // 500 Hz band
        float c80_ref = preset.acoustics.c80[4];
        float erEnergy50 = d50_ref;
        float erEnergy80 = std::pow(10.f, c80_ref / 10.f) /
            (1.f + std::pow(10.f, c80_ref / 10.f));

        // Base delay span = Lindau mixing time estimate
        float V = preset.volumeM3 > 0.f ? preset.volumeM3 : 10.f;
        float mixTimeMs = 0.0117f * V + 50.1f;  // Lindau t_mp95
        mixTimeMs = std::min(mixTimeMs, 150.f);

        // Generate 16 exponentially spaced taps up to mixTimeMs
        float span = mixTimeMs * roomSizeScale;
        for (int i = 0; i < ER_TAPS; ++i) {
            float t01 = static_cast<float>(i + 1) / static_cast<float>(ER_TAPS);
            float delMs = span * std::pow(t01, 1.5f);  // logarithmic spread
            taps[i].delaySamples = delMs * 0.001f * (float)sampleRate;

            // Amplitude: exponential decay based on RT60 mid
            float rt60m = preset.acoustics.rt60[4];
            float amp = std::exp(-6.9f * delMs * 0.001f / rt60m);

            // Energy normalisation to hit D50 target
            float factor = (i < ER_TAPS / 2) ? std::sqrt(erEnergy50)
                : std::sqrt(1.f - erEnergy50);
            amp *= factor * std::sqrt(2.f / ER_TAPS);

            // Stereo panning — alternate L/R with Haas-law distance cue
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
        // HP filter (remove sub from ER)
        L = erHPL.tick(L, erHPCoeffs);
        R = erHPR.tick(R, erHPCoeffs);
        return { L, R };
    }

    void EarlyReflections::reset() noexcept {
        buf.reset();
        erHPL.reset(); erHPR.reset();
        erLPL.reset(); erLPR.reset();
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  FDNReverbEngine
    // ─────────────────────────────────────────────────────────────────────────────
    FDNReverbEngine::FDNReverbEngine() {
        bIn.fill(1.f / std::sqrt((float)FDN_N));
        cOut.fill(1.f / std::sqrt((float)FDN_N));
        fbVec.fill(0.f);
        delaySamples = BASE_PRIMES_48K;
        effectiveRT60 = ALL_PRESETS[0]->acoustics.rt60;
    }

    void FDNReverbEngine::prepare(const juce::dsp::ProcessSpec& sp) {
        spec = sp;
        sampleRate = sp.sampleRate;
        prepared = true;

        // Pre-delay (max 500 ms)
        juce::dsp::ProcessSpec mono = sp;
        mono.numChannels = 1;
        preDelay.prepare(mono);
        preDelay.setMaximumDelayInSamples((int)(0.505 * sampleRate));

        // Early reflections
        er.prepare(sp);

        // Delay units
        rebuildDelayLengths();
        rebuildERTaps();
        rebuildAbsorptionFilters();

        // DC blocker on each line
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
        toneCorrStateL.reset();
        toneCorrStateR.reset();
        duckEnv = 0.f;
    }

    void FDNReverbEngine::setParams(const DSPParams& p) {
        juce::SpinLock::ScopedLockType lock(paramLock);
        pendingParams = p;
        paramsNeedUpdate.store(true, std::memory_order_release);
    }

    // ─── rebuild helpers ──────────────────────────────────────────────────────────
    void FDNReverbEngine::rebuildDelayLengths() {
        float scale = 0.5f + activeParams.roomSizeScale;  // 0.5–1.5 typical
        for (int i = 0; i < FDN_N; ++i) {
            // Scale primes from 48 kHz reference
            float scaled = BASE_PRIMES_48K[i] * scale * (float)(sampleRate / 48000.0);
            delaySamples[i] = std::max(64, (int)std::round(scaled));
        }

        // SAPF delays: 3 values per unit, log-spaced 2.5–6.3 ms, coprime
        // Fixed prime delays in ms×10 (e.g. 25 = 2.5 ms at 48 kHz)
        static constexpr std::array<std::array<int, SAPF_STAGES>, FDN_N> SAPF_DELAYS_MS10 = { {
            {{ 25,  41,  63 }},  // line 0
            {{ 29,  47,  71 }},  // line 1
            {{ 31,  53,  79 }},  // line 2
            {{ 37,  59,  83 }},  // line 3
            {{ 43,  61,  89 }},  // line 4
            {{ 47,  67,  97 }},  // line 5
            {{ 53,  73, 101 }},  // line 6
            {{ 59,  79, 107 }},  // line 7
        } };

        juce::dsp::ProcessSpec mono = spec;
        mono.numChannels = 1;

        for (int i = 0; i < FDN_N; ++i) {
            std::array<int, SAPF_STAGES> sapfSamples;
            for (int s = 0; s < SAPF_STAGES; ++s) {
                sapfSamples[s] = juce::roundToInt(
                    SAPF_DELAYS_MS10[i][s] * sampleRate / 10000.0);
            }
            int lfoOff = i * 360 / FDN_N;
            delayUnits[i].prepare(mono, delaySamples[i], lfoOff, sapfSamples);
        }
    }

    void FDNReverbEngine::rebuildAbsorptionFilters() {
        auto& preset = *ALL_PRESETS[activeParams.algorithmIndex];

        // Scale RT60 by user decayScale
        std::array<float, NUM_BANDS> scaledRT60 = preset.acoustics.rt60;
        for (auto& v : scaledRT60) v *= activeParams.decayScale;

        effectiveRT60 = scaledRT60;

        for (int i = 0; i < FDN_N; ++i) {
            auto coeffs = FilterDesign::designAbsorption(
                delaySamples[i], sampleRate,
                scaledRT60,
                activeParams.hfDamping,
                activeParams.lfAbsorption);
            delayUnits[i].setAbsorption(coeffs);
        }

        // Update LFO per unit: D_ms × f_LFO ≤ 0.184  (2-cent safety budget)
        float lfoHz = activeParams.modRate;
        float maxDepth = 0.184f / std::max(0.01f, lfoHz);  // ms
        float depthMs = activeParams.modAmount * maxDepth;
        float depthSmp = depthMs * 0.001f * (float)sampleRate;

        for (auto& u : delayUnits)
            u.setLFO(lfoHz, depthSmp, sampleRate);

        // Diffusion
        float diff = activeParams.diffusion;
        for (auto& u : delayUnits)
            u.setDiffusion(diff);

        // Input/output gain from energy budget
        // D50 → erLevel, C80 → late level
        float d50 = preset.acoustics.d50[4];
        float e_er80 = std::pow(10.f, preset.acoustics.c80[4] / 10.f) /
            (1.f + std::pow(10.f, preset.acoustics.c80[4] / 10.f));
        float lateAmp = std::sqrt(1.f - e_er80) * activeParams.lateLevel;
        for (int i = 0; i < FDN_N; ++i) {
            bIn[i] = lateAmp / std::sqrt((float)FDN_N);
            cOut[i] = lateAmp / std::sqrt((float)FDN_N);
        }

        // Ducking envelope time constants
        duckAtt = std::exp(-1.f / (activeParams.duckingAttackMs * 0.001f * (float)sampleRate));
        duckRel = std::exp(-1.f / (activeParams.duckingRelMs * 0.001f * (float)sampleRate));
    }

    void FDNReverbEngine::rebuildERTaps() {
        auto& preset = *ALL_PRESETS[activeParams.algorithmIndex];
        er.buildTaps(preset, activeParams.roomSizeScale, sampleRate);
        er.setPreDelay(activeParams.preDelayMs, sampleRate);
    }

    // ─── main process block ───────────────────────────────────────────────────────
    void FDNReverbEngine::processBlock(const float* inL, const float* inR,
        float* outL, float* outR,
        int numSamples) noexcept
    {
        // Pick up pending params (lock-free check)
        if (paramsNeedUpdate.load(std::memory_order_acquire)) {
            {
                juce::SpinLock::ScopedTryLockType tryLock(paramLock);
                if (tryLock.isLocked()) {
                    activeParams = pendingParams;
                    paramsNeedUpdate.store(false, std::memory_order_release);
                }
            }
            // NOTE: filter rebuild is deferred to avoid audio glitches;
            // in a production plugin this would use a separate rebuild request.
            // For prototype: rebuild immediately (acceptable for first-draft).
            const_cast<FDNReverbEngine*>(this)->rebuildAbsorptionFilters();
            const_cast<FDNReverbEngine*>(this)->rebuildERTaps();
        }

        // Gain factors
        float wetGain = juce::Decibels::decibelsToGain(activeParams.wetDB);
        float duckThreshLin = juce::Decibels::decibelsToGain(activeParams.duckingThreshDB);
        float duckAmount = juce::Decibels::decibelsToGain(-std::abs(activeParams.duckingAmount));
        float satAmt = activeParams.saturation;
        float width = activeParams.stereoWidth;

        for (int n = 0; n < numSamples; ++n) {
            float xL = inL[n];
            float xR = inR[n];
            float mono = (xL + xR) * 0.5f;

            // ── Early reflections ─────────────────────────────────────────────
            auto [erL, erR] = er.tick(mono);
            float erScale = activeParams.erLevel;

            // ── FDN: pre-delay on mono input ─────────────────────────────────
            preDelay.pushSample(0, mono);
            float xFDN = preDelay.popSample(0, (float)(
                juce::roundToInt(activeParams.preDelayMs * 0.001 * sampleRate)));

            // ── FDN core ──────────────────────────────────────────────────────
            // Householder mix of last-cycle's feedback vector
            householderMix(fbVec);
            applySignFlip(fbVec);

            float fdnOutL = 0.f, fdnOutR = 0.f;
            std::array<float, FDN_N> newFb;

            for (int i = 0; i < FDN_N; ++i) {
                float input = fbVec[i] + xFDN * bIn[i];
                float out = delayUnits[i].tick(input);

                // DC block
                out = dcState[i].tick(out, dcCoeffs[i]);

                // Saturation per channel
                out = softClip(out, satAmt);

                newFb[i] = out;

                // Stereo mixing: even → L+, odd → R+ with cross term
                if (i % 2 == 0) {
                    fdnOutL += cOut[i] * out;
                    fdnOutR += cOut[i] * out * (1.f - width);
                }
                else {
                    fdnOutR += cOut[i] * out;
                    fdnOutL += cOut[i] * out * (1.f - width);
                }
            }
            fbVec = newFb;

            // ── Ducking ───────────────────────────────────────────────────────
            float inLevel = std::abs(mono);
            if (inLevel > duckThreshLin)
                duckEnv = duckAtt * duckEnv + (1.f - duckAtt) * inLevel;
            else
                duckEnv = duckRel * duckEnv;

            float duckGain = 1.f;
            if (activeParams.duckingAmount > 0.1f && duckEnv > duckThreshLin * 0.5f)
                duckGain = juce::jmap(duckEnv,
                    duckThreshLin * 0.5f, duckThreshLin * 2.f,
                    1.f, duckAmount);

            // ── Combine ER + FDN ──────────────────────────────────────────────
            float reverbL = (fdnOutL + erL * erScale) * duckGain * wetGain;
            float reverbR = (fdnOutR + erR * erScale) * duckGain * wetGain;

            outL[n] = reverbL;
            outR[n] = reverbR;
        }
    }

    std::array<float, NUM_BANDS> FDNReverbEngine::getEffectiveRT60() const noexcept {
        return effectiveRT60;
    }

} // namespace FDNReverb