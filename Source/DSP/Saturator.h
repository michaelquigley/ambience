#pragma once

#include <cmath>
#include <algorithm>

namespace FDNReverb {

    // ─────────────────────────────────────────────────────────────────────────────
    //  SaturationMode: ProMode で選択可能な 4 種のサチュレーション特性
    // ─────────────────────────────────────────────────────────────────────────────
    //   Warm  : Vicanek 関数 x/√(1+x²) + ADAA 1次
    //           対称的・滑らかな soft clip。奇数次倍音中心。FET 系アンプ感。
    //           Ambience の "デフォルト" であり、最も使いやすい味付け。
    //
    //   Tape  : Padé 有理多項式 x(27+x²)/(27+9x²)
    //           ADAA 不要 (高密度な FDN テールに倍音がマスキングされる)。
    //           tanh より軽量。テープレコーダーの圧縮感・暖かみ。
    //
    //   Tube  : 非対称 ADAA (正側ソフト・負側ハード)
    //           偶数次倍音が支配的に発生。真空管プリアンプの "うねり" "太さ"。
    //           Hall・Goldfoil 等のリッチな響きと相性が良い。
    //
    //   Hard  : ハードクリッピング + ADAA
    //           急峻なクリップによる強い歪み。奇数次の高次倍音まで含む。
    //           Spring・Plate の金属的キャラクターと組み合わせると効果的。
    // ─────────────────────────────────────────────────────────────────────────────
    enum class SaturationMode {
        Warm = 0,
        Tape = 1,
        Tube = 2,
        Hard = 3
    };

    // ─────────────────────────────────────────────────────────────────────────────
    //  Saturator: Wet 出力に適用するキャラクターサチュレーター
    // ─────────────────────────────────────────────────────────────────────────────
    //   設計方針:
    //     - リアルタイム安全: processSample 内でメモリアロケーション・ロック禁止。
    //       状態変数は固定サイズの float のみ。
    //     - amount = 0 で完全バイパス (CPU 負荷ゼロ・音質劣化ゼロ)。
    //     - SaturationMode で 4 種類の音色キャラクターを切替。
    //       Warm/Tube/Hard は ADAA 1次でエイリアシングを抑制、
    //       Tape は計算量を優先しネイティブな波形整形のみ。
    //     - モード切替時には reset() を内部呼び出ししてクリックノイズを防止。
    //
    //   なお、本クラスは UniversalEngine の Wet 出力直前 (FDN 後段) に
    //   配置される "Layer 2 サチュレーション" である。FDN フィードバック
    //   ループ内の "Layer 1 マイクロサチュレーション" は別系統で実装する。
    // ─────────────────────────────────────────────────────────────────────────────
    class Saturator {
    public:
        Saturator() = default;

        // ─── 状態リセット (DAW 停止時・サンプルレート変更時に呼ぶ) ───
        void reset() noexcept {
            prevInput = 0.0f;
            // 各モードの F(0) を初期値として設定 (ADAA 初回計算の整合性のため)
            switch (currentMode) {
            case SaturationMode::Warm: prevF = 1.0f; break;  // sqrt(1+0²) = 1
            case SaturationMode::Tape: prevF = 0.0f; break;  // 未使用 (ADAA なし)
            case SaturationMode::Tube: prevF = 1.0f; break;  // F(0) = 1 に揃えた設計
            case SaturationMode::Hard: prevF = 0.0f; break;  // F(0) = 0²/2 = 0
            }
        }

        // ─── モード設定 (ProMode の SatType セレクタから呼ばれる) ───
        void setMode(SaturationMode mode) noexcept {
            if (mode != currentMode) {
                currentMode = mode;
                reset();  // モード変更時は履歴をクリアしてクリック防止
            }
        }

        // int 版オーバーロード (APVTS の AudioParameterChoice インデックスから直接呼べる)
        void setMode(int modeIndex) noexcept {
            setMode(static_cast<SaturationMode>(std::clamp(modeIndex, 0, 3)));
        }

