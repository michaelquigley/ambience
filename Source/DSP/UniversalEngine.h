#pragma once
#include "DelayMemory.h"
#include "BiquadFilters.h"
#include "MagnitudeResponseFitter.h"
#include "AcousticMetrics.h"
#include "Saturator.h"
#include "OutputLimiter.h"
#include "../PluginParameters.h"
#include <array>
#include <cmath>

#define AMBIENCE_USE_STAGE2_ABSORPTION 1

namespace FDNReverb {

    enum class ReverbTopology { Room, Hall, Plate, Spring, Goldfoil };

    // ─────────────────────────────────────────────────────────────────────────────
    //  BandlimitedNoiseLFO: 白色ノイズ + 1次 IIR LPF によるモジュレーション
    // ─────────────────────────────────────────────────────────────────────────────
    //   旧 RandomWalkLFO との根本的な違い:
    //     旧: 「sampleRate/rateHz サンプルごとに新しい目標値へ補間」
    //         → 切り替わりが周期的に知覚される (ユーザー報告の「気持ち悪い揺れ」の原因)
    //     新: 「毎サンプルに白色ノイズを生成し、1次 LPF で帯域制限」
    //         → 完全に非周期的なドリフト。自然なアナログ揺れ。
    //
    //   設計根拠:
    //     - XOR shift PRNG: 非常に軽量、かつオーディオ用途で十分なランダム性
    //     - 1次 IIR LPF: y[n] = y[n-1] + coeff * (noise[n] - y[n-1])
    //       coeff = 1 - exp(-2π * fc / fs) (fc = rateHz * rateMultiplier)
    //     - LPF カットオフ = modRate: 低 modRate → 緩やかなドリフト、高 modRate → 速い揺れ
    //     - rateMultiplier: 黄金比数列で各チャンネルに固有の係数を付与
    //       → 16ch が絶対に同期しないことを数学的に保証
    //
    //   CPU コスト:
    //     - exp() の計算は processBlock() でブロック単位に 1 回 (per-sample ではない)
    //     - tick() は XOR shift 3 命令 + 加減乗算 2 命令のみ
    // ─────────────────────────────────────────────────────────────────────────────
    struct BandlimitedNoiseLFO {
        uint32_t state{ 12345u };  // XOR shift PRNG 状態 (チャンネルごとに異なる seed)
        float    smoothed{ 0.0f };    // 1次 IIR LPF 状態 (= 現在の出力値)
        float    rateMultiplier{ 1.0f };    // チャンネル固有のレート係数 (初期化時に設定)

        // ─── XOR shift PRNG: 均一分布 [-1.0, +1.0] ───
        // Marsaglia (2003) の xorshift32 アルゴリズム
        // 周期 2^32 - 1、オーディオ用途には十分
        inline float nextNoise() noexcept {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            // uint32_t → float [-1, 1] への変換
            return static_cast<float>(state) * 2.3283064365386963e-10f * 2.0f - 1.0f;
        }

