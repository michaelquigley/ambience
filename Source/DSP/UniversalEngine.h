#pragma once
#include "DelayMemory.h"
#include "BiquadFilters.h"
#include "MagnitudeResponseFitter.h"
#include "AcousticMetrics.h"
#include "Saturator.h"
#include "../PluginParameters.h"
#include <array>
#include <random>
#include "OutputLimiter.h"

#define AMBIENCE_USE_STAGE2_ABSORPTION 1

namespace FDNReverb {

    enum class ReverbTopology { Room, Hall, Plate, Spring, Goldfoil };

    // ─────────────────────────────────────────────────────────────────────────────
    //  RandomWalkLFO: 非相関ノイズベースのモジュレーション
    // ─────────────────────────────────────────────────────────────────────────────
    struct RandomWalkLFO {
        float value{ 0.0f };
        float target{ 0.0f };
        float coeff{ 0.005f };
        int stepsRemaining{ 0 };
        uint32_t state{ 12345 };

        inline float nextRandom() noexcept {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return (state * 2.3283064365386963e-10f) * 2.0f - 1.0f;
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
    //  UniversalEngine: 16ch FWHT FDN リバーブのメインエンジン
    // ─────────────────────────────────────────────────────────────────────────────
    class UniversalEngine {
    public:
        UniversalEngine();

        void prepare(double sampleRate, int maxBlockSize);
        void reset();
        void setParams(const DSPParams& p);
        void processBlock(const float* inL, const float* inR,
            float* outL, float* outR, int numSamples) noexcept;

        // ── 既存のアクセサ ──
        std::array<float, NUM_BANDS> getEffectiveRT60() const noexcept { return effectiveRT60; }

        // ── AcousticMetrics アクセサ ──
        float getD50() const noexcept { return acousticMetrics.getD50(); }
        float getC50() const noexcept { return acousticMetrics.getC50(); }
        float getC80() const noexcept { return acousticMetrics.getC80(); }
        float getEDT() const noexcept { return theoreticalEDT; }
        const AcousticMetrics& getAcousticMetrics() const noexcept { return acousticMetrics; }

        // ── ER タップデータアクセサ (GUI 用) ──
        int getERTapCount() const noexcept { return currentERTapCount; }
        float getERTapDelaySamples(int index) const noexcept {
            return (index >= 0 && index < currentERTapCount) ? currentERDelaySamples[index] : 0.0f;
        }
        float getERTapGain(int index) const noexcept {
            return (index >= 0 && index < currentERTapCount) ? currentERGains[index] : 0.0f;
        }
        double getSampleRate() const noexcept { return fs; }
        bool isERBypassed() const noexcept { return bypassER; }

    private:
        void updateTopologyAndRouting();
        void calculatePrimePowerDelays();
        inline void fastWalshHadamardTransform(std::array<float, 16>& v) noexcept;
        inline void applySignFlipping(std::array<float, 16>& v) noexcept;

        // ─── FDN ループ内マイクロサチュレーション (Layer 1, 内部固定) ───
//   Padé 有理多項式 x(27+x²)/(27+9x²)
//   - クランプ x ∈ [-3, 3]
//   - パッシブ安定性の保証 (リミットサイクル消滅)
//   - HF Damping が後続でエイリアスを除去するため OS 不要
        inline static float processMicroSaturation(float x) noexcept {
            if (x > 3.0f) return  1.0f;
            if (x < -3.0f) return -1.0f;
            const float xsq = x * x;
            return x * (27.0f + xsq) / (27.0f + 9.0f * xsq);
        }

        // ── 基本パラメータ ──
        DelayMemoryPool memoryPool;
        double fs{ 48000.0 };
        DSPParams activeParams;
        ReverbTopology currentTopology{ ReverbTopology::Room };

        // ── FDN 構成 ──
        static constexpr int FDN_ORDER = 16;
        LinearDelayLine erDelay;
        std::array<float, 16> erTaps;  // (将来用、現在未使用)
        std::array<LinearDelayLine, 4> inputDiffusers;
        std::array<LinearDelayLine, FDN_ORDER> fdnDelays;
        std::array<LinearDelayLine, FDN_ORDER> nestedAllpassDelays;

        // ── ER パターン (プリセット選択時に更新) ──
        int currentERTapCount{ 0 };
        std::array<float, MAX_ER_TAPS> currentERDelaySamples;
        std::array<float, MAX_ER_TAPS> currentERGains;

        // ─── Phase 3-1 新規追加 ───
        OutputLimiter outputLimiter;                // 出力段リミッター

        // Ducking 用エンベロープフォロワー
        float duckingEnvelope{ 0.0f };
        float duckingAttackCoeff{ 0.0f };
        float duckingReleaseCoeff{ 0.0f };

        // ── 吸収フィルタ ──
#if AMBIENCE_USE_STAGE2_ABSORPTION
        std::array<std::array<BiquadState, ABSO_STAGES_S2>, FDN_ORDER> absorptionFiltersS2;
        std::array<std::array<BiquadCoeffs, ABSO_STAGES_S2>, FDN_ORDER> currentAbsorptionCoeffsS2;
#else
        std::array<BiquadState, FDN_ORDER> absorptionFilters;
        std::array<BiquadCoeffs, FDN_ORDER> currentAbsorptionCoeffs;
#endif

        // ── モジュレーション ──
        std::array<RandomWalkLFO, FDN_ORDER> lfos;
        std::array<float, FDN_ORDER> fdnBaseDelaySamples;
        std::array<float, FDN_ORDER> fbVec;

        // ── トポロジー固有設定 ──
        float apfGain{ 0.618f };
        bool bypassER{ false };
        bool bypassInputDiffusers{ true };
        float lateMixScale{ 1.0f };
        float lateMakeupGainLinear{ 1.0f };

        // ── RT60/EDT データ ──
        std::array<float, NUM_BANDS> effectiveRT60;
        float theoreticalEDT{ 0.0f };

        // ── AcousticMetrics ──
        AcousticMetrics acousticMetrics;

        // ── Saturator (L/R 独立、内蔵 OS 対応) ──
        Saturator saturatorL;
        Saturator saturatorR;
    };

}  // namespace FDNReverb