        // ─── ドライブ量設定 (0.0 = バイパス, 1.0 = 最大ドライブ) ───
        void setAmount(float amount) noexcept {
            amount = std::clamp(amount, 0.0f, 1.0f);
            currentAmount = amount;
            // 共通のドライブカーブ: 1.0 → 4.0 を 2 次でマッピング
            // (低 amount では穏やか、高 amount では急峻に倍音が出始める)
            drive = 1.0f + amount * amount * 3.0f;
            wetMix = amount;
            dryMix = 1.0f - amount * 0.5f;  // amount=1 でも dry を 0.5 残し、芯を保つ
        }

        // ─── サンプル単位の処理 (オーディオスレッドから呼ぶ) ───
        inline float processSample(float input) noexcept {
            // バイパスの早期リターン (currentAmount ≈ 0 で完全スルー)
            if (currentAmount < 1e-4f) {
                return input;
            }

            const float dryInput = input;
            const float driven = input * drive;
            float saturated = 0.0f;

            // モード別の処理関数を呼び出し (switch はコンパイラがジャンプテーブル化)
            switch (currentMode) {
            case SaturationMode::Warm: saturated = processWarm(driven); break;
            case SaturationMode::Tape: saturated = processTape(driven); break;
            case SaturationMode::Tube: saturated = processTube(driven); break;
            case SaturationMode::Hard: saturated = processHard(driven); break;
            }

            // ドライブで上昇したゲインを戻す
            saturated /= drive;

            // Dry / Wet ミックス
            return dryInput * dryMix + saturated * wetMix;
        }

    private:
        // ─────────────────────────────────────────────────────────────────────────
        //  Warm: Vicanek 関数 x/√(1+x²) + ADAA 1次
        // ─────────────────────────────────────────────────────────────────────────
        //   原関数:   f(x) = x / sqrt(1 + x²)
        //   原始関数: F(x) = sqrt(1 + x²)
        //   ADAA:    y[n] = (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1])
        //   フォールバック (|Δx| が微小): 中点評価 f((x[n] + x[n-1]) / 2)
        // ─────────────────────────────────────────────────────────────────────────
        inline float processWarm(float x) noexcept {
            const float F_x = std::sqrt(1.0f + x * x);
            const float dx = x - prevInput;
            float y;

            constexpr float kTolerance = 1e-5f;
            if (std::abs(dx) < kTolerance) {
                // 中点評価でゼロ除算を回避 (Δx が微小なときに有効)
                const float xAvg = (x + prevInput) * 0.5f;
                y = xAvg / std::sqrt(1.0f + xAvg * xAvg);
            }
            else {
                y = (F_x - prevF) / dx;
            }

            prevInput = x;
            prevF = F_x;
            return y;
        }

        // ─────────────────────────────────────────────────────────────────────────
        //  Tape: Padé 有理多項式 (ADAA なし、最軽量)
        // ─────────────────────────────────────────────────────────────────────────
        //   関数: f(x) = x · (27 + x²) / (27 + 9x²)
        //   特性:
        //     - x = ±3 で出力 ±1.0 に正確に到達 (自然な saturation 境界)
        //     - x が ±3 を超えたら ±1.0 にクランプ (発散防止)
        //     - tanh より軽量 (除算 1 回のみ、超越関数なし)
        //     - 心理音響的に、Wet 信号は高密度な FDN テールに高調波が
        //       マスキングされるため ADAA なしでも実用上問題なし。
        //       (資料「微小サチュレーションの高度なC++実装」の知見に基づく判断)
        // ─────────────────────────────────────────────────────────────────────────
        inline float processTape(float x) noexcept {
            // クランプ: x ∈ [-3, 3] 外なら ±1.0 を返す
            if (x > 3.0f) { prevInput = x; return  1.0f; }
            if (x < -3.0f) { prevInput = x; return -1.0f; }

            const float xsq = x * x;
            prevInput = x;
            return x * (27.0f + xsq) / (27.0f + 9.0f * xsq);
        }

