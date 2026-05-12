#include "UniversalEngine.h"

namespace FDNReverb {

    // ─────────────────────────────────────────────────────────────────────────────
    //  コンストラクタ・初期化
    // ─────────────────────────────────────────────────────────────────────────────
    UniversalEngine::UniversalEngine() {
        fbVec.fill(0.0f);
        for (int i = 0; i < FDN_ORDER; ++i) {
            lfos[i].state = 12345 + i * 9876;
        }
    }

    void UniversalEngine::prepare(double sampleRate, int /*maxBlockSize*/) {
        fs = sampleRate;

#if AMBIENCE_USE_STAGE2_ABSORPTION
        // Stage 2 用: Interaction Matrix を起動時にプリ計算
        MagnitudeResponseFitter::precomputeInteractionMatrix(sampleRate);
#endif

        // ── メモリプール割り当て ──
        auto getPow2 = [](size_t s) -> size_t {
            size_t p = 1;
            while (p < s) p *= 2;
            return p;
            };

        size_t totalMemoryNeeded =
            getPow2(static_cast<size_t>(fs * 1.0))
            + getPow2(static_cast<size_t>(fs * 0.05)) * 4
            + getPow2(static_cast<size_t>(fs * 0.5)) * FDN_ORDER
            + getPow2(static_cast<size_t>(fs * 0.1)) * FDN_ORDER;

        memoryPool.allocate(totalMemoryNeeded);

        // ── 遅延ラインの割り当て ──
        int mask = 0;
        float* ptr = nullptr;

        ptr = memoryPool.requestMemory(static_cast<size_t>(fs * 1.0), mask);
        erDelay.init(ptr, mask);

        for (int i = 0; i < 4; ++i) {
            ptr = memoryPool.requestMemory(static_cast<size_t>(fs * 0.05), mask);
            inputDiffusers[i].init(ptr, mask);
        }

        for (int i = 0; i < FDN_ORDER; ++i) {
            ptr = memoryPool.requestMemory(static_cast<size_t>(fs * 0.5), mask);
            fdnDelays[i].init(ptr, mask);

            ptr = memoryPool.requestMemory(static_cast<size_t>(fs * 0.1), mask);
            nestedAllpassDelays[i].init(ptr, mask);
        }

        // ── AcousticMetrics 初期化 ──
        acousticMetrics.prepare(sampleRate, 2000.0f);  // 2秒解析窓

        // ── ER パターン配列の初期化 ──
        currentERTapCount = 0;
        currentERDelaySamples.fill(0.0f);
        currentERGains.fill(0.0f);

        reset();
    }

    void UniversalEngine::reset() {
        memoryPool.clear();
        fbVec.fill(0.0f);

#if AMBIENCE_USE_STAGE2_ABSORPTION
        for (auto& lineFilters : absorptionFiltersS2) {
            for (auto& f : lineFilters) f.reset();
        }
#else
        for (auto& f : absorptionFilters) f.reset();
#endif

        acousticMetrics.reset();
        saturatorL.reset();
        saturatorR.reset();
    }

