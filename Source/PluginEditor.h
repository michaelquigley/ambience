#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "GUI/AmbienceUI.h"
#include "GUI/DecayCurveViz.h"

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

    // ─── GUI Components ───────────────────────────────────────────────────

    AlgorithmSelector algoSelector;

    ArcKnob kPreDelay, kRoomSize, kDecay;
    ArcKnob kHFDamp, kLFAbsorb;
    ArcKnob kDiffusion, kModAmt, kModRate;
    ArcKnob kStereoW, kCrossFeed;
    ArcKnob kERLevel, kSaturation;
    ArcKnob kWet, kDry;
    ArcKnob kDuckAmt, kDuckThr, kDuckAtt, kDuckRel;

    // ────────────────────────────────────────────────────────────────────────
    //  ★ 削除: oversamplingCombo, oversamplingLabel, osAttachment
    //    （v1.1.0 で Oversampling パラメータを廃止）
    // ────────────────────────────────────────────────────────────────────────
    // juce::ComboBox oversamplingCombo;
    // juce::Label oversamplingLabel;
    // std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> osAttachment;

    RT60Visualizer rt60Viz;
    VUMeter vuIn, vuOut;
    juce::Label titleLabel;

    // ─── AcousticMetrics 表示用ラベル ───
    juce::Label labelMetricsTitle;  // セクションタイトル "ACOUSTICS"
    juce::Label labelD50Caption, labelD50Value;
    juce::Label labelC50Caption, labelC50Value;
    juce::Label labelC80Caption, labelC80Value;
    juce::Label labelEDTCaption, labelEDTValue;

    // Layout constants
    static constexpr int W = 900, H = 540, PAD = 8;
    static constexpr int KNOB_W = 64, KNOB_H = 72, KNOB_LBL_H = 14, UNIT_H = 88;

    DecayCurveViz decayCurveViz;

    // ────────────────────────────────────────────────────────────────────────
    //  ★ 将来計画 (Phase 4): ProMode の SatType セレクタ
    //    Phase 2 完了後に下記を追加予定。
    //    - juce::ComboBox satTypeCombo;
    //    - std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> satTypeAttachment;
    //    - ProMode 専用の setVisible(isProMode) ロジック
    // ────────────────────────────────────────────────────────────────────────

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FDNReverbEditor)
};