#pragma once
#include "DelayMemory.h"
#include "BiquadFilters.h"
#include "MagnitudeResponseFitter.h"
#include "../PluginParameters.h"
#include <array>
#include <random>

// ─────────────────────────────────────────────────────────────────────────────
//  コンパイル時スイッチ: Stage 2 吸収フィルタ使用フラグ
// ─────────────────────────────────────────────────────────────────────────────
//  1: Stage 2c (Välimäki–Liski 累積バイカッドGEQ, 10段カスケード) を使用
//  0: Stage 1 (Jot 直交化1次, 1段) にフォールバック
//
//  ロールバック用のセーフティスイッチ。
// ─────────────────────────────────────────────────────────────────────────────
#define AMBIENCE_USE_STAGE2_ABSORPTION 1

namespace FDNReverb {
    enum class ReverbTopology { Room, Hall, Plate, Spring, Goldfoil };
    // ─────────────────────────────────────────────────────────────────────────────
    // Random Walk LFO (非相関ノイズベースのモジュレーション)
    // ─────────────────────────────────────────────────────────────────────────────
    struct RandomWalkLFO {
        float value{ 0.0f };
        float target{ 0.0f };
        float coeff{ 0.005f };
        int stepsRemaining{ 0 };
        uint32_t state{ 12345 }; // 簡易Xorshift用シード
        inline float nextRandom() noexcept {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return (state * 2.3283064365386963e-10f) * 2.0f - 1.0f; // -1.0 to 1.0
        }
        inline float tick(float rateHz, float sampleRate) noexcept {
            if (stepsRemaining <= 0) {
                target = nextRandom();
                stepsRemaining = static_cast<int>(sampleRate / std::max(0.1f, rateHz));
                coeff = 1.0f - std::exp(-2.0f * 3.14159265f * rateHz / sampleRate);
            }
            stepsRemaining--;
            value += (target - value) * coeff;
            return value;
        }
    };
    // ─────────────────────────────────────────────────────────────────────────────
    // ユニバーサルFDNエンジン (Super Structure)
    // ─────────────────────────────────────────────────────────────────────────────
    class UniversalEngine {
    public:
        UniversalEngine();
        void prepare(double sampleRate, int maxBlockSize);
        void reset();
        void setParams(const DSPParams& p);
        void processBlock(const float* inL, const float* inR, float* outL, float* outR, int numSamples) noexcept;
        std::array<float, NUM_BANDS> getEffectiveRT60() const noexcept { return effectiveRT60; }
    private:
        void updateTopologyAndRouting();
        void calculatePrimePowerDelays();
        inline void fastWalshHadamardTransform(std::array<float, 16>& v) noexcept;
        inline void applySignFlipping(std::array<float, 16>& v) noexcept;
        // ── メモリとコア ──
        DelayMemoryPool memoryPool;
        double fs{ 48000.0 };
        DSPParams activeParams;
        ReverbTopology currentTopology{ ReverbTopology::Room };
        // ── モジュール群 ──
        static constexpr int FDN_ORDER = 16;
        LinearDelayLine erDelay; // ER用タップディレイライン
        std::array<float, 16> erTaps; // ERのタップ位置(ms)
        std::array<LinearDelayLine, 4> inputDiffusers; // Plate/Goldfoil用
        std::array<LinearDelayLine, FDN_ORDER> fdnDelays;
        std::array<LinearDelayLine, FDN_ORDER> nestedAllpassDelays;

#if AMBIENCE_USE_STAGE2_ABSORPTION
        // Stage 2c: 10段カスケード (GEQ のみ、midGain は band 0 に吸収)
        std::array<std::array<BiquadState, ABSO_STAGES_S2>, FDN_ORDER> absorptionFiltersS2;
        std::array<std::array<BiquadCoeffs, ABSO_STAGES_S2>, FDN_ORDER> currentAbsorptionCoeffsS2;
#else
        // Stage 1: 1段のみ
        std::array<BiquadState, FDN_ORDER> absorptionFilters;
        std::array<BiquadCoeffs, FDN_ORDER> currentAbsorptionCoeffs;
#endif

        std::array<RandomWalkLFO, FDN_ORDER> lfos;
        std::array<float, FDN_ORDER> fdnBaseDelaySamples;
        std::array<float, FDN_ORDER> fbVec; // フィードバック・ベクトル
        // トポロジー固有の内部パラメータ
        float apfGain{ 0.618f };
        bool bypassER{ false };
        bool bypassInputDiffusers{ true };
        float lateMixScale{ 1.0f };
        // ▼ 動的メイクアップゲイン（Late Reverb専用）
        float lateMakeupGainLinear{ 1.0f };
        std::array<float, NUM_BANDS> effectiveRT60;
    };
} // namespace FDNReverb