    void UniversalEngine::setParams(const DSPParams& p) {
        activeParams = p;
        switch (p.algorithmIndex) {
        case 0: case 1: currentTopology = ReverbTopology::Room;     break;
        case 2: case 3: currentTopology = ReverbTopology::Hall;     break;
        case 4:         currentTopology = ReverbTopology::Plate;    break;
        case 5:         currentTopology = ReverbTopology::Spring;   break;
        case 6:         currentTopology = ReverbTopology::Goldfoil; break;
        }
        updateTopologyAndRouting();
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  素数べき乗 (Prime Power) アルゴリズムによる遅延時間の算定
    // ─────────────────────────────────────────────────────────────────────────────
    void UniversalEngine::calculatePrimePowerDelays() {
        static constexpr std::array<int, FDN_ORDER> primes = {
            2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53
        };

        float baseSizeMs = 30.0f * (0.5f + activeParams.roomSizeScale);
        for (int i = 0; i < FDN_ORDER; ++i) {
            float targetSamples = (baseSizeMs + i * 5.0f) * 0.001f * static_cast<float>(fs);
            float m_i = std::round(std::log(targetSamples) / std::log(static_cast<float>(primes[i])));
            fdnBaseDelaySamples[i] = std::pow(static_cast<float>(primes[i]), m_i);
        }
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  動的トポロジー構成 (アルゴリズムごとの結線切り替え)
    // ─────────────────────────────────────────────────────────────────────────────
    void UniversalEngine::updateTopologyAndRouting() {
        calculatePrimePowerDelays();

        auto& preset = *ALL_PRESETS[activeParams.algorithmIndex];

        // ── RT60 スケーリング ──
        std::array<float, NUM_BANDS> scaledRT60 = preset.acoustics.rt60;
        for (auto& v : scaledRT60) v *= activeParams.decayScale;
        effectiveRT60 = scaledRT60;

#if AMBIENCE_USE_STAGE2_ABSORPTION
        // ── Stage 2c: 10段カスケード GEQ ──
        for (int i = 0; i < FDN_ORDER; ++i) {
            auto s2 = MagnitudeResponseFitter::designStage2(
                static_cast<int>(fdnBaseDelaySamples[i]), fs, scaledRT60,
                activeParams.hfDamping, activeParams.lfAbsorption);
            for (int b = 0; b < NUM_BANDS; ++b) {
                currentAbsorptionCoeffsS2[i][b] = s2.geqStages[b];
            }
        }
#else
        // ── Stage 1: Jot 直交化 1次 (1段) ──
        for (int i = 0; i < FDN_ORDER; ++i) {
            auto absoStages = FilterDesign::designAbsorption(
                static_cast<int>(fdnBaseDelaySamples[i]), fs, scaledRT60,
                activeParams.hfDamping, activeParams.lfAbsorption);
            currentAbsorptionCoeffs[i] = absoStages[0];
        }
#endif

        // ─────────────────────────────────────────────────────────────────────────
        //  Auto Gain Compensation (AGC) — Stage 2c キャリブレーション
        // ─────────────────────────────────────────────────────────────────────────
        float rt60Mid = std::max(0.1f, scaledRT60[4]);
        constexpr float baseDB = 28.7f;
        float decayCompDB = 7.0f * std::log10(rt60Mid);

        static constexpr std::array<float, 7> algorithmOffsetDB = {
            +0.8f,   // Room1
            +0.9f,   // Room2
            +0.5f,   // Hall1
            +0.5f,   // Hall2
            +1.5f,   // Plate
            +0.6f,   // Spring
            +0.6f    // Goldfoil
        };
        float algoOffset = algorithmOffsetDB[
            juce::jlimit(0, 6, activeParams.algorithmIndex)];

        // ── トポロジーごとの結線設定 ──
        switch (currentTopology) {
        case ReverbTopology::Room:
            bypassER = false; bypassInputDiffusers = true;
            apfGain = 0.3f;
            break;
        case ReverbTopology::Hall:
            bypassER = false; bypassInputDiffusers = false;
            apfGain = 0.618f;
            break;
        case ReverbTopology::Plate:
            bypassER = true; bypassInputDiffusers = false;
            apfGain = 0.7f;
            break;
        case ReverbTopology::Spring:
            bypassER = true; bypassInputDiffusers = false;
            apfGain = 0.5f;
            break;
        case ReverbTopology::Goldfoil:
            bypassER = true; bypassInputDiffusers = false;
            apfGain = 0.75f;
            break;
        }

        // ─────────────────────────────────────────────────────────────────────────
        //  ER パターンの更新 (プリセット選択時)
        // ─────────────────────────────────────────────────────────────────────────
        const auto& erPattern = PRESET_ER_PATTERNS[
            juce::jlimit(0, 6, activeParams.algorithmIndex)];
        currentERTapCount = erPattern.numTaps;

        float erSizeScale = 0.5f + activeParams.roomSizeScale;
        for (int i = 0; i < erPattern.numTaps; ++i) {
            currentERDelaySamples[i] = erPattern.taps[i].delayMs * 0.001f
                * static_cast<float>(fs) * erSizeScale;
            currentERGains[i] = erPattern.taps[i].gain;
        }
        if (erPattern.numTaps == 0) bypassER = true;

        // ─────────────────────────────────────────────────────────────────────────
        //  EDT 理論計算
        // ─────────────────────────────────────────────────────────────────────────
        float edtCoeff = 0.7f;
        switch (currentTopology) {
        case ReverbTopology::Room:     edtCoeff = 0.70f; break;
        case ReverbTopology::Hall:     edtCoeff = 0.95f; break;
        case ReverbTopology::Plate:    edtCoeff = 0.60f; break;
        case ReverbTopology::Spring:   edtCoeff = 0.50f; break;
        case ReverbTopology::Goldfoil: edtCoeff = 0.85f; break;
        }
        theoreticalEDT = rt60Mid * edtCoeff;

        // ─────────────────────────────────────────────────────────────────────────
        //  Saturator 設定 (プリセット別の倍音特性 + Mode 切替)
        // ─────────────────────────────────────────────────────────────────────────
        // ─ プリセット別サチュレーション強度 ─
        // Plate/Spring/Goldfoil は金属系で強めの倍音、Room/Hall は控えめ
        float saturationMultiplier = 1.0f;
        switch (currentTopology) {
        case ReverbTopology::Room:     saturationMultiplier = 0.6f; break;
        case ReverbTopology::Hall:     saturationMultiplier = 0.7f; break;
        case ReverbTopology::Plate:    saturationMultiplier = 1.0f; break;
        case ReverbTopology::Spring:   saturationMultiplier = 1.2f; break;
        case ReverbTopology::Goldfoil: saturationMultiplier = 1.1f; break;
        }

        float effectiveSatAmount = activeParams.saturation * saturationMultiplier;
        effectiveSatAmount = juce::jlimit(0.0f, 1.0f, effectiveSatAmount);

        saturatorL.setAmount(effectiveSatAmount);
        saturatorR.setAmount(effectiveSatAmount);

        // ─ Saturation Type の反映 (ProMode の SatType セレクタ値) ─
        // 0=Warm / 1=Tape / 2=Tube / 3=Hard
        // setMode 内でモード変化があれば履歴を reset() する (クリック防止)
        saturatorL.setMode(activeParams.satTypeIdx);
        saturatorR.setMode(activeParams.satTypeIdx);

        // ── 最終ゲイン補正 ──
        float totalLateMakeupDB = baseDB + decayCompDB + algoOffset;
        lateMakeupGainLinear = juce::Decibels::decibelsToGain(totalLateMakeupDB);
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  FWHT (O(N log N) の無損失マトリクス) & Sign Flipping
    // ─────────────────────────────────────────────────────────────────────────────
    inline void UniversalEngine::fastWalshHadamardTransform(std::array<float, 16>& v) noexcept {
        for (int h = 1; h < 16; h *= 2) {
            for (int i = 0; i < 16; i += h * 2) {
                for (int j = i; j < i + h; ++j) {
                    float x = v[j];
                    float y = v[j + h];
                    v[j] = x + y;
                    v[j + h] = x - y;
                }
            }
        }
        for (int i = 0; i < 16; ++i) v[i] *= 0.25f;
    }

    inline void UniversalEngine::applySignFlipping(std::array<float, 16>& v) noexcept {
        static constexpr std::array<float, 16> flip = {
             1.f, -1.f,  1.f, -1.f, -1.f,  1.f, -1.f,  1.f,
             1.f,  1.f, -1.f, -1.f, -1.f, -1.f,  1.f,  1.f
        };
        for (int i = 0; i < 16; ++i) v[i] *= flip[i];
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  メイン DSP 処理ループ
    // ─────────────────────────────────────────────────────────────────────────────
    void UniversalEngine::processBlock(const float* inL, const float* inR,
        float* outL, float* outR, int numSamples) noexcept {
        float depthSamples = activeParams.modAmount * 0.002f * static_cast<float>(fs);
        float wetGain = juce::Decibels::decibelsToGain(activeParams.wetDB);
        const float stereoWidth = activeParams.stereoWidth;

        for (int n = 0; n < numSamples; ++n) {
            // ── 入力 L/R 分離 ──
            float leftIn = inL[n];
            float rightIn = inR[n];
            float midIn = (leftIn + rightIn) * 0.5f;
            float sideIn = (leftIn - rightIn) * 0.5f;
            float erOutL = 0.0f, erOutR = 0.0f;

            // ── 1. Input Diffusers (モノラル拡散) ──
            float fdnInputMid = midIn;
            if (!bypassInputDiffusers) {
                for (int i = 0; i < 4; ++i) {
                    float delaySmp = (3.0f + i * 2.0f) * 0.001f * fs;
                    float d = inputDiffusers[i].read(delaySmp);
                    float w = fdnInputMid + 0.618f * d;
                    inputDiffusers[i].write(w);
                    fdnInputMid = d - 0.618f * w;
                }
            }

            // ── 2. ER Tapped Delay (プリセット別 ISM パターン) ──
            // 偶数タップを L 寄り、奇数タップを R 寄りに振り分けて自然なステレオ感
            if (!bypassER) {
                erDelay.write(midIn);
                float erTotalL = 0.0f;
                float erTotalR = 0.0f;
                for (int t = 0; t < currentERTapCount; ++t) {
                    float tapValue = erDelay.read(currentERDelaySamples[t]);
                    float tapGain = currentERGains[t] * 0.5f;
                    if (t % 2 == 0) {
                        erTotalL += tapValue * tapGain;
                        erTotalR += tapValue * tapGain * 0.7f;
                    }
                    else {
                        erTotalR += tapValue * tapGain;
                        erTotalL += tapValue * tapGain * 0.7f;
                    }
                }
                erOutL = erTotalL;
                erOutR = erTotalR;
            }

            // ── 3. FDN + Nested Allpass Loop (16ch) ──
            std::array<float, 16> currentFb = fbVec;
            fastWalshHadamardTransform(currentFb);
            applySignFlipping(currentFb);

            float fdnOutL = 0.0f, fdnOutR = 0.0f;
            std::array<float, 16> nextFb;

            for (int i = 0; i < FDN_ORDER; ++i) {
                // モジュレーション + 遅延読み出し
                float lfoVal = lfos[i].tick(activeParams.modRate, fs);
                float delaySmp = fdnBaseDelaySamples[i] + lfoVal * depthSamples;
                float d = fdnDelays[i].read(delaySmp);

                // 吸収フィルタ (Stage 2c: 10段 GEQ)
#if AMBIENCE_USE_STAGE2_ABSORPTION
                for (int s = 0; s < ABSO_STAGES_S2; ++s) {
                    d = absorptionFiltersS2[i][s].tick(d, currentAbsorptionCoeffsS2[i][s]);
                }
#else
                d = absorptionFilters[i].tick(d, currentAbsorptionCoeffs[i]);
#endif

                // Nested Allpass Filter
                float apfDelaySmp = (1.5f + i * 0.3f) * 0.001f * fs;
                float apfD = nestedAllpassDelays[i].read(apfDelaySmp);
                float apfW = d + apfGain * apfD;
                nestedAllpassDelays[i].write(apfW);
                float apfOut = apfD - apfGain * apfW;

                nextFb[i] = apfOut;

                // L/R 別々のサイド成分注入で、FDN 内に位相差を保持
                float sideForCh = (i % 2 == 0 ? +sideIn : -sideIn) * stereoWidth;
                float fdnInputForThisCh = (fdnInputMid + sideForCh) * 0.25f;
                fdnDelays[i].write(fdnInputForThisCh + currentFb[i]);

                // L/R 振り分け (width で混合度を変える)
                if (i % 2 == 0) {
                    fdnOutL += apfOut;
                    fdnOutR += apfOut * (1.0f - stereoWidth);
                }
                else {
                    fdnOutR += apfOut;
                    fdnOutL += apfOut * (1.0f - stereoWidth);
                }
            }

            // 正規化: 16ch のうち L 寄り 8ch / R 寄り 8ch なので 1/8 = 0.125
            fdnOutL *= 0.125f;
            fdnOutR *= 0.125f;
            fbVec = nextFb;

            // ── 4. 最終ミックス ──
            float erMixL = bypassER ? 0.0f : erOutL * activeParams.erLevel;
            float erMixR = bypassER ? 0.0f : erOutR * activeParams.erLevel;
            float lateMixL = fdnOutL * lateMakeupGainLinear * activeParams.lateLevel;
            float lateMixR = fdnOutR * lateMakeupGainLinear * activeParams.lateLevel;

            // AcousticMetrics に Wet を投入 (モノミックス)
            float wetMono = (lateMixL + lateMixR) * 0.5f;
            acousticMetrics.processSample(wetMono);

            // ── 5. Saturation 適用 (Late のみ、Mode 切替対応) ──
            // 資料の Valhalla 知見: FDN ループ出力段にサチュレーションを配置することで
            // フィードバックを通じて倍音が蓄積し、「Spectral Plasma」が形成される。
            // SaturationMode に応じて Warm/Tape/Tube/Hard が選択される (ProMode 専用)。
            // FDN ループ内 HF Damping がエイリアスを反復的に除去するため OS は不要。
            float satL = saturatorL.processSample(lateMixL);
            float satR = saturatorR.processSample(lateMixR);

            // ── 6. 最終出力 (ER は線形のまま、Late のみサチュレーション適用) ──
            outL[n] = (erMixL + satL) * wetGain;
            outR[n] = (erMixR + satR) * wetGain;
        }
    }

} // namespace FDNReverb