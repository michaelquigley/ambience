#pragma once
#include <JuceHeader.h>

namespace FDNReverb {

    // パラメータIDの一元管理
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
        inline const juce::String WetLevel = "wetlevel";
        inline const juce::String DryLevel = "drylevel";
        inline const juce::String DuckAmount = "duckamount";
        inline const juce::String DuckAttack = "duckattack";
        inline const juce::String DuckRelease = "duckrelease";
        inline const juce::String DuckThresh = "duckthresh";
        inline const juce::String Oversampling = "oversampling";
    }

    // DSPエンジンが受け取る純粋なデータ構造
    struct DSPParams {
        int   algorithmIndex{ 0 };
        float decayScale{ 1.0f };
        float roomSizeScale{ 1.0f };
        float hfDamping{ 0.5f };
        float lfAbsorption{ 0.5f };
        float diffusion{ 0.70f };
        float preDelayMs{ 10.f };
        float modAmount{ 0.25f };
        float modRate{ 0.5f };
        float stereoWidth{ 0.80f };
        float crossFeed{ 0.15f };
        float erLevel{ 0.6f };
        float lateLevel{ 1.0f };
        float wetDB{ -6.f };
        float dryDB{ 0.f };
        float saturation{ 0.0f };
        float duckingAmount{ 0.0f };
        float duckingAttackMs{ 10.f };
        float duckingRelMs{ 200.f };
        float duckingThreshDB{ -20.f };
    };

    class ParameterHelper {
    public:
        static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    };

} // namespace FDNReverb