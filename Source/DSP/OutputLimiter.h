#pragma once

#include <cmath>
#include <algorithm>

namespace FDNReverb {

    // ─────────────────────────────────────────────────────────────────────────────
    //  OutputLimiter: 出力段の安全装置（ブリックウォール・ピークリミッター）
    // ─────────────────────────────────────────────────────────────────────────────
    //   設計方針:
    //     - パラメータなし: ユーザーが操作するものではなく、純粋な安全装置
    //     - Threshold = -0.5 dBFS (≈ 0.944 リニア): DAW でクリップする手前で抑制
    //     - Look-ahead なし: プラグインのレイテンシを増やさない
    //     - Attack: 0.5ms（突発ピークに即応）
    //     - Release: 50ms（自然な余韻・ポンピング防止）
    //
    //   リアルタイム安全性:
    //     - メモリアロケーション: prepare() で 1 回のみ、processBlock 内ではゼロ
    //     - ブランチ: targetGain の比較 1 回のみ、SIMD 化容易
    //     - 浮動小数点演算: 加減算と除算のみ、超越関数なし
    //
    //   配置: UniversalEngine の processBlock() の最終出力段
    //   （Dry/Wet ミックス後、ステレオ出力直前）
    // ─────────────────────────────────────────────────────────────────────────────
    class OutputLimiter {
    public:
        OutputLimiter() = default;

        // ─── サンプルレート依存の係数を計算 ───
        void prepare(double sampleRate) noexcept {
            fs = sampleRate;
            // 1次ローパスフィルタ係数: y[n] = y[n-1] + coeff * (x[n] - y[n-1])
            // coeff = 1 - exp(-T / τ) where T = 1/fs, τ = time constant
            attackCoeff = 1.0f - std::exp(-1.0f / (static_cast<float>(fs) * 0.0005f)); // 0.5ms
            releaseCoeff = 1.0f - std::exp(-1.0f / (static_cast<float>(fs) * 0.050f));  // 50ms
            reset();
        }

        void reset() noexcept {
            currentGain = 1.0f;
        }

        // ─── サンプル単位の処理 (オーディオスレッドから呼ぶ) ───
        inline void process(float& l, float& r) noexcept {
            // 真のピーク検出（L/R 絶対値の最大値）
            const float absL = std::abs(l);
            const float absR = std::abs(r);
            const float peak = std::max(absL, absR);

            // Threshold: -0.5 dBFS ≈ 0.944
            // ここを超えた信号に対して目標ゲインを算出
            constexpr float threshold = 0.944f;

            // 目標ゲイン:
            //   peak <= threshold なら 1.0 (リミット不要)
            //   peak >  threshold なら threshold/peak (信号を threshold に圧縮)
            const float targetGain = (peak > threshold) ? (threshold / peak) : 1.0f;

            // Attack/Release エンベロープ追従
            //   ゲイン減少時 (targetGain < currentGain): 高速 attack
            //   ゲイン復帰時 (targetGain > currentGain): 緩慢 release
            // これにより、突発ピークには即応するが、ポンピングは抑制される
            const float coeff = (targetGain < currentGain) ? attackCoeff : releaseCoeff;
            currentGain += (targetGain - currentGain) * coeff;

            // ゲイン適用 (L/R 同じゲインを使用 → ステレオイメージ保持)
            l *= currentGain;
            r *= currentGain;
        }

    private:
        double fs{ 44100.0 };
        float  attackCoeff{ 0.0f };
        float  releaseCoeff{ 0.0f };
        float  currentGain{ 1.0f };
    };

} // namespace FDNReverb