        // ─────────────────────────────────────────────────────────────────────────
        //  Tube: 非対称サチュレーション + ADAA
        // ─────────────────────────────────────────────────────────────────────────
        //   正側 (x ≥ 0): f(x) = x / sqrt(1 + x²)              (ソフト・Warm と同じ)
        //   負側 (x < 0): f(x) = x / sqrt(1 + (1.5·x)²)        (ハード・圧縮強め)
        //   ⇒ 正負で異なる傾きを持つため、偶数次倍音が発生する
        //     (真空管プリアンプ特有の "うねり" "太さ" を生む)
        //
        //   原始関数 F(x) を C¹ 連続にするため、負側に定数シフトを加算:
        //     F_pos(x) = sqrt(1 + x²)                            ; F_pos(0) = 1
        //     F_neg(x) = (1/1.5²)·sqrt(1 + (1.5x)²) + (1 - 1/1.5²)
        //              = (4/9)·sqrt(1 + 2.25x²) + 5/9            ; F_neg(0) = 1
        //   ⇒ x = 0 で F が連続、かつ微分も連続 (f(0) = 0 from both sides)
        //
        //   サンプル間でゼロクロスが発生した場合は ADAA フォールバック
        //   (直接 f(x) を評価) を用いて数値的な不安定を回避する。
        // ─────────────────────────────────────────────────────────────────────────
        inline float processTube(float x) noexcept {
            constexpr float kNeg = 1.5f;
            constexpr float kNeg2 = kNeg * kNeg;            // = 2.25
            constexpr float invKneg2 = 1.0f / kNeg2;           // = 4/9 ≈ 0.4444
            constexpr float fShift = 1.0f - invKneg2;        // = 5/9 ≈ 0.5556

            // 現在の F(x) を計算
            float F_x;
            if (x >= 0.0f) {
                F_x = std::sqrt(1.0f + x * x);
            }
            else {
                const float kx = kNeg * x;
                F_x = invKneg2 * std::sqrt(1.0f + kx * kx) + fShift;
            }

            const float dx = x - prevInput;
            const bool  signChanged = (x >= 0.0f) != (prevInput >= 0.0f);
            float y;

            constexpr float kTolerance = 1e-5f;
            if (std::abs(dx) < kTolerance || signChanged) {
                // フォールバック: 直接 f(x) を評価 (ゼロクロス時も安全)
                if (x >= 0.0f) {
                    y = x / std::sqrt(1.0f + x * x);
                }
                else {
                    const float kx = kNeg * x;
                    y = x / std::sqrt(1.0f + kx * kx);
                }
            }
            else {
                y = (F_x - prevF) / dx;
            }

            prevInput = x;
            prevF = F_x;
            return y;
        }

        // ─────────────────────────────────────────────────────────────────────────
        //  Hard: ハードクリッピング + ADAA
        // ─────────────────────────────────────────────────────────────────────────
        //   関数:    f(x) = clamp(x, -1, 1)
        //   原始関数 (C¹ 連続になるよう設計):
        //     F(x) = x² / 2        for  |x| ≤ 1
        //     F(x) =  x - 1/2     for   x >  1
        //     F(x) = -x - 1/2     for   x < -1
        //   ⇒ F(±1) = 1/2 で連続、F'(±1) = ±1 (= f(±1)) で微分も連続
        //
        //   ADAA を通すことで急峻なクリップでもエイリアスを大幅抑制できる。
        //   amount が高いと "ファズ" 風の鋭く明るい倍音が前面に出る。
        // ─────────────────────────────────────────────────────────────────────────
        inline float processHard(float x) noexcept {
            float F_x;
            if (x > 1.0f)      F_x = x - 0.5f;
            else if (x < -1.0f) F_x = -x - 0.5f;
            else                F_x = x * x * 0.5f;

            const float dx = x - prevInput;
            float y;

            constexpr float kTolerance = 1e-5f;
            if (std::abs(dx) < kTolerance) {
                // フォールバック: 直接 f(x) を評価
                y = std::clamp(x, -1.0f, 1.0f);
            }
            else {
                y = (F_x - prevF) / dx;
            }

            prevInput = x;
            prevF = F_x;
            return y;
        }

        // ─── 状態変数 (ADAA の履歴、全モード共用) ───
        float prevInput{ 0.0f };
        float prevF{ 1.0f };  // デフォルトモード Warm の F(0) = 1 に合わせて初期化

        // ─── 設定値 ───
        SaturationMode currentMode{ SaturationMode::Warm };
        float currentAmount{ 0.0f };
        float drive{ 1.0f };
        float wetMix{ 0.0f };
        float dryMix{ 1.0f };
    };

} // namespace FDNReverb