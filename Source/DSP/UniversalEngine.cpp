#include "UniversalEngine.h"

namespace FDNReverb {

    // ─────────────────────────────────────────────────────────────────────────────
    //  ファイルローカルヘルパー (素数探索)
    // ─────────────────────────────────────────────────────────────────────────────
    //   オーディオスレッドで呼ばれるが、以下の理由でリアルタイム安全:
    //     - ヒープアロケーションなし (すべてスタック変数)
    //     - isPrime: 引数範囲 500〜20000 で sqrt(20000)≈141 回以内のループ
    //     - findNearestUniquePrime: 素数の密度から通常 ±30 以内で発見
    //     - 合計計算量: 16ch × 約 141 × 約 30 ≈ 67,000 整数演算/ブロック
    //       (designStage2 の WLS 行列演算と比べ 1/100 以下の負荷)
    // ─────────────────────────────────────────────────────────────────────────────
    namespace {

        // 素数判定 (試し割り法)
        static bool isMathPrime(int n) noexcept {
            if (n < 2)  return false;
            if (n == 2) return true;
            if (n % 2 == 0) return false;
            for (int i = 3; i * i <= n; i += 2)
                if (n % i == 0) return false;
            return true;
        }

        // target に最も近い、usedPrimes[0..usedCount-1] に含まれない素数を返す
        // 正方向・負方向を交互に探索して最近傍を保証する
        static int findNearestUniquePrime(int target,
            const std::array<int, 16>& usedPrimes,
            int usedCount) noexcept {
            target = std::max(target, 2);

            for (int offset = 0; offset < 100000; ++offset) {
                // 正方向
                int hi = target + offset;
                if (isMathPrime(hi)) {
                    bool used = false;
                    for (int k = 0; k < usedCount; ++k)
                        if (usedPrimes[k] == hi) { used = true; break; }
                    if (!used) return hi;
                }
                // 負方向 (offset > 0 かつ 有効値)
                int lo = target - offset;
                if (offset > 0 && lo >= 2 && isMathPrime(lo)) {
                    bool used = false;
                    for (int k = 0; k < usedCount; ++k)
                        if (usedPrimes[k] == lo) { used = true; break; }
                    if (!used) return lo;
                }
            }
            return target; // フォールバック (実際には到達しない)
        }

    } // anonymous namespace

