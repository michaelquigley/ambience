#pragma once

#include <JuceHeader.h>

namespace FDNReverb {

    // ─────────────────────────────────────────────────────────────────────────────
    //  ParamID: AudioProcessorValueTreeState で使うパラメータ ID の一元管理
    // ─────────────────────────────────────────────────────────────────────────────
    //   ID 文字列を変更すると、ユーザーの既存セッションがロードできなくなる
    //   ため、原則として ID 文字列の変更・削除は慎重に行うこと。
    //
    //   v1.0.0 → v1.1.0 の変更:
    //     - Oversampling ("oversampling") を廃止
    //       FDN ループ内 HF Damping が反復的にエイリアスを除去するため、
    //       Wet 出力の OS は計算量に見合う効果がないと判断。
    //     - SatType ("sattype") を新規追加
    //       ProMode 専用。Saturation の音色キャラクター 4 種を選択可能に。
    // ─────────────────────────────────────────────────────────────────────────────
    namespace ParamID {
        inline const juce::String Algorithm = "algorithm";
        inline const juce::String PreDelay = "predelay";
        inline const juce::String RoomSize = "roomsize";
        inline const juce::String DecayTime = "decaytime";
        inline const juce::String HFDamping = "hfdamping";
        inline const juce::String LFAbsorption = "lfabsorption";
        inline const juce::String Diffusion = "diffusion";
        inline const juce::String ModAmount = "modamount";
        inline const juce::String ModRate = "modrate";
        inline const juce::String StereoWidth = "stereowidth";
        inline const juce::String CrossFeed = "crossfeed";
        inline const juce::String ERLevel = "erlevel";
        inline const juce::String Saturation = "saturation";
        inline const juce::String SatType = "sattype";       // ★ 新規追加 (ProMode 専用)
        inline const juce::String WetLevel = "wetlevel";
        inline const juce::String DryLevel = "drylevel";
        inline const juce::String DuckAmount = "duckamount";
        inline const juce::String DuckAttack = "duckattack";
        inline const juce::String DuckRelease = "duckrelease";
        inline const juce::String DuckThresh = "duckthresh";
        // ─── 廃止 (v1.1.0) ─────────────────────────────────────────
        // inline const juce::String Oversampling = "oversampling";
        // ───────────────────────────────────────────────────────────
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  DSPParams: DSP エンジン (UniversalEngine) が受け取る純粋なデータ構造
    // ─────────────────────────────────────────────────────────────────────────────
    //   オーディオスレッドから読み込まれるため、ロックフリーで渡せるよう
    //   POD 的なデータのみ保持する。JUCE の AudioParameter* は介さない。
    // ─────────────────────────────────────────────────────────────────────────────
    struct DSPParams {
        int   algorithmIndex{ 0 };
        float decayScale{ 1.0f };
        float roomSizeScale{ 1.0f };
        float hfDamping{ 0.5f };
        float lfAbsorption{ 0.5f };
        float diffusion{ 0.70f };
        float preDelayMs{ 10.0f };
        float modAmount{ 0.25f };
        float modRate{ 0.5f };
        float stereoWidth{ 0.80f };
        float crossFeed{ 0.15f };
        float erLevel{ 0.6f };
        float lateLevel{ 1.0f };
        float wetDB{ -6.0f };
        float dryDB{ 0.0f };
        float saturation{ 0.0f };
        int   satTypeIdx{ 0 };       // ★ 新規追加: 0=Warm / 1=Tape / 2=Tube / 3=Hard
        float duckingAmount{ 0.0f };
        float duckingAttackMs{ 10.0f };
        float duckingRelMs{ 200.0f };
        float duckingThreshDB{ -20.0f };
        // ─── 廃止 (v1.1.0) ─────────────────────────────────────────
        // int oversamplingIdx { 0 };
        // ───────────────────────────────────────────────────────────
    };

    // ─────────────────────────────────────────────────────────────────────────────
    //  ParameterHelper: APVTS の ParameterLayout を構築するファクトリ
    // ─────────────────────────────────────────────────────────────────────────────
    class ParameterHelper {
    public:
        static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    };

} // namespace FDNReverb