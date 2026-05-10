#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "GUI/AmbienceUI.h"

class FDNReverbEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit FDNReverbEditor(FDNReverbAudioProcessor&);
    ~FDNReverbEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    FDNReverbAudioProcessor& audioProcessor;
    AmbienceLookAndFeel laf;

    // GUI Components
    AlgorithmSelector algoSelector;
    ArcKnob kPreDelay, kRoomSize, kDecay;
    ArcKnob kHFDamp, kLFAbsorb;
    ArcKnob kDiffusion, kModAmt, kModRate;
    ArcKnob kStereoW, kCrossFeed;
    ArcKnob kERLevel, kSaturation;
    ArcKnob kWet, kDry;
    ArcKnob kDuckAmt, kDuckThr, kDuckAtt, kDuckRel;

    juce::ComboBox oversamplingCombo;
    juce::Label oversamplingLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> osAttachment;

    RT60Visualizer rt60Viz;
    VUMeter vuIn, vuOut;
    juce::Label titleLabel;

    // Layout constants
    static constexpr int W = 900, H = 540, PAD = 8;
    static constexpr int KNOB_W = 64, KNOB_H = 72, KNOB_LBL_H = 14, UNIT_H = 88;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FDNReverbEditor)
};