    // ─────────────────────────────────────────────────────────────────────────────
    //  コンストラクタ
    // ─────────────────────────────────────────────────────────────────────────────
    UniversalEngine::UniversalEngine() {
        fbVec.fill(0.0f);

        // BandlimitedNoiseLFO: 黄金比 Weyl 列による rateMultiplier 初期化
        constexpr float phi = 1.6180339887f;
        for (int i = 0; i < FDN_ORDER; ++i) {
            lfos[i].state = 12345u + static_cast<uint32_t>(i) * 9876u;
            lfos[i].smoothed = 0.0f;
            const float angle = static_cast<float>(i) * phi;
            const float frac = angle - std::floor(angle);
            lfos[i].rateMultiplier = 0.80f + frac * 0.40f;  // [0.80, 1.20]
        }
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  prepare()
    // ─────────────────────────────────────────────────────────────────────────────
    void UniversalEngine::prepare(double sampleRate, int /*maxBlockSize*/) {
        fs = sampleRate;

#if AMBIENCE_USE_STAGE2_ABSORPTION
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

        acousticMetrics.prepare(sampleRate, 2000.0f);

        currentERTapCount = 0;
        currentERDelaySamples.fill(0.0f);
        currentERGains.fill(0.0f);

        outputLimiter.prepare(sampleRate);

        duckingAttackCoeff = 1.0f - std::exp(-1.0f / (static_cast<float>(fs) * 0.010f));
        duckingReleaseCoeff = 1.0f - std::exp(-1.0f / (static_cast<float>(fs) * 0.200f));
        duckingEnvelope = 0.0f;

        reset();
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  reset()
    // ─────────────────────────────────────────────────────────────────────────────
    void UniversalEngine::reset() {
        memoryPool.clear();
        fbVec.fill(0.0f);

#if AMBIENCE_USE_STAGE2_ABSORPTION
        for (auto& lineFilters : absorptionFiltersS2)
            for (auto& f : lineFilters) f.reset();
#else
        for (auto& f : absorptionFilters) f.reset();
#endif

        acousticMetrics.reset();
        saturatorL.reset();
        saturatorR.reset();
        outputLimiter.reset();
        duckingEnvelope = 0.0f;

        for (auto& lfo : lfos) lfo.smoothed = 0.0f;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  setParams()
    // ─────────────────────────────────────────────────────────────────────────────
    void UniversalEngine::setParams(const DSPParams& p) {
        activeParams = p;

        switch (p.algorithmIndex) {
        case 0: case 1: currentTopology = ReverbTopology::Room;     break;
        case 2: case 3: currentTopology = ReverbTopology::Hall;     break;
        case 4:         currentTopology = ReverbTopology::Plate;    break;
        case 5:         currentTopology = ReverbTopology::Spring;   break;
        case 6:         currentTopology = ReverbTopology::Goldfoil; break;
        }

        const float attMs = juce::jmax(0.1f, activeParams.duckingAttackMs);
        const float relMs = juce::jmax(0.1f, activeParams.duckingRelMs);
        duckingAttackCoeff = 1.0f - std::exp(-1.0f / (static_cast<float>(fs) * attMs * 0.001f));
        duckingReleaseCoeff = 1.0f - std::exp(-1.0f / (static_cast<float>(fs) * relMs * 0.001f));

        updateTopologyAndRouting();
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  calculatePrimePowerDelays() — v1.3 全面再設計
    // ─────────────────────────────────────────────────────────────────────────────
    //  旧実装 (Prime Power 法) の問題:
    //    遅延時間を「素数の整数乗 p^m」で表現していたため、
    //    大きな素数 (23, 29, 31...) では p² と p³ の間に巨大な「崖」が存在した。
    //    例: 29² = 841 samples (17.5ms) → 29³ = 24389 samples (508ms)
    //    RoomSize を動かしたとき、特定チャンネルの遅延が数十倍ジャンプし、
    //    FDN のモード密度が崩壊して「疎なエコー列」になっていた。
    //
    //  新実装 (Nearest-Prime 法):
    //    1. roomSizeScale から遅延範囲 [minDelay, maxDelay] をサンプル数で算出
    //    2. 16ch に対数均等分布の目標値を配置 (対数分布が心理音響的に最適)
    //    3. 各目標値に最も近い「未使用の素数」を選択
    //       → すべての遅延が素数 → 任意の2チャンネルが互いに素 (自動保証)
    //       → 崖ジャンプが不可能 (素数は連続して存在するため)
    //    4. 結果はなめらかな対数分布を維持し、RoomSize 全域で密なモードを形成する
    //
    //  遅延範囲の設計値 (48kHz):
    //    RoomSize 0.3 (min): 約 20〜115ms (960〜5520 samples)
    //    RoomSize 1.0 (def): 約 25〜155ms (1200〜7440 samples)
    //    RoomSize 2.0 (max): 約 30〜200ms (1440〜9600 samples)
    // ─────────────────────────────────────────────────────────────────────────────
    void UniversalEngine::calculatePrimePowerDelays() {
        const float fsf = static_cast<float>(fs);

        // ─── 遅延範囲の決定 ───
        // roomSizeScale = RoomSize - 0.5 ∈ [-0.2, 1.5]
        // sizeCoeff を [0.5, 2.0] に正規化: 小さい部屋〜大きい部屋で滑らかにスケール
        const float sizeCoeff = juce::jlimit(0.5f, 2.0f,
            activeParams.roomSizeScale + 1.0f);  // roomSizeScale+1.0 ∈ [0.8, 2.5] → clamp

        // 最小遅延: 20〜30ms (部屋のサイズで変化)
        const float minDelayMs = 15.0f + sizeCoeff * 7.5f;   // [18.75ms, 30ms]
        // 最大遅延: 80〜200ms (部屋のサイズで大きく変化)
        const float maxDelayMs = 50.0f + sizeCoeff * 75.0f;  // [87.5ms, 200ms]

        const int minDelaySamples = std::max(11,
            static_cast<int>(minDelayMs * 0.001f * fsf));
        const int maxDelaySamples =
            static_cast<int>(maxDelayMs * 0.001f * fsf);

        // ─── 対数分布ターゲットの算出と素数への丸め ───
        const float logMin = std::log(static_cast<float>(minDelaySamples));
        const float logMax = std::log(static_cast<float>(maxDelaySamples));

        std::array<int, FDN_ORDER> usedPrimes;
        usedPrimes.fill(0);

        for (int i = 0; i < FDN_ORDER; ++i) {
            // t = [0, 1] の線形補間 → 対数空間で均等分布
            const float t = static_cast<float>(i) / static_cast<float>(FDN_ORDER - 1);
            const float logTarget = logMin + t * (logMax - logMin);
            const int   target = static_cast<int>(std::round(std::exp(logTarget)));

            // target に最も近い未使用の素数を選択
            const int prime = findNearestUniquePrime(target, usedPrimes, i);
            usedPrimes[i] = prime;
            fdnBaseDelaySamples[i] = static_cast<float>(prime);
        }
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  updateTopologyAndRouting()
    // ─────────────────────────────────────────────────────────────────────────────
    void UniversalEngine::updateTopologyAndRouting() {
        calculatePrimePowerDelays();

        auto& preset = *ALL_PRESETS[activeParams.algorithmIndex];

        // RT60 スケーリング (+ ProMode 拡張)
        std::array<float, NUM_BANDS> scaledRT60 = preset.acoustics.rt60;
        for (auto& v : scaledRT60) v *= activeParams.decayScale;

        if (activeParams.proMode) {
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

            for (int b = 0; b < NUM_BANDS; ++b)
                scaledRT60[b] *= activeParams.rtBands[b];
        }

        effectiveRT60 = scaledRT60;

#if AMBIENCE_USE_STAGE2_ABSORPTION
        for (int i = 0; i < FDN_ORDER; ++i) {
            auto s2 = MagnitudeResponseFitter::designStage2(
                static_cast<int>(fdnBaseDelaySamples[i]), fs, scaledRT60,
                activeParams.hfDamping, activeParams.lfAbsorption);
            for (int b = 0; b < NUM_BANDS; ++b)
                currentAbsorptionCoeffsS2[i][b] = s2.geqStages[b];
        }
#else
        for (int i = 0; i < FDN_ORDER; ++i) {
            auto absoStages = FilterDesign::designAbsorption(
                static_cast<int>(fdnBaseDelaySamples[i]), fs, scaledRT60,
                activeParams.hfDamping, activeParams.lfAbsorption);
            currentAbsorptionCoeffs[i] = absoStages[0];
        }
#endif

        // AGC
        float rt60Mid = std::max(0.1f, scaledRT60[4]);
        constexpr float baseDB = 28.7f;
        float decayCompDB = 7.0f * std::log10(rt60Mid);

        static constexpr std::array<float, 7> algorithmOffsetDB = {
            +0.8f, +0.9f, +0.5f, +0.5f, +1.5f, +0.6f, +0.6f
        };
        float algoOffset = algorithmOffsetDB[juce::jlimit(0, 6, activeParams.algorithmIndex)];

        // トポロジー結線
        switch (currentTopology) {
        case ReverbTopology::Room:
            bypassER = false; bypassInputDiffusers = true;  apfGain = 0.3f;   break;
        case ReverbTopology::Hall:
            bypassER = false; bypassInputDiffusers = false; apfGain = 0.618f; break;
        case ReverbTopology::Plate:
            bypassER = true;  bypassInputDiffusers = false; apfGain = 0.7f;   break;
        case ReverbTopology::Spring:
            bypassER = true;  bypassInputDiffusers = false; apfGain = 0.5f;   break;
        case ReverbTopology::Goldfoil:
            bypassER = true;  bypassInputDiffusers = false; apfGain = 0.75f;  break;
        }

        // ER パターン更新
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

        // EDT 理論計算
        float edtCoeff = 0.7f;
        switch (currentTopology) {
        case ReverbTopology::Room:     edtCoeff = 0.70f; break;
        case ReverbTopology::Hall:     edtCoeff = 0.95f; break;
        case ReverbTopology::Plate:    edtCoeff = 0.60f; break;
        case ReverbTopology::Spring:   edtCoeff = 0.50f; break;
        case ReverbTopology::Goldfoil: edtCoeff = 0.85f; break;
        }
        theoreticalEDT = rt60Mid * edtCoeff;

        // Saturator 設定 (v1.2: アルゴリズム別倍率を圧縮)
        float satMultiplier = 1.0f;
        switch (currentTopology) {
        case ReverbTopology::Room:     satMultiplier = 0.90f; break;
        case ReverbTopology::Hall:     satMultiplier = 0.93f; break;
        case ReverbTopology::Plate:    satMultiplier = 1.00f; break;
        case ReverbTopology::Spring:   satMultiplier = 1.05f; break;
        case ReverbTopology::Goldfoil: satMultiplier = 1.02f; break;
        }

        float effectiveSatAmount = juce::jlimit(0.0f, 1.0f,
            activeParams.saturation * satMultiplier);

        saturatorL.setAmount(effectiveSatAmount);
        saturatorR.setAmount(effectiveSatAmount);
        saturatorL.setMode(activeParams.satTypeIdx);
        saturatorR.setMode(activeParams.satTypeIdx);

        float totalLateMakeupDB = baseDB + decayCompDB + algoOffset;
        lateMakeupGainLinear = juce::Decibels::decibelsToGain(totalLateMakeupDB);
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  FWHT & Sign Flipping
    // ─────────────────────────────────────────────────────────────────────────────
    inline void UniversalEngine::fastWalshHadamardTransform(
        std::array<float, 16>& v) noexcept
    {
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

    inline void UniversalEngine::applySignFlipping(
        std::array<float, 16>& v) noexcept
    {
        static constexpr std::array<float, 16> flip = {
             1.f, -1.f,  1.f, -1.f, -1.f,  1.f, -1.f,  1.f,
             1.f,  1.f, -1.f, -1.f, -1.f, -1.f,  1.f,  1.f
        };
        for (int i = 0; i < 16; ++i) v[i] *= flip[i];
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  processBlock()
    // ─────────────────────────────────────────────────────────────────────────────
    void UniversalEngine::processBlock(const float* inL, const float* inR,
        float* outL, float* outR,
        int numSamples) noexcept
    {
        // ── ブロック単位の事前計算 ──
        const float depthSamples = activeParams.modAmount * 0.002f * static_cast<float>(fs);
        const float wetGain = juce::Decibels::decibelsToGain(activeParams.wetDB);
        const float stereoWidth = activeParams.stereoWidth;
        const float crossFeedAmt = activeParams.crossFeed;
        const float erLevel = activeParams.erLevel;
        const float lateLevel = activeParams.lateLevel;
        const bool  erSolo = activeParams.erSolo;
        const float duckThreshLin = juce::Decibels::decibelsToGain(activeParams.duckingThreshDB);
        const float duckAmountDB = activeParams.duckingAmount;

        // ── LFO 係数のブロック単位事前計算 (exp を per-sample から排除) ──
        std::array<float, FDN_ORDER> lfoCoeffs;
        {
            const float fsf = static_cast<float>(fs);
            constexpr float twoPi = 6.28318530718f;
            for (int i = 0; i < FDN_ORDER; ++i) {
                const float fc = activeParams.modRate * lfos[i].rateMultiplier;
                lfoCoeffs[i] = juce::jlimit(0.0001f, 0.9999f,
                    1.0f - std::exp(-twoPi * fc / fsf));
            }
        }

        // ── per-sample ループ ──
        for (int n = 0; n < numSamples; ++n) {
            const float leftIn = inL[n];
            const float rightIn = inR[n];
            const float midIn = (leftIn + rightIn) * 0.5f;
            const float sideIn = (leftIn - rightIn) * 0.5f;
            float erOutL = 0.0f, erOutR = 0.0f;

            // ── Ducking エンベロープ検出 ──
            const float inputPeak = juce::jmax(std::abs(leftIn), std::abs(rightIn));
            const float envCoeff = (inputPeak > duckingEnvelope)
                ? duckingAttackCoeff : duckingReleaseCoeff;
            duckingEnvelope += (inputPeak - duckingEnvelope) * envCoeff;

            float duckGainLinear = 1.0f;
            if (duckAmountDB > 0.001f && duckingEnvelope > duckThreshLin) {
                const float envDB = 20.0f * std::log10(
                    juce::jmax(duckingEnvelope, 1e-6f));
                const float overDB = envDB - activeParams.duckingThreshDB;
                const float gainRedDB = -juce::jmin(overDB, duckAmountDB);
                duckGainLinear = juce::Decibels::decibelsToGain(gainRedDB);
            }

            // ── 1. Input Diffusers ──
            float fdnInputMid = midIn;
            if (!bypassInputDiffusers) {
                for (int i = 0; i < 4; ++i) {
                    float delaySmp = (3.0f + i * 2.0f) * 0.001f * static_cast<float>(fs);
                    float d = inputDiffusers[i].read(delaySmp);
                    float w = fdnInputMid + 0.618f * d;
                    inputDiffusers[i].write(w);
                    fdnInputMid = d - 0.618f * w;
                }
            }

            // ── 2. ER Tapped Delay ──
            if (!bypassER) {
                erDelay.write(midIn);
                float erTotalL = 0.0f, erTotalR = 0.0f;
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

            // ── 3. FDN + Nested Allpass (16ch) ──
            std::array<float, 16> currentFb = fbVec;
            fastWalshHadamardTransform(currentFb);
            applySignFlipping(currentFb);

            float fdnOutL = 0.0f, fdnOutR = 0.0f;
            std::array<float, 16> nextFb;

            for (int i = 0; i < FDN_ORDER; ++i) {
                // BandlimitedNoiseLFO
                const float lfoVal = lfos[i].tick(lfoCoeffs[i]);
                const float delaySmp = fdnBaseDelaySamples[i] + lfoVal * depthSamples;
                float d = fdnDelays[i].read(delaySmp);

                // 吸収フィルタ
#if AMBIENCE_USE_STAGE2_ABSORPTION
                for (int s = 0; s < ABSO_STAGES_S2; ++s)
                    d = absorptionFiltersS2[i][s].tick(d, currentAbsorptionCoeffsS2[i][s]);
#else
                d = absorptionFilters[i].tick(d, currentAbsorptionCoeffs[i]);
#endif

                // FDN ループ内マイクロサチュレーション (Layer 1, v1.2)
                d = processMicroSaturation(d);

                // Nested Allpass
                const float apfDelaySmp = (1.5f + i * 0.3f) * 0.001f
                    * static_cast<float>(fs);
                float apfD = nestedAllpassDelays[i].read(apfDelaySmp);
                float apfW = d + apfGain * apfD;
                nestedAllpassDelays[i].write(apfW);
                float apfOut = apfD - apfGain * apfW;

                nextFb[i] = apfOut;

                const float sideForCh = (i % 2 == 0 ? +sideIn : -sideIn)
                    * stereoWidth;
                const float fdnInputForThisCh = (fdnInputMid + sideForCh) * 0.25f;
                fdnDelays[i].write(fdnInputForThisCh + currentFb[i]);

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

            // ── CrossFeed ──
            const float xfeed = crossFeedAmt * 0.5f;
            const float origL = fdnOutL;
            const float origR = fdnOutR;
            fdnOutL = origL * (1.0f - xfeed) + origR * xfeed;
            fdnOutR = origR * (1.0f - xfeed) + origL * xfeed;

            // ── 4. ミックス ──
            const float erMixL = bypassER ? 0.0f : erOutL * erLevel;
            const float erMixR = bypassER ? 0.0f : erOutR * erLevel;
            const float lateMixL = fdnOutL * lateMakeupGainLinear * lateLevel;
            const float lateMixR = fdnOutR * lateMakeupGainLinear * lateLevel;

            acousticMetrics.processSample((lateMixL + lateMixR) * 0.5f);

            // ── 5. Saturation (Layer 2) ──
            float satL = saturatorL.processSample(lateMixL);
            float satR = saturatorR.processSample(lateMixR);

            // ── ER SOLO ──
            if (erSolo) { satL = 0.0f; satR = 0.0f; }

            // ── 6. 最終出力 ──
            const float finalWetGain = wetGain * duckGainLinear;
            outL[n] = (erMixL + satL) * finalWetGain;
            outR[n] = (erMixR + satR) * finalWetGain;

            outputLimiter.process(outL[n], outR[n]);
        }
    }

} // namespace FDNReverb