        // ─── 1 次 IIR LPF: 帯域制限ノイズを生成 ───
        //   lpfCoeff: processBlock() でブロック単位に事前計算された係数
        //   (= 1 - exp(-2π * fc / fs)) を渡す。per-sample での exp() 計算を回避。
        inline float tick(float lpfCoeff) noexcept {
            smoothed += (nextNoise() - smoothed) * lpfCoeff;
            return smoothed;
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

        // ── アクセサ ──
        std::array<float, NUM_BANDS> getEffectiveRT60() const noexcept { return effectiveRT60; }
        float getD50() const noexcept { return acousticMetrics.getD50(); }
        float getC50() const noexcept { return acousticMetrics.getC50(); }
        float getC80() const noexcept { return acousticMetrics.getC80(); }
        float getEDT() const noexcept { return theoreticalEDT; }
        const AcousticMetrics& getAcousticMetrics() const noexcept { return acousticMetrics; }

        int   getERTapCount()                       const noexcept { return currentERTapCount; }
        float getERTapDelaySamples(int index)       const noexcept {
            return (index >= 0 && index < currentERTapCount) ? currentERDelaySamples[index] : 0.0f;
        }
        float getERTapGain(int index)               const noexcept {
            return (index >= 0 && index < currentERTapCount) ? currentERGains[index] : 0.0f;
        }
        double getSampleRate()  const noexcept { return fs; }
        bool   isERBypassed()   const noexcept { return bypassER; }

    private:
        void updateTopologyAndRouting();
        void calculatePrimePowerDelays();
        inline void fastWalshHadamardTransform(std::array<float, 16>& v) noexcept;
        inline void applySignFlipping(std::array<float, 16>& v) noexcept;

        // ─────────────────────────────────────────────────────────────────────────
        //  FDN ループ内マイクロサチュレーション (Layer 1, 内部固定)
        // ─────────────────────────────────────────────────────────────────────────
        //   v1.2 変更: 係数を 27/9 から scaled Padé (÷4 スケール) に変更
        //
        //   旧実装 (v1.1) の問題:
        //     f(1.0) = 1*(27+1)/(27+9) = 0.778 → x=1 で 22% 圧縮 → 和音で可聴の飽和
        //
        //   新実装 (v1.2) の設計:
        //     入力を 1/4 にスケールダウン → 同じ Padé を適用 → 4 倍に戻す
        //     f(1.0) ≈ 0.982 → x=1 で約 2% 圧縮 (知覚不能)
        //     f(4.0) ≈ 3.11  → x=4 でようやく圧縮が始まる
        //     x=12 で出力 ±4.0 (Padé の xs=3 でクランプ)
        //     → 通常の FDN 信号レベル (|x| < 2) ではほぼ完全に線形
        //     → Valhalla 流「essentially-linear, 安全装置のみ」を実現
        //
        //   受動性 (Passivity) の保証:
        //     クランプ閾値が高くなったため安全装置としての即応性は下がるが、
        //     OutputLimiter が最終段で保護するため問題なし。
        //     リミットサイクル抑制は「非線形要素の存在」自体で成立し、
        //     強度には大きく依存しない。
        // ─────────────────────────────────────────────────────────────────────────
        inline static float processMicroSaturation(float x) noexcept {
            constexpr float kInScale = 0.05f;   // 入力スケールダウン係数
            constexpr float kOutScale = 1.0f / kInScale;  // kInScale から自動導出 (整合性を保証)

            const float xs = x * kInScale;      // スケール後の入力

            // xs ∈ [-3, 3] でクランプ (Padé の有効範囲外は ±kOutScale に固定)
            // この閾値は x = ±12 に相当するため、通常の FDN 動作では到達しない
            if (xs > 3.0f) return  kOutScale;
            if (xs < -3.0f) return -kOutScale;

            const float xsq = xs * xs;
            // Padé 有理多項式 x(27+x²)/(27+9x²)
            return (xs * (27.0f + xsq) / (27.0f + 9.0f * xsq)) * kOutScale;
        }

        // ── 基本パラメータ ──
        DelayMemoryPool memoryPool;
        double          fs{ 48000.0 };
        DSPParams       activeParams;
        ReverbTopology  currentTopology{ ReverbTopology::Room };

        // ── FDN 構成 ──
        static constexpr int FDN_ORDER = 16;

        LinearDelayLine                              erDelay;
        std::array<float, 16>                        erTaps;   // 将来用、現在未使用
        std::array<LinearDelayLine, 4>               inputDiffusers;
        std::array<LinearDelayLine, FDN_ORDER>       fdnDelays;
        std::array<LinearDelayLine, FDN_ORDER>       nestedAllpassDelays;

        // ── ER パターン ──
        int                                          currentERTapCount{ 0 };
        std::array<float, MAX_ER_TAPS>               currentERDelaySamples;
        std::array<float, MAX_ER_TAPS>               currentERGains;

        // ── Phase 3-1 追加 ──
        OutputLimiter outputLimiter;
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

        // ── モジュレーション (BandlimitedNoiseLFO に変更) ──
        std::array<BandlimitedNoiseLFO, FDN_ORDER> lfos;
        std::array<float, FDN_ORDER>               fdnBaseDelaySamples;
        std::array<float, FDN_ORDER>               fbVec;

        // ── トポロジー固有設定 ──
        float apfGain{ 0.618f };
        bool  bypassER{ false };
        bool  bypassInputDiffusers{ true };
        float lateMixScale{ 1.0f };
        float lateMakeupGainLinear{ 1.0f };

        // ── RT60/EDT データ ──
        std::array<float, NUM_BANDS> effectiveRT60;
        float theoreticalEDT{ 0.0f };

        // ── AcousticMetrics ──
        AcousticMetrics acousticMetrics;

        // ── Saturator (L/R 独立) ──
        Saturator saturatorL;
        Saturator saturatorR;
    };

} // namespace FDNReverb