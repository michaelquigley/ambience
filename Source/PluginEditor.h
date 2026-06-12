#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PresetManager.h"
#include "GUI/AmbienceUI.h"
#include "GUI/DecayCurveViz.h"

// ─────────────────────────────────────────────────────────────────────────────
//  MixLockButton
// ─────────────────────────────────────────────────────────────────────────────
//  Wet/Dry の隣に置く小さな南京錠トグル。ベクター描画なので任意のズーム倍率で
//  クリアに表示される (Assets / BinaryData は不要)。
//    ロック時   : 閉じた南京錠 + Accent カラー
//    アンロック時: シャックルを持ち上げた南京錠 + 減光グレー
// ─────────────────────────────────────────────────────────────────────────────
class MixLockButton : public juce::Button
{
public:
    MixLockButton() : juce::Button("mixlock") {}

    void paintButton(juce::Graphics& g,
                     bool shouldDrawButtonAsHighlighted,
                     bool /*shouldDrawButtonAsDown*/) override
    {
        auto b = getLocalBounds().toFloat().reduced(0.5f);
        const bool locked = getToggleState();
        const auto col = locked
            ? AmbienceColors::Accent
            : AmbienceColors::TextSecondary.withAlpha(shouldDrawButtonAsHighlighted ? 0.9f : 0.55f);

        const float w = b.getWidth();
        const float h = b.getHeight();

        // ── 本体 (ロック部) ──
        const float bodyW = w * 0.74f;
        const float bodyH = h * 0.50f;
        juce::Rectangle<float> body(b.getCentreX() - bodyW * 0.5f,
                                    b.getBottom() - bodyH,
                                    bodyW, bodyH);

        // ── シャックル ──
        const float r  = w * 0.22f;
        const float cx = b.getCentreX();
        // アンロック時はシャックルを少し持ち上げ、右脚を本体から離して「開」を表現
        const float lift   = locked ? 0.0f : h * 0.16f;
        const float arcCY  = body.getY() - h * 0.06f - lift;
        const float legBot = locked ? body.getY() : body.getY() - h * 0.18f;

        juce::Path shackle;
        shackle.addCentredArc(cx, arcCY, r, r, 0.0f,
                              -juce::MathConstants<float>::halfPi,
                               juce::MathConstants<float>::halfPi, true);
        shackle.lineTo(cx + r, legBot);                 // 右脚
        shackle.startNewSubPath(cx - r, arcCY);
        shackle.lineTo(cx - r, body.getY());            // 左脚 (常に本体へ接続)

        g.setColour(col);
        g.strokePath(shackle, juce::PathStrokeType(
            juce::jmax(1.0f, w * 0.10f),
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.fillRoundedRectangle(body, juce::jmax(1.0f, w * 0.10f));

        // ── 鍵穴 ──
        g.setColour(AmbienceColors::Background);
        const float kh = w * 0.16f;
        g.fillEllipse(cx - kh * 0.5f, body.getCentreY() - kh * 0.7f, kh, kh);
    }
};

class FDNReverbEditor : public juce::AudioProcessorEditor,
    private juce::Timer
{
public:
    explicit FDNReverbEditor(FDNReverbAudioProcessor&);
    ~FDNReverbEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void updatePanelVisibility();

    // ─── UI scale (click the "AMBIENCE" title to change) ───
    void setEditorScale(float newScale);
    void showScaleMenu();
    float scale{ 1.0f };

    // 最後に選んだ UI 倍率をグローバルに永続化し、新規インスタンスへ引き継ぐ。
    // 保存先: ~/.config/Ambience/scale.settings (OS ごとに JUCE が解決)
    std::unique_ptr<juce::PropertiesFile> uiSettings;
    static juce::PropertiesFile::Options  uiSettingsOptions();

    // ─── プリセット UI ヘルパー ───
    void refreshPresetCombo();
    void savePresetWithDialog();
    void deleteCurrentPreset();

    FDNReverbAudioProcessor& audioProcessor;
    AmbienceLookAndFeel laf;

    // ─── 共通 ───
    AlgorithmSelector algoSelector;
    RT60Visualizer    rt60Viz;
    DecayCurveViz     decayCurveViz;
    VUMeter           vuIn, vuOut;
    juce::Label       titleLabel;

    juce::Label labelMetricsTitle;
    juce::Label labelD50Caption, labelD50Value;
    juce::Label labelC50Caption, labelC50Value;
    juce::Label labelC80Caption, labelC80Value;
    juce::Label labelEDTCaption, labelEDTValue;

    MixLockButton    mixLockButton;   // Wet/Dry をプリセット切り替えから保護
    juce::TextButton proModeButton;
    juce::TextButton erSoloButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> proModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> erSoloAttachment;

    bool isProMode{ false };

    // ─── Normal Mode ノブ ───
    ArcKnob kPreDelay, kRoomSize, kDecay;
    ArcKnob kHFDamp, kLFAbsorb;
    ArcKnob kDiffusion, kModAmt, kModRate;
    ArcKnob kStereoW;
    ArcKnob kERLevel, kSaturation;
    ArcKnob kWet, kDry;
    ArcKnob kDuckAmt, kDuckThr, kDuckAtt, kDuckRel;
    ArcKnob kLoCutNorm, kHiCutNorm;

    // ─── ProMode パネル ───
    std::array<ArcKnob, 10> kRTBands;
    juce::Label    satTypeLabel;
    juce::ComboBox satTypeCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> satTypeAttachment;
    ArcKnob kTiltLow, kTiltMid, kTiltHigh;
    ArcKnob kLoCutPro, kHiCutPro;

    // ─── プリセット UI ───
    std::unique_ptr<PresetManager> presetManager;
    juce::TextButton presetPrevButton;
    juce::ComboBox   presetCombo;
    juce::TextButton presetNextButton;
    // 既存の presetSaveButton / presetDeleteButton の隣に追加
    juce::TextButton presetSaveButton;
    juce::TextButton presetLoadButton;    // ★ 追加
    juce::TextButton presetDeleteButton;

    // ─── Layout 定数 ───
    static constexpr int W = 900;
    static constexpr int H = 540;
    static constexpr int PAD = 8;
    static constexpr int KNOB_W = 64;
    static constexpr int KNOB_H = 72;
    static constexpr int KNOB_LBL_H = 14;
    static constexpr int UNIT_H = 88;
    static constexpr int ROW1_GAP = 18;

    // プリセットパネルの固定 X 座標
    // DUCKING 終端 (616) + gap(16) = 632
    static constexpr int PRESET_PANEL_X = 632;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FDNReverbEditor)
};