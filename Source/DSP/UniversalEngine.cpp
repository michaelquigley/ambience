#include "UniversalEngine.h"

namespace FDNReverb {
    UniversalEngine::UniversalEngine() {
        fbVec.fill(0.0f);
        for (int i = 0; i < FDN_ORDER; ++i) lfos[i].state = 12345 + i * 9876;
    }

    void UniversalEngine::prepare(double sampleRate, int /*maxBlockSize*/) {
        fs = sampleRate;

#if AMBIENCE_USE_STAGE2_ABSORPTION
        // Stage 2 用: Interaction Matrix を起動時にプリ計算
        MagnitudeResponseFitter::precomputeInteractionMatrix(sampleRate);
#endif

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
        // ─── 追加: AcousticMetrics 初期化 ───
        acousticMetrics.prepare(sampleRate, 2000.0f);  // 2秒解析窓

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

        // ─── 追加: AcousticMetrics リセット ───
        acousticMetrics.reset();
    }

    void UniversalEngine::setParams(const DSPParams& p) {
        activeParams = p;
        switch (p.algorithmIndex) {
        case 0: case 1: currentTopology = ReverbTopology::Room; break;
        case 2: case 3: currentTopology = ReverbTopology::Hall; break;
        case 4:         currentTopology = ReverbTopology::Plate; break;
        case 5:         currentTopology = ReverbTopology::Spring; break;
        case 6:         currentTopology = ReverbTopology::Goldfoil; break;
        }
        updateTopologyAndRouting();
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // 素数べき乗 (Prime Power) アルゴリズムによる遅延時間の算定
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
    // 動的トポロジー構成（アルゴリズムごとの結線切り替え）
    // ─────────────────────────────────────────────────────────────────────────────
    void UniversalEngine::updateTopologyAndRouting() {
        calculatePrimePowerDelays();
        auto& preset = *ALL_PRESETS[activeParams.algorithmIndex];
        std::array<float, NUM_BANDS> scaledRT60 = preset.acoustics.rt60;
        for (auto& v : scaledRT60) v *= activeParams.decayScale;
        effectiveRT60 = scaledRT60;

#if AMBIENCE_USE_STAGE2_ABSORPTION
        // ─── Stage 2c: 10段カスケード GEQ (midGain は band 0 に吸収) ───
        for (int i = 0; i < FDN_ORDER; ++i) {
            auto s2 = MagnitudeResponseFitter::designStage2(
                static_cast<int>(fdnBaseDelaySamples[i]), fs, scaledRT60,
                activeParams.hfDamping, activeParams.lfAbsorption);

            for (int b = 0; b < NUM_BANDS; ++b) {
                currentAbsorptionCoeffsS2[i][b] = s2.geqStages[b];
            }
        }
#else
        // ─── Stage 1: Jot 直交化1次 (1段のみ使用) ───
        for (int i = 0; i < FDN_ORDER; ++i) {
            auto absoStages = FilterDesign::designAbsorption(
                static_cast<int>(fdnBaseDelaySamples[i]), fs, scaledRT60,
                activeParams.hfDamping, activeParams.lfAbsorption);
            currentAbsorptionCoeffs[i] = absoStages[0];
        }
#endif

        // ─────────────────────────────────────────────────────────────────────────
        // 動的 Auto Gain Compensation (AGC) の計算 ── Stage 2c 第一次再キャリブレーション
        // ─────────────────────────────────────────────────────────────────────────
        //
        //   totalLateMakeupDB = baseDB + decayCompDB + algorithmOffsetDB
        //
        //   baseDB = 22.5 dB
        //     16次FWHTマトリクスとSAPFループでの固定エネルギー損失を相殺するキャリブレーション値
        //     Stage 1 時代の 24.0dB から Stage 2c の正確な吸収特性に合わせて -1.5dB 引き下げ
        //
        //   decayCompDB = 5.0 · log10(rt60Mid)
        //     RT60 が長くなった分、エネルギーが時間方向に引き伸ばされ RMS が低下するのを補正
        //     Stage 1 の係数 10.0 は Stage 2c の正確な吸収特性に対して過剰だったため 5.0 に変更
        //     RT60=1.0s で 0dB、=2.0s で +1.5dB、=4.0s で +3.0dB の補正となる
        //
        //   algorithmOffsetDB
        //     アルゴリズムごとの個性 (RT60カーブ複雑度・ループ構造) に応じた最終補正
        //     Stage 1 では topology (Room/Hall/Plate...) 単位で +0〜+6dB の加算補償だったが、
        //     Stage 2c では正確な減衰再現により逆方向 (減算) の補正が必要
        //     Room1 のみ RT60 が極端に短いため例外的に +1.5dB
        // ─────────────────────────────────────────────────────────────────────────
        float rt60Mid = std::max(0.1f, scaledRT60[4]);

        // 修正後
        constexpr float baseDB = 28.7f;  // 28.2 → +0.5dB
        float decayCompDB = 7.0f * std::log10(rt60Mid);

        // アルゴリズムインデックス別オフセット (0..6)
        //   0: Room1   +1.5  (RT60 極短の補償)
        //   1: Room2   -1.5
        //   2: Hall1   -1.5
        //   3: Hall2   -1.5
        //   4: Plate   -2.0
        //   5: Spring  -3.5
        //   6: Goldfoil -4.5
        static constexpr std::array<float, 7> algorithmOffsetDB = {
            +0.8f,   // Room1    (変更なし、基準)
            +0.9f,   // Room2    (変更なし)
            +0.5f,   // Hall1    (変更なし)
            +0.5f,   // Hall2    (変更なし)
            +1.5f,   // Plate    (上げる) ← -1.5 から +1.0dB
            +0.6f,   // Spring   (上げる) ← -2.2 から +1.0dB
            +0.6f    // Goldfoil (上げる) ← -3.2 から +1.4dB
        };

        float algoOffset = algorithmOffsetDB[
            juce::jlimit(0, 6, activeParams.algorithmIndex)];

        // トポロジーごとの結線設定 (音量補正は algorithmOffsetDB に統合済み)
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
    // EDT (Early Decay Time) の理論計算
    // ─────────────────────────────────────────────────────────────────────────
    // EDT = 中域RT60 × トポロジー別係数
    // 経験則：
    //   Room (小型空間)     ≈ 0.7  × RT60 (初期反射が早く減衰)
    //   Hall (大型空間)     ≈ 0.95 × RT60 (緩やかな減衰)
    //   Plate (金属板)      ≈ 0.6  × RT60 (高密度初期反射)
    //   Spring (バネ)       ≈ 0.5  × RT60 (急激な初期減衰)
    //   Goldfoil (金属箔)   ≈ 0.85 × RT60 (中庸)
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


        float totalLateMakeupDB = baseDB + decayCompDB + algoOffset;
        lateMakeupGainLinear = juce::Decibels::decibelsToGain(totalLateMakeupDB);
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // FWHT (O(N log N) の無損失マトリクス) & Sign Flipping
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
    // メインDSP処理ループ
    // ─────────────────────────────────────────────────────────────────────────────
    void UniversalEngine::processBlock(const float* inL, const float* inR, float* outL, float* outR, int numSamples) noexcept {
        float depthSamples = activeParams.modAmount * 0.002f * static_cast<float>(fs);
        float wetGain = juce::Decibels::decibelsToGain(activeParams.wetDB);

        // ステレオ幅: 0.0 = モノラル、1.0 = フルステレオ
        const float stereoWidth = activeParams.stereoWidth;

        for (int n = 0; n < numSamples; ++n) {
            // ─── 修正1: 入力のモノ化を廃止 ───
            // L/R を別々の信号として保持
            float leftIn = inL[n];
            float rightIn = inR[n];
            // モノラル成分（センター）とサイド成分（ステレオ）に分離
            float midIn = (leftIn + rightIn) * 0.5f;
            float sideIn = (leftIn - rightIn) * 0.5f;

            float erOutL = 0.0f, erOutR = 0.0f;

            // 1. Input Diffusers (モノラル拡散)
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

            // 2. ER Tapped Delay
            if (!bypassER) {
                erDelay.write(midIn);
                float erTotal = 0.0f;
                erTotal += erDelay.read(15.0f * 0.001f * fs) * 0.5f;
                erTotal += erDelay.read(27.0f * 0.001f * fs) * 0.4f;
                erTotal += erDelay.read(41.0f * 0.001f * fs) * 0.3f;
                erTotal += erDelay.read(59.0f * 0.001f * fs) * 0.2f;
                erOutL = erTotal;
                erOutR = erTotal;
            }

            // 3. FDN + Nested Allpass Loop
            std::array<float, 16> currentFb = fbVec;
            fastWalshHadamardTransform(currentFb);
            applySignFlipping(currentFb);

            float fdnOutL = 0.0f, fdnOutR = 0.0f;
            std::array<float, 16> nextFb;

            for (int i = 0; i < FDN_ORDER; ++i) {
                float lfoVal = lfos[i].tick(activeParams.modRate, fs);
                float delaySmp = fdnBaseDelaySamples[i] + lfoVal * depthSamples;
                float d = fdnDelays[i].read(delaySmp);

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

                // ─── 修正2: FDN 入力に L/R 別々のサイド成分を注入 ───
                // 各チャンネルに「センター + サイド成分」を異なる位相で注入
                // 偶数 ch には +sideIn、奇数 ch には -sideIn を加える
                // これにより L/R で異なる FDN 励起が起きる
                float sideForCh = (i % 2 == 0 ? +sideIn : -sideIn) * stereoWidth;
                float fdnInputForThisCh = (fdnInputMid + sideForCh) * 0.25f;
                fdnDelays[i].write(fdnInputForThisCh + currentFb[i]);

                // ─── 修正3: 出力もシンプルに L/R 振り分け ───
                // 偶数 ch → L、奇数 ch → R で固定（width で混合度を変える）
                if (i % 2 == 0) {
                    fdnOutL += apfOut;
                    fdnOutR += apfOut * (1.0f - stereoWidth);
                }
                else {
                    fdnOutR += apfOut;
                    fdnOutL += apfOut * (1.0f - stereoWidth);
                }
            }

            // 正規化（8ch ずつなので 8 で割る）
            fdnOutL *= 0.125f;  // 1/8
            fdnOutR *= 0.125f;

            fbVec = nextFb;

            // 4. 最終出力
            float erMixL = bypassER ? 0.0f : erOutL * activeParams.erLevel;
            float erMixR = bypassER ? 0.0f : erOutR * activeParams.erLevel;
            float lateMixL = fdnOutL * lateMakeupGainLinear * activeParams.lateLevel;
            float lateMixR = fdnOutR * lateMakeupGainLinear * activeParams.lateLevel;

            // AcousticMetrics に Wet 信号を入力（モノミックス）
            float wetMono = (lateMixL + lateMixR) * 0.5f;
            acousticMetrics.processSample(wetMono);

            outL[n] = (erMixL + lateMixL) * wetGain;
            outR[n] = (erMixR + lateMixR) * wetGain;
        }
    }
} // namespace FDNReverb