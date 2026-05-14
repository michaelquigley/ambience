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
        acousticMetrics.prepare(sampleRate, 2000.0f);

        // ── ER パターン配列の初期化 ──
        currentERTapCount = 0;
        currentERDelaySamples.fill(0.0f);
        currentERGains.fill(0.0f);

        // ★ Phase 3-1 追加: OutputLimiter 初期化
        outputLimiter.prepare(sampleRate);

        // ★ Phase 3-1 追加: Ducking エンベロープ係数初期化（デフォルト値で）
        //   後で setParams() で更新される
        duckingAttackCoeff = 1.0f - std::exp(-1.0f / (static_cast<float>(fs) * 0.010f)); // 10ms
        duckingReleaseCoeff = 1.0f - std::exp(-1.0f / (static_cast<float>(fs) * 0.200f)); // 200ms
        duckingEnvelope = 0.0f;

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

        // ★ Phase 3-1 追加
        outputLimiter.reset();
        duckingEnvelope = 0.0f;
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

        // ★ Phase 3-1 追加: Ducking 係数の動的更新
        //   ms → 秒 → 1次LPF係数
        const float attMs = juce::jmax(0.1f, activeParams.duckingAttackMs);
        const float relMs = juce::jmax(0.1f, activeParams.duckingRelMs);
        duckingAttackCoeff = 1.0f - std::exp(-1.0f / (static_cast<float>(fs) * attMs * 0.001f));
        duckingReleaseCoeff = 1.0f - std::exp(-1.0f / (static_cast<float>(fs) * relMs * 0.001f));

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

        // ─────────────────────────────────────────────────────────────────────
        // ★ Phase 4 追加: RT60 スケーリング + ProMode 拡張
        // ─────────────────────────────────────────────────────────────────────
        //   Normal Mode: decayScale のみ適用
        //   Pro Mode:    decayScale + Tilt EQ x3 + 帯域別 RT60 ノブ x10
        //
        //   Tilt EQ 帯域マッピング (10 バンド構成):
        //     - tiltLow:  bands 0-2 (31, 62.5, 125 Hz)
        //     - tiltMid:  bands 3-6 (250, 500, 1k, 2k Hz)
        //     - tiltHigh: bands 7-9 (4k, 8k, 16k Hz)
        // ─────────────────────────────────────────────────────────────────────
        std::array<float, NUM_BANDS> scaledRT60 = preset.acoustics.rt60;
        for (auto& v : scaledRT60) v *= activeParams.decayScale;

        if (activeParams.proMode) {
            // ─ Tilt EQ 適用 ─
            scaledRT60[0] *= activeParams.tiltLow;
            scaledRT60[1] *= activeParams.tiltLow;
            scaledRT60[2] *= activeParams.tiltLow;
            scaledRT60[3] *= activeParams.tiltMid;
            scaledRT60[4] *= activeParams.tiltMid;
            scaledRT60[5] *= activeParams.tiltMid;
            scaledRT60[6] *= activeParams.tiltMid;
            scaledRT60[7] *= activeParams.tiltHigh;
            scaledRT60[8] *= activeParams.tiltHigh;
            scaledRT60[9] *= activeParams.tiltHigh;

            // ─ 帯域別 RT60 ノブ x10 適用 ─
            for (int b = 0; b < NUM_BANDS; ++b) {
                scaledRT60[b] *= activeParams.rtBands[b];
            }
        }

        effectiveRT60 = scaledRT60;

#if AMBIENCE_USE_STAGE2_ABSORPTION
        for (int i = 0; i < FDN_ORDER; ++i) {
            auto s2 = MagnitudeResponseFitter::designStage2(
                static_cast<int>(fdnBaseDelaySamples[i]), fs, scaledRT60,
                activeParams.hfDamping, activeParams.lfAbsorption);
            for (int b = 0; b < NUM_BANDS; ++b) {
                currentAbsorptionCoeffsS2[i][b] = s2.geqStages[b];
            }
        }
#else
        for (int i = 0; i < FDN_ORDER; ++i) {
            auto absoStages = FilterDesign::designAbsorption(
                static_cast<int>(fdnBaseDelaySamples[i]), fs, scaledRT60,
                activeParams.hfDamping, activeParams.lfAbsorption);
            currentAbsorptionCoeffs[i] = absoStages[0];
        }
