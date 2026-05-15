#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "GUI/AmbienceUI.h"
#include "GUI/DecayCurveViz.h"

class FDNReverbEditor : public juce::AudioProcessorEditor,
    private juce::Timer
{
public:
    explicit FDNReverbEditor(FDNReverbAudioProcessor&);
    ~FDNReverbEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void updatePanelVisibility();

    FDNReverbAudioProcessor& audioProcessor;
    AmbienceLookAndFeel laf;

    // ─── 共通 (常時表示) ────────────────────────────────────────────
    AlgorithmSelector algoSelector;
    RT60Visualizer    rt60Viz;
    DecayCurveViz     decayCurveViz;
    VUMeter           vuIn, vuOut;
    juce::Label       titleLabel;

    // AcousticMetrics 表示ラベル
    juce::Label labelMetricsTitle;
    juce::Label labelD50Caption, labelD50Value;
    juce::Label labelC50Caption, labelC50Value;
    juce::Label labelC80Caption, labelC80Value;
    juce::Label labelEDTCaption, labelEDTValue;

    // ─── ProMode / ER SOLO ボタン (常時表示) ────────────────────────
    //   配置: ヘッダー行 (タイトル右隣)
    juce::TextButton proModeButton;   // "PRO" トグルボタン
    juce::TextButton erSoloButton;    // "ER SOLO" トグルボタン (青系)
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
        proModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
        erSoloAttachment;

    // ProMode の状態キャッシュ（timerCallback で更新）
    bool isProMode{ false };

    // ─── Normal Mode ノブ (ProMode 時は非表示) ──────────────────────
    ArcKnob kPreDelay, kRoomSize, kDecay;
    ArcKnob kHFDamp, kLFAbsorb;
    ArcKnob kDiffusion, kModAmt, kModRate;
    ArcKnob kStereoW, kCrossFeed;
    ArcKnob kERLevel, kSaturation;
    ArcKnob kWet, kDry;
    ArcKnob kDuckAmt, kDuckThr, kDuckAtt, kDuckRel;

    // ─── ProMode パネル (Normal Mode 時は非表示) ────────────────────

    // 1段目: RT60 帯域別ノブ × 10 (31Hz〜16kHz)
    std::array<ArcKnob, 10> kRTBands;

    // 2段目: SatType コンボ + Tilt EQ ノブ × 3 (左寄せ)
    juce::Label   satTypeLabel;
    juce::ComboBox satTypeCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        satTypeAttachment;
    ArcKnob kTiltLow, kTiltMid, kTiltHigh;

    // ─── Layout 定数 ────────────────────────────────────────────────
    static constexpr int W = 900;
    static constexpr int H = 540;
    static constexpr int PAD = 8;
    static constexpr int KNOB_W = 64;
    static constexpr int KNOB_H = 72;
    static constexpr int KNOB_LBL_H = 14;
    static constexpr int UNIT_H = 88;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FDNReverbEditor)
};