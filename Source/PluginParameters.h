#pragma once

#include <JuceHeader.h>
#include <array>

namespace FDNReverb {

    // ─────────────────────────────────────────────────────────────────────────────
    //  ParamID
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
        inline const juce::String SatType = "sattype";
        inline const juce::String WetLevel = "wetlevel";
        inline const juce::String DryLevel = "drylevel";
        inline const juce::String DuckAmount = "duckamount";
        inline const juce::String DuckAttack = "duckattack";
        inline const juce::String DuckRelease = "duckrelease";
        inline const juce::String DuckThresh = "duckthresh";

        // ─── Phase 3-1 追加 ───
        inline const juce::String ERSolo = "ersolo";    // ER ソロ機能

        // ─── Phase 4 追加 (ProMode) ───
        inline const juce::String ProMode = "promode";   // ProMode 切り替え
        inline const juce::String TiltLow = "tiltlow";   // Tilt EQ 低域
        inline const juce::String TiltMid = "tiltmid";   // Tilt EQ 中域
        inline const juce::String TiltHigh = "tilthigh";  // Tilt EQ 高域

        // RT60 帯域別ノブ x10 (31Hz ~ 16kHz)
        inline const juce::String RTBand0 = "rtband0";   // 31.25 Hz
        inline const juce::String RTBand1 = "rtband1";   // 62.5 Hz
        inline const juce::String RTBand2 = "rtband2";   // 125 Hz
        inline const juce::String RTBand3 = "rtband3";   // 250 Hz
        inline const juce::String RTBand4 = "rtband4";   // 500 Hz
        inline const juce::String RTBand5 = "rtband5";   // 1 kHz
        inline const juce::String RTBand6 = "rtband6";   // 2 kHz
        inline const juce::String RTBand7 = "rtband7";   // 4 kHz
        inline const juce::String RTBand8 = "rtband8";   // 8 kHz
        inline const juce::String RTBand9 = "rtband9";   // 16 kHz
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  DSPParams
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
        int   satTypeIdx{ 0 };
        float duckingAmount{ 0.0f };
        float duckingAttackMs{ 10.0f };
        float duckingRelMs{ 200.0f };
        float duckingThreshDB{ -20.0f };

        // ─── Phase 3-1 追加 ───
        bool  erSolo{ false };

        // ─── Phase 4 追加 (ProMode) ───
        bool  proMode{ false };
        float tiltLow{ 1.0f };  // 0.5 ~ 2.0 (1.0 = neutral)
        float tiltMid{ 1.0f };
        float tiltHigh{ 1.0f };
        std::array<float, 10> rtBands{ { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                                         1.0f, 1.0f, 1.0f, 1.0f, 1.0f } };
    };

    class ParameterHelper {
    public:
        static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    };

} // namespace FDNReverb