#endif

        // ── Auto Gain Compensation (AGC) ──
        float rt60Mid = std::max(0.1f, scaledRT60[4]);
        constexpr float baseDB = 28.7f;
        float decayCompDB = 7.0f * std::log10(rt60Mid);

        static constexpr std::array<float, 7> algorithmOffsetDB = {
            +0.8f, +0.9f, +0.5f, +0.5f, +1.5f, +0.6f, +0.6f
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

        // ── ER パターン更新 ──
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

        // ── EDT 理論計算 ──
        float edtCoeff = 0.7f;
        switch (currentTopology) {
        case ReverbTopology::Room:     edtCoeff = 0.70f; break;
        case ReverbTopology::Hall:     edtCoeff = 0.95f; break;
        case ReverbTopology::Plate:    edtCoeff = 0.60f; break;
        case ReverbTopology::Spring:   edtCoeff = 0.50f; break;
        case ReverbTopology::Goldfoil: edtCoeff = 0.85f; break;
        }
        theoreticalEDT = rt60Mid * edtCoeff;

        // ── Saturator 設定 ──
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
        saturatorL.setMode(activeParams.satTypeIdx);
        saturatorR.setMode(activeParams.satTypeIdx);

        // ── 最終ゲイン補正 ──
        float totalLateMakeupDB = baseDB + decayCompDB + algoOffset;
        lateMakeupGainLinear = juce::Decibels::decibelsToGain(totalLateMakeupDB);
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  FWHT & Sign Flipping
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
        const float crossFeedAmt = activeParams.crossFeed;  // ★ Phase 3-1
        const float erLevel = activeParams.erLevel;
        const float lateLevel = activeParams.lateLevel;
        const bool erSolo = activeParams.erSolo;            // ★ Phase 3-1

        // ★ Phase 3-1: Ducking の Threshold をリニア化（毎サンプル log10 を避ける）
        const float duckThreshLin = juce::Decibels::decibelsToGain(activeParams.duckingThreshDB);
        const float duckAmountDB = activeParams.duckingAmount;  // 0-20 dB

        for (int n = 0; n < numSamples; ++n) {
            // ── 入力 L/R 分離 ──
            float leftIn = inL[n];
            float rightIn = inR[n];
            float midIn = (leftIn + rightIn) * 0.5f;
            float sideIn = (leftIn - rightIn) * 0.5f;
            float erOutL = 0.0f, erOutR = 0.0f;

            // ────────────────────────────────────────────────────────────────
            // ★ Phase 3-1: Ducking エンベロープ検出
            // ────────────────────────────────────────────────────────────────
            //   入力信号の絶対値ピークから RMS 風のエンベロープを抽出。
            //   信号レベル > Threshold のとき、Wet ゲインを duckingAmount だけ下げる。
            //   これにより、ドラムキック等の入力が強い瞬間に残響が引っ込み、
            //   原音の輪郭がクリアに保たれる (典型的なリバーブ-ダッキング効果)。
            // ────────────────────────────────────────────────────────────────
            const float inputPeak = juce::jmax(std::abs(leftIn), std::abs(rightIn));
            const float envCoeff = (inputPeak > duckingEnvelope) ? duckingAttackCoeff : duckingReleaseCoeff;
            duckingEnvelope += (inputPeak - duckingEnvelope) * envCoeff;

            float duckGainLinear = 1.0f;
            if (duckAmountDB > 0.001f && duckingEnvelope > duckThreshLin) {
                // エンベロープが threshold を超えた量を dB で算出
                // 注意: log10 は重いが、duckAmountDB > 0 の条件下でのみ実行される
                const float envDB = 20.0f * std::log10(juce::jmax(duckingEnvelope, 1e-6f));
                const float threshDB = activeParams.duckingThreshDB;
                const float overDB = envDB - threshDB;
                // gainReduction を duckingAmount でクランプ
                const float gainReductionDB = -juce::jmin(overDB, duckAmountDB);
                duckGainLinear = juce::Decibels::decibelsToGain(gainReductionDB);
            }

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

            // ── 2. ER Tapped Delay ──
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
                float lfoVal = lfos[i].tick(activeParams.modRate, fs);
                float delaySmp = fdnBaseDelaySamples[i] + lfoVal * depthSamples;
                float d = fdnDelays[i].read(delaySmp);

                // ─ 吸収フィルタ ─
#if AMBIENCE_USE_STAGE2_ABSORPTION
                for (int s = 0; s < ABSO_STAGES_S2; ++s) {
                    d = absorptionFiltersS2[i][s].tick(d, currentAbsorptionCoeffsS2[i][s]);
                }
#else
                d = absorptionFilters[i].tick(d, currentAbsorptionCoeffs[i]);
#endif

                // ────────────────────────────────────────────────────────────
                // ★ Phase 3-1: FDN ループ内マイクロサチュレーション (Layer 1)
                // ────────────────────────────────────────────────────────────
                //   配置: 吸収フィルタ直後、Nested Allpass の前。
                //
                //   目的:
                //     1) 受動性(Passivity)の保証 → リミットサイクル消滅
                //     2) アナログ感の付加 → 「Spectral Plasma」形成
                //     3) ハーモニックスマスキング → ループ吸収で滑らかな倍音
                //
                //   実装: Padé 有理多項式 x(27+x²)/(27+9x²)
                //         - クランプ x ∈ [-3, 3] (発散完全防止)
                //         - 加算・乗算・除算 1 回のみ (tanh 比 10 倍速)
                //         - HF Damping が後続でエイリアスを除去 → OS 不要
                //
                //   このサチュレーションはユーザー操作不可 (固定パラメータ)。
                //   ユーザーが操作する Wet 出力サチュレーション (Layer 2) は
                //   後段の saturatorL/R で別途処理される。
                // ────────────────────────────────────────────────────────────
                d = processMicroSaturation(d);

                // ─ Nested Allpass Filter ─
                float apfDelaySmp = (1.5f + i * 0.3f) * 0.001f * fs;
                float apfD = nestedAllpassDelays[i].read(apfDelaySmp);
                float apfW = d + apfGain * apfD;
                nestedAllpassDelays[i].write(apfW);
                float apfOut = apfD - apfGain * apfW;

                nextFb[i] = apfOut;

                // L/R 別々のサイド成分注入
                float sideForCh = (i % 2 == 0 ? +sideIn : -sideIn) * stereoWidth;
                float fdnInputForThisCh = (fdnInputMid + sideForCh) * 0.25f;
                fdnDelays[i].write(fdnInputForThisCh + currentFb[i]);

                // L/R 振り分け
                if (i % 2 == 0) {
                    fdnOutL += apfOut;
                    fdnOutR += apfOut * (1.0f - stereoWidth);
                }
                else {
                    fdnOutR += apfOut;
                    fdnOutL += apfOut * (1.0f - stereoWidth);
                }
            }

            fdnOutL *= 0.125f;
            fdnOutR *= 0.125f;
            fbVec = nextFb;

            // ────────────────────────────────────────────────────────────────
            // ★ Phase 3-1: CrossFeed (L/R 間のステレオブレンド)
            // ────────────────────────────────────────────────────────────────
            //   設計:
            //     crossFeed = 0.0 → 完全分離ステレオ (各チャンネル独立)
            //     crossFeed = 0.5 → 部分混合 (バイノーラル風)
            //     crossFeed = 1.0 → 完全モノ (両チャンネル同一)
            //
            //   用途:
            //     - ヘッドフォン用途で過度なステレオ分離を抑える
            //     - 古い録音や AM ラジオ風の演出
            //     - L/R の整合性を保ちながら自然な定位を作る
            //
            //   実装: 線形補間でクロスゲイン
            //     L' = L*(1-x/2) + R*(x/2)
            //     R' = R*(1-x/2) + L*(x/2)
            //   (x/2 とすることで、x=1 でも各チャンネルの寄与は 50:50)
            // ────────────────────────────────────────────────────────────────
            const float xfeed = crossFeedAmt * 0.5f;
            const float origL = fdnOutL;
            const float origR = fdnOutR;
            fdnOutL = origL * (1.0f - xfeed) + origR * xfeed;
            fdnOutR = origR * (1.0f - xfeed) + origL * xfeed;

            // ── 4. 最終ミックス ──
            float erMixL = bypassER ? 0.0f : erOutL * erLevel;
            float erMixR = bypassER ? 0.0f : erOutR * erLevel;
            float lateMixL = fdnOutL * lateMakeupGainLinear * lateLevel;
            float lateMixR = fdnOutR * lateMakeupGainLinear * lateLevel;

            // AcousticMetrics に Wet を投入
            float wetMono = (lateMixL + lateMixR) * 0.5f;
            acousticMetrics.processSample(wetMono);

            // ── 5. Saturation 適用 (Layer 2: Wet 出力) ──
            float satL = saturatorL.processSample(lateMixL);
            float satR = saturatorR.processSample(lateMixR);

            // ────────────────────────────────────────────────────────────────
            // ★ Phase 3-1: ER SOLO 機能
            // ────────────────────────────────────────────────────────────────
            //   ER SOLO 有効時: Late ミックス (FDN サチュレーション含む) を 0 に。
            //   初期反射のみを出力。
            //
            //   用途:
            //     - 空間定位の確認 (ER だけで「部屋らしさ」を聴く)
            //     - ER パターンのチューニング・デバッグ
            //     - クリエイティブ用途 (短いプリディレイのみ欲しい場合)
            // ────────────────────────────────────────────────────────────────
            if (erSolo) {
                satL = 0.0f;
                satR = 0.0f;
            }

            // ── 6. 最終出力 (Wet ゲイン + Ducking 適用) ──
            const float finalWetGain = wetGain * duckGainLinear;
            outL[n] = (erMixL + satL) * finalWetGain;
            outR[n] = (erMixR + satR) * finalWetGain;

            // ────────────────────────────────────────────────────────────────
            // ★ Phase 3-1: OutputLimiter (最終安全装置)
            // ────────────────────────────────────────────────────────────────
            //   Threshold = -0.5 dBFS のブリックウォール・リミッター。
            //   ユーザーが操作するパラメータなし (純粋な安全装置)。
            //
            //   設計意図:
            //     - 過大な Wet レベル + 強い入力 → クリッピング → 不快なデジタル歪み
            //       を防止。
            //     - Attack 0.5ms / Release 50ms で自然な制動。
            //     - L/R に同じゲインを適用 → ステレオイメージ完全保持。
            // ────────────────────────────────────────────────────────────────
            outputLimiter.process(outL[n], outR[n]);
        }
    }

} // namespace FDNReverb