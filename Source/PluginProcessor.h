#pragma once
// ============================================================
//  PluginProcessor.h
//  Main JUCE AudioProcessor — parameter management,
//  oversampling, dry/wet, state save/restore
// ============================================================
#include <JuceHeader.h>
#include "FDNCore.h"

// ─── Parameter IDs (string literals used with APVTS) ─────────────────────────
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
    inline const juce::String Oversampling = "oversampling";  // 0=1x 1=2x 2=4x 3=8x
}

// ─────────────────────────────────────────────────────────────────────────────
class FDNReverbAudioProcessor : public juce::AudioProcessor
{
public:
    FDNReverbAudioProcessor();
    ~FDNReverbAudioProcessor() override = default;

    //── AudioProcessor overrides ─────────────────────────────────────────────
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Ambience"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 20.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    //── APVTS ────────────────────────────────────────────────────────────────
    juce::AudioProcessorValueTreeState apvts;

    //── Visualizer data (read from GUI thread) ───────────────────────────────
    std::array<float, FDNReverb::NUM_BANDS> getRT60ForDisplay() const noexcept {
        return engine.getEffectiveRT60();
    }

    //── RMS level for VU meters ───────────────────────────────────────────────
    float getInputRMSL()  const noexcept { return inputRMS_L.load(); }
    float getInputRMSR()  const noexcept { return inputRMS_R.load(); }
    float getOutputRMSL() const noexcept { return outputRMS_L.load(); }
    float getOutputRMSR() const noexcept { return outputRMS_R.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void updateEngineParams();

    //── Engine ────────────────────────────────────────────────────────────────
    FDNReverb::FDNReverbEngine engine;

    //── Oversampling (1x/2x/4x/8x) ───────────────────────────────────────────
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    int    currentOSFactor{ 1 };
    int    currentOSIndex{ 0 };

    //── Working buffers (pre-allocated, no alloc in processBlock) ─────────────
    juce::AudioBuffer<float> dryBuffer;
    juce::AudioBuffer<float> wetBuffer;

    //── SmoothedValues for dry/wet ────────────────────────────────────────────
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothWetGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothDryGain;

    //── RMS meters ────────────────────────────────────────────────────────────
    std::atomic<float> inputRMS_L{ 0.f };
    std::atomic<float> inputRMS_R{ 0.f };
    std::atomic<float> outputRMS_L{ 0.f };
    std::atomic<float> outputRMS_R{ 0.f };
    float rmsDecay{ 0.f };   // per-sample decay coefficient

    double lastSampleRate{ 0.0 };
    int    lastBlockSize{ 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FDNReverbAudioProcessor)
};