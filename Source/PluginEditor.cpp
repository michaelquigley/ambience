// ============================================================
//  PluginEditor.cpp  — Ambience FDN Reverb GUI
//  Fix v3:
//    1. parameterChanged: use roundToInt(newVal) directly
//       (AudioParameterChoice passes the INDEX, not normalized 0-1)
//    2. Layout: explicit y-positions for section labels / knob rows
//       to eliminate all overlap
// ============================================================
#include "PluginEditor.h"
#include <cmath>

using namespace FDNReverb;

// ─────────────────────────────────────────────────────────────────────────────
//  Layout constants (all explicit, no arithmetic that causes drift)
// ─────────────────────────────────────────────────────────────────────────────
static constexpr int W = 900;
static constexpr int H = 540;
static constexpr int PAD = 8;
static constexpr int HEADER_H = 32;
static constexpr int ALGO_H = 30;
static constexpr int SLABEL_H = 14;   // section label height
static constexpr int KNOB_SIZE = 64;
static constexpr int KNOB_LBL_H = 14;
static constexpr int UNIT_H = KNOB_SIZE + KNOB_LBL_H + 2;  // 80

// Named y-positions (computed once, used in both resized() and paint())
static constexpr int Y_HEADER = PAD;                              // 8
static constexpr int Y_ALGO = Y_HEADER + HEADER_H + PAD;       // 48
static constexpr int Y_SLABEL1 = Y_ALGO + ALGO_H + PAD;       // 86
static constexpr int Y_ROW1 = Y_SLABEL1 + SLABEL_H + 4;        // 104
static constexpr int Y_SLABEL2 = Y_ROW1 + UNIT_H + PAD;       // 192
static constexpr int Y_ROW2 = Y_SLABEL2 + SLABEL_H + 4;        // 210
static constexpr int Y_SEP = Y_ROW2 + UNIT_H + PAD;       // 298
static constexpr int Y_VIZ = Y_SEP + 4;                       // 302
static constexpr int VIZ_H = H - Y_VIZ - PAD;                 // 230

// ─────────────────────────────────────────────────────────────────────────────
//  KronosLookAndFeel
// ─────────────────────────────────────────────────────────────────────────────
KronosLookAndFeel::KronosLookAndFeel()
{
    setColour(juce::Slider::backgroundColourId, KronosColors::ArcTrack);
    setColour(juce::Slider::thumbColourId, KronosColors::Accent);
    setColour(juce::Slider::trackColourId, KronosColors::ArcFill);
    setColour(juce::Label::textColourId, KronosColors::TextSecondary);
    setColour(juce::ComboBox::backgroundColourId, KronosColors::Surface);
    setColour(juce::ComboBox::textColourId, KronosColors::TextPrimary);
    setColour(juce::ComboBox::outlineColourId, KronosColors::Border);
    setColour(juce::PopupMenu::backgroundColourId, KronosColors::Panel);
    setColour(juce::PopupMenu::textColourId, KronosColors::TextPrimary);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, KronosColors::Accent);
    setColour(juce::TextButton::buttonColourId, KronosColors::Surface);
    setColour(juce::TextButton::textColourOffId, KronosColors::TextSecondary);
    setColour(juce::TextButton::textColourOnId, KronosColors::Accent);
    mainFont = juce::Font(juce::FontOptions("Helvetica Neue", 11.f, juce::Font::plain));
}

void KronosLookAndFeel::drawRotarySlider(juce::Graphics& g,
    int x, int y, int w, int h,
    float sliderPos, float startAngle, float endAngle,
    juce::Slider& slider)
{
    auto b = juce::Rectangle<float>((float)x, (float)y, (float)w, (float)h).reduced(4.f);
    float cx = b.getCentreX(), cy = b.getCentreY();
    float r = juce::jmin(b.getWidth(), b.getHeight()) * 0.45f;
    float th = r * 0.22f;

    juce::Path track;
    track.addCentredArc(cx, cy, r, r, 0.f, startAngle, endAngle, true);
    g.setColour(KronosColors::ArcTrack);
    g.strokePath(track, juce::PathStrokeType(th,
        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    float angle = startAngle + sliderPos * (endAngle - startAngle);
    juce::Path fill;
    fill.addCentredArc(cx, cy, r, r, 0.f, startAngle, angle, true);
    juce::ColourGradient grad(KronosColors::AccentBlue, cx - r, cy,
        KronosColors::Accent, cx + r, cy, false);
    g.setGradientFill(grad);
    g.strokePath(fill, juce::PathStrokeType(th,
        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour(KronosColors::Panel);
    g.fillEllipse(cx - r * 0.28f, cy - r * 0.28f, r * 0.56f, r * 0.56f);

    float ix = cx + r * 0.6f * std::sin(angle);
    float iy = cy - r * 0.6f * std::cos(angle);
    g.setColour(KronosColors::TextPrimary);
    g.drawLine(cx, cy, ix, iy, 2.f);

    if (slider.hasKeyboardFocus(true)) {
        g.setColour(KronosColors::Accent.withAlpha(0.5f));
        g.drawEllipse(cx - r - 3.f, cy - r - 3.f, (r + 3.f) * 2.f, (r + 3.f) * 2.f, 1.5f);
    }
}

void KronosLookAndFeel::drawLinearSlider(juce::Graphics& g,
    int x, int y, int w, int h,
    float sliderPos, float, float,
    juce::Slider::SliderStyle, juce::Slider&)
{
    auto b = juce::Rectangle<int>(x, y, w, h).toFloat();
    float ty = b.getCentreY() - 2.f;
    g.setColour(KronosColors::ArcTrack);
    g.fillRoundedRectangle(b.getX(), ty, b.getWidth(), 4.f, 2.f);
    g.setColour(KronosColors::Accent);
    g.fillRoundedRectangle(b.getX(), ty, sliderPos - b.getX(), 4.f, 2.f);
    float r = 7.f;
    g.setColour(KronosColors::TextPrimary);
    g.fillEllipse(sliderPos - r, b.getCentreY() - r, r * 2.f, r * 2.f);
}

void KronosLookAndFeel::drawComboBox(juce::Graphics& g,
    int w, int h, bool isDown, int, int, int, int, juce::ComboBox&)
{
    auto b = juce::Rectangle<int>(0, 0, w, h).toFloat();
    g.setColour(isDown ? KronosColors::Panel : KronosColors::Surface);
    g.fillRoundedRectangle(b, 3.f);
    g.setColour(KronosColors::Border);
    g.drawRoundedRectangle(b.reduced(0.5f), 3.f, 1.f);
    juce::Path arrow;
    arrow.addTriangle(w - 16.f, h * 0.5f - 3.f, w - 8.f, h * 0.5f - 3.f, w - 12.f, h * 0.5f + 3.f);
    g.setColour(KronosColors::TextSecondary);
    g.fillPath(arrow);
}

void KronosLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label) {
    label.setBounds(6, 1, box.getWidth() - 22, box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
}

juce::Font KronosLookAndFeel::getLabelFont(juce::Label&) {
    return mainFont.withHeight(10.f);
}
juce::Font KronosLookAndFeel::getComboBoxFont(juce::ComboBox&) {
    return mainFont.withHeight(11.f);
}

void KronosLookAndFeel::drawGroupComponentOutline(juce::Graphics& g,
    int w, int h, const juce::String& text,
    const juce::Justification&, juce::GroupComponent&)
{
    float textH = 12.f, indent = 8.f, yOff = textH * 0.5f;
    juce::Path p;
    p.startNewSubPath(indent + 4.f, yOff); p.lineTo(indent, yOff);
    p.lineTo(indent, (float)h - 1.f); p.lineTo((float)w - indent, (float)h - 1.f);
    p.lineTo((float)w - indent, yOff);
    float tw = mainFont.withHeight(textH).getStringWidth(text) + 6.f;
    p.lineTo(indent + 14.f + tw, yOff);
    g.setColour(KronosColors::Border);
    g.strokePath(p, juce::PathStrokeType(1.f));
    g.setColour(KronosColors::TextSecondary);
    g.setFont(mainFont.withHeight(textH).boldened());
    g.drawText(text, (int)(indent + 14.f), 0, (int)tw, (int)textH,
        juce::Justification::centredLeft);
}

// ─────────────────────────────────────────────────────────────────────────────
//  RT60 Visualizer
// ─────────────────────────────────────────────────────────────────────────────
RT60Visualizer::RT60Visualizer() {
    displayRT60 = ALL_PRESETS[0]->acoustics.rt60;
    startTimerHz(15);
}
RT60Visualizer::~RT60Visualizer() { stopTimer(); }

void RT60Visualizer::timerCallback() {
    if (!processor) return;
    auto live = processor->getRT60ForDisplay();
    for (int i = 0; i < NUM_BANDS; ++i)
        displayRT60[i] += 0.25f * (live[i] - displayRT60[i]);
    repaint();
}

void RT60Visualizer::paint(juce::Graphics& g) {
    auto b = getLocalBounds().toFloat().reduced(2.f);
    float W2 = b.getWidth(), H2 = b.getHeight();
    float x0 = b.getX(), y0 = b.getY();

    g.setColour(KronosColors::Surface);
    g.fillRoundedRectangle(b, 4.f);
    g.setColour(KronosColors::Border);
    g.drawRoundedRectangle(b.reduced(0.5f), 4.f, 1.f);

    float logMin = std::log10(MIN_RT60_DISPLAY);
    float logMax = std::log10(MAX_RT60_DISPLAY);

    static const float gridVals[] = { 0.1f,0.3f,0.5f,1.0f,2.0f,4.0f,8.0f };
    g.setColour(KronosColors::Separator);
    for (float v : gridVals) {
        float ny = 1.f - (std::log10(v) - logMin) / (logMax - logMin);
        g.drawHorizontalLine((int)(y0 + ny * H2), x0 + 36.f, x0 + W2 - 4.f);
    }

    g.setFont(8.5f);
    g.setColour(KronosColors::TextSecondary);
    static const char* fLbls[] = { "31","63","125","250","500","1k","2k","4k","8k","16k" };
    for (int i = 0; i < NUM_BANDS; ++i) {
        float px = x0 + 36.f + (float)i / (NUM_BANDS - 1) * (W2 - 40.f);
        g.drawText(fLbls[i], (int)(px - 12.f), (int)(y0 + H2 - 14.f), 24, 13,
            juce::Justification::centred);
    }
    for (float v : gridVals) {
        float ny = 1.f - (std::log10(v) - logMin) / (logMax - logMin);
        float py = y0 + ny * H2;
        juce::String lbl = (v < 1.f) ? juce::String(v, 1) + "s" : juce::String((int)v) + "s";
        g.drawText(lbl, (int)(x0 + 2.f), (int)(py - 7.f), 32, 14,
            juce::Justification::centredLeft);
    }

    auto plotCurve = [&](const std::array<float, NUM_BANDS>& rt60,
        juce::Colour col, float thick) {
            juce::Path path;
            bool first = true;
            for (int i = 0; i < NUM_BANDS; ++i) {
                float v = std::clamp(rt60[i], MIN_RT60_DISPLAY, MAX_RT60_DISPLAY);
                float ny = 1.f - (std::log10(v) - logMin) / (logMax - logMin);
                float px = x0 + 36.f + (float)i / (NUM_BANDS - 1) * (W2 - 40.f);
                float py = y0 + ny * H2;
                if (first) { path.startNewSubPath(px, py); first = false; }
                else        path.lineTo(px, py);
            }
            g.setColour(col);
            g.strokePath(path, juce::PathStrokeType(thick,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            for (int i = 0; i < NUM_BANDS; ++i) {
                float v = std::clamp(rt60[i], MIN_RT60_DISPLAY, MAX_RT60_DISPLAY);
                float ny = 1.f - (std::log10(v) - logMin) / (logMax - logMin);
                float px = x0 + 36.f + (float)i / (NUM_BANDS - 1) * (W2 - 40.f);
                float py = y0 + ny * H2;
                g.fillEllipse(px - 3.f, py - 3.f, 6.f, 6.f);
            }
        };

    if (processor) {
        int algo = (int)*processor->apvts.getRawParameterValue("algorithm");
        auto& preset = *ALL_PRESETS[juce::jlimit(0, NUM_ALGORITHMS - 1, algo)];
        plotCurve(preset.acoustics.rt60,
            KronosColors::TextSecondary.withAlpha(0.5f), 1.f);
    }
    plotCurve(displayRT60, KronosColors::Accent, 2.f);

    g.setColour(KronosColors::TextSecondary);
    g.setFont(9.f);
    g.drawText("RT60 (s) per band", (int)x0 + 36, (int)y0 + 3, (int)W2 - 40, 12,
        juce::Justification::right);
}

// ─────────────────────────────────────────────────────────────────────────────
//  VUMeter
// ─────────────────────────────────────────────────────────────────────────────
VUMeter::VUMeter(const juce::String& lbl, Side s) : label(lbl), side(s) {}
void VUMeter::paint(juce::Graphics& g) {
    auto b = getLocalBounds().toFloat().reduced(1.f);
    g.setColour(KronosColors::Surface);
    g.fillRoundedRectangle(b, 3.f);
    float bx = b.getX() + 22.f, bw = b.getWidth() - 22.f;
    auto bar = [&](float y, float level) {
        float n = juce::jlimit(0.f, 1.f, juce::jmap(
            juce::Decibels::gainToDecibels(level + 1e-9f), -60.f, 0.f, 0.f, 1.f));
        g.setColour(KronosColors::ArcTrack);
        g.fillRoundedRectangle(bx, y, bw, 7.f, 2.f);
        juce::ColourGradient gr(KronosColors::AccentBlue, bx, y, KronosColors::Accent, bx + bw, y, false);
        g.setGradientFill(gr);
        g.fillRoundedRectangle(bx, y, bw * n, 7.f, 2.f);
        };
    bar(b.getY() + 2.f, levelL);
    bar(b.getY() + 11.f, levelR);
    g.setColour(KronosColors::TextSecondary);
    g.setFont(8.f);
    g.drawText(label, (int)b.getX(), (int)b.getY(), 20, (int)b.getHeight(),
        juce::Justification::centredLeft);
}

// ─────────────────────────────────────────────────────────────────────────────
//  ArcKnob
// ─────────────────────────────────────────────────────────────────────────────
void ArcKnob::build(juce::AudioProcessorValueTreeState& apvts,
    const juce::String& paramID,
    const juce::String& labelText,
    juce::Component* parent,
    KronosLookAndFeel& laf)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 62, 14);
    slider.setLookAndFeel(&laf);
    slider.setColour(juce::Slider::textBoxTextColourId, KronosColors::TextSecondary);
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    parent->addAndMakeVisible(slider);

    label.setText(labelText, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setFont(juce::Font(juce::FontOptions(9.f)));
    label.setColour(juce::Label::textColourId, KronosColors::TextSecondary);
    parent->addAndMakeVisible(label);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, paramID, slider);
}

// ─────────────────────────────────────────────────────────────────────────────
//  AlgorithmSelector
// ─────────────────────────────────────────────────────────────────────────────
AlgorithmSelector::AlgorithmSelector(juce::AudioProcessorValueTreeState& a)
    : apvts(a)
{
    static const char* names[] = {
        "ROOM1","ROOM2","HALL1","HALL2","PLATE","SPRING","GOLDFOIL"
    };
    for (int i = 0; i < NUM_ALGORITHMS; ++i) {
        buttons[i].setButtonText(names[i]);
        buttons[i].setClickingTogglesState(false);
        addAndMakeVisible(buttons[i]);

        int idx = i;
        buttons[i].onClick = [this, idx] {
            if (auto* param = apvts.getParameter("algorithm"))
                param->setValueNotifyingHost((float)idx / (float)(NUM_ALGORITHMS - 1));
            };
    }

    apvts.addParameterListener("algorithm", this);
    // getRawParameterValue for AudioParameterChoice returns normalized 0-1
    currentAlgo = juce::roundToInt(
        *apvts.getRawParameterValue("algorithm") * (NUM_ALGORITHMS - 1));
    updateButtonColors();
}

AlgorithmSelector::~AlgorithmSelector() {
    apvts.removeParameterListener("algorithm", this);
}

// ─── [FIX] parameterChanged receives the INDEX (not normalized 0-1) ─────────
void AlgorithmSelector::parameterChanged(const juce::String&, float newVal)
{
    // AudioParameterChoice calls listeners with the DENORMALIZED index value
    int newAlgo = juce::jlimit(0, NUM_ALGORITHMS - 1, juce::roundToInt(newVal));
    juce::MessageManager::callAsync([this, newAlgo] {
        currentAlgo = newAlgo;
        updateButtonColors();
        });
}

void AlgorithmSelector::updateButtonColors()
{
    for (int i = 0; i < NUM_ALGORITHMS; ++i) {
        bool on = (i == currentAlgo);
        buttons[i].setColour(juce::TextButton::buttonColourId,
            on ? KronosColors::Accent : KronosColors::Surface);
        buttons[i].setColour(juce::TextButton::textColourOffId,
            on ? KronosColors::Background : KronosColors::TextSecondary);
        buttons[i].repaint();
    }
}

void AlgorithmSelector::paint(juce::Graphics& g) {
    g.setColour(KronosColors::Surface);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.f);
}

void AlgorithmSelector::resized() {
    auto area = getLocalBounds().reduced(2);
    int btnW = area.getWidth() / NUM_ALGORITHMS;
    for (int i = 0; i < NUM_ALGORITHMS; ++i)
        buttons[i].setBounds(area.getX() + i * btnW, area.getY(), btnW - 1, area.getHeight());
    updateButtonColors();
}

// ─────────────────────────────────────────────────────────────────────────────
//  FDNReverbEditor
// ─────────────────────────────────────────────────────────────────────────────
FDNReverbEditor::FDNReverbEditor(FDNReverbAudioProcessor& p)
    : AudioProcessorEditor(&p),
    audioProcessor(p),
    algoSelector(p.apvts),
    vuIn("IN", VUMeter::Side::Input),
    vuOut("OUT", VUMeter::Side::Output)
{
    setLookAndFeel(&laf);
    setSize(W, H);
    setOpaque(true);

    titleLabel.setText("AMBIENCE", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions("Helvetica Neue", 14.f, juce::Font::bold)));
    titleLabel.setColour(juce::Label::textColourId, KronosColors::TextPrimary);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(titleLabel);

    addAndMakeVisible(algoSelector);

#define BK(k,id,lbl) k.build(p.apvts, id, lbl, this, laf)
    BK(kPreDelay, "predelay", "PRE-DELAY");
    BK(kRoomSize, "roomsize", "ROOM SIZE");
    BK(kDecay, "decaytime", "DECAY");
    BK(kHFDamp, "hfdamping", "HF DAMP");
    BK(kLFAbsorb, "lfabsorption", "LF ABSORB");
    BK(kDiffusion, "diffusion", "DIFFUSION");
    BK(kModAmt, "modamount", "MOD AMT");
    BK(kModRate, "modrate", "MOD RATE");
    BK(kStereoW, "stereowidth", "WIDTH");
    BK(kCrossFeed, "crossfeed", "X-FEED");
    BK(kERLevel, "erlevel", "ER LEVEL");
    BK(kSaturation, "saturation", "SATURATE");
    BK(kWet, "wetlevel", "WET");
    BK(kDry, "drylevel", "DRY");
    BK(kDuckAmt, "duckamount", "AMOUNT");
    BK(kDuckThr, "duckthresh", "THRESH");
    BK(kDuckAtt, "duckattack", "ATTACK");
    BK(kDuckRel, "duckrelease", "RELEASE");
#undef BK

    oversamplingLabel.setText("OS", juce::dontSendNotification);
    oversamplingLabel.setColour(juce::Label::textColourId, KronosColors::TextSecondary);
    oversamplingLabel.setFont(juce::Font(juce::FontOptions(9.f)));
    addAndMakeVisible(oversamplingLabel);

    oversamplingCombo.addItem("1x", 1); oversamplingCombo.addItem("2x", 2);
    oversamplingCombo.addItem("4x", 3); oversamplingCombo.addItem("8x", 4);
    oversamplingCombo.setLookAndFeel(&laf);
    addAndMakeVisible(oversamplingCombo);
    osAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        p.apvts, "oversampling", oversamplingCombo);

    rt60Viz.setProcessor(&p);
    addAndMakeVisible(rt60Viz);
    addAndMakeVisible(vuIn);
    addAndMakeVisible(vuOut);

    startTimerHz(60);
}

FDNReverbEditor::~FDNReverbEditor() {
    stopTimer();
    setLookAndFeel(nullptr);
    for (auto* k : { &kPreDelay,&kRoomSize,&kDecay,&kHFDamp,&kLFAbsorb,
                     &kDiffusion,&kModAmt,&kModRate,&kStereoW,&kCrossFeed,
                     &kERLevel,&kSaturation,&kWet,&kDry,
                     &kDuckAmt,&kDuckThr,&kDuckAtt,&kDuckRel })
        k->slider.setLookAndFeel(nullptr);
    oversamplingCombo.setLookAndFeel(nullptr);
}

void FDNReverbEditor::timerCallback() {
    vuIn.setLevels(audioProcessor.getInputRMSL(), audioProcessor.getInputRMSR());
    vuOut.setLevels(audioProcessor.getOutputRMSL(), audioProcessor.getOutputRMSR());
    vuIn.repaint(); vuOut.repaint();
}

// ─────────────────────────────────────────────────────────────────────────────
//  resized  — uses named y-positions, no arithmetic drift
// ─────────────────────────────────────────────────────────────────────────────
void FDNReverbEditor::resized()
{
    // Header
    titleLabel.setBounds(PAD, Y_HEADER, 180, HEADER_H);
    oversamplingLabel.setBounds(W - 96, Y_HEADER + 6, 18, 12);
    oversamplingCombo.setBounds(W - 76, Y_HEADER + 4, 68, 18);
    vuIn.setBounds(W - 220, Y_HEADER + 2, 96, 28);
    vuOut.setBounds(W - 120, Y_HEADER + 2, 96, 28);

    // Algorithm selector
    algoSelector.setBounds(PAD, Y_ALGO, W - PAD * 2, ALGO_H);

    // ── Row 1 knobs (Y_ROW1) ─────────────────────────────────────────────
    // Group widths: TIME=3, FREQ=2, DIFFUSION=3, STEREO=2, CHARACTER=2  → 12 knobs
    int kx = PAD;
    auto next = [&](ArcKnob& k) {
        k.slider.setBounds(kx, Y_ROW1, KNOB_SIZE, KNOB_SIZE);
        k.label.setBounds(kx, Y_ROW1 + KNOB_SIZE, KNOB_SIZE, KNOB_LBL_H);
        kx += KNOB_SIZE + PAD;
        };
    next(kPreDelay);  next(kRoomSize);  next(kDecay);    // TIME (3)
    kx += 6;
    next(kHFDamp);    next(kLFAbsorb);                   // FREQUENCY (2)
    kx += 6;
    next(kDiffusion); next(kModAmt);    next(kModRate);  // DIFFUSION (3)
    kx += 6;
    next(kStereoW);   next(kCrossFeed);                  // STEREO (2)
    kx += 6;
    next(kERLevel);   next(kSaturation);                 // CHARACTER (2)

    // ── Row 2 knobs (Y_ROW2) ─────────────────────────────────────────────
    kx = PAD;
    next(kWet);  next(kDry);                             // MIX (2)
    kx += 16;
    next(kDuckAmt); next(kDuckThr); next(kDuckAtt); next(kDuckRel);  // DUCKING (4)

    // RT60 visualizer
    rt60Viz.setBounds(PAD, Y_VIZ, W - PAD * 2, VIZ_H);
}

// ─────────────────────────────────────────────────────────────────────────────
//  paint  — section labels drawn at Y_SLABEL1 / Y_SLABEL2 (correct positions)
// ─────────────────────────────────────────────────────────────────────────────
void FDNReverbEditor::paint(juce::Graphics& g)
{
    g.fillAll(KronosColors::Background);

    juce::ColourGradient grad(KronosColors::Surface.withAlpha(0.12f), 0.f, 0.f,
        KronosColors::Background, 0.f, (float)H, false);
    g.setGradientFill(grad);
    g.fillAll();

    // Subtitle (all ASCII)
    g.setFont(juce::Font(juce::FontOptions(8.f)));
    g.setColour(KronosColors::TextSecondary.withAlpha(0.35f));
    g.drawText("8ch FDN | SAPF | ISM-ER | 44.1-192kHz | 1-8x OS",
        PAD + 190, Y_HEADER + 10, W / 2, 12, juce::Justification::centredLeft);

    // ── Section separator lines for Row 1 ───────────────────────────────
    // Groups: TIME(3) | FREQ(2) | DIFFUSION(3) | STEREO(2) | CHARACTER(2)
    static const int gw[] = { 3, 2, 3, 2, 2 };  // knobs per group
    int lx = PAD;
    g.setColour(KronosColors::Separator);
    for (int gi = 0; gi < 4; ++gi) {
        lx += gw[gi] * (KNOB_SIZE + PAD) + 6;
        g.drawVerticalLine(lx - 3, (float)Y_SLABEL1, (float)(Y_ROW1 + UNIT_H));
    }

    // Section separator for Row 2
    int lx2 = PAD + 2 * (KNOB_SIZE + PAD) + 16;
    g.drawVerticalLine(lx2 - 3, (float)Y_SLABEL2, (float)(Y_ROW2 + UNIT_H));

    // Separator before viz
    g.drawHorizontalLine(Y_SEP, (float)PAD, (float)(W - PAD));

    // ── Section labels at Y_SLABEL1 (guaranteed to be below algo selector) ──
    g.setFont(juce::Font(juce::FontOptions("Helvetica Neue", 8.5f, juce::Font::bold)));
    g.setColour(KronosColors::Accent.withAlpha(0.75f));

    auto sl = [&](int sx, int sy, const char* text) {
        g.drawText(juce::String(text), sx, sy, 120, SLABEL_H,
            juce::Justification::centredLeft);
        };

    // Row 1 section labels
    int bx = PAD;
    sl(bx, Y_SLABEL1, "TIME");
    bx += gw[0] * (KNOB_SIZE + PAD) + 6;
    sl(bx, Y_SLABEL1, "FREQUENCY");
    bx += gw[1] * (KNOB_SIZE + PAD) + 6;
    sl(bx, Y_SLABEL1, "DIFFUSION");
    bx += gw[2] * (KNOB_SIZE + PAD) + 6;
    sl(bx, Y_SLABEL1, "STEREO");
    bx += gw[3] * (KNOB_SIZE + PAD) + 6;
    sl(bx, Y_SLABEL1, "CHARACTER");

    // Row 2 section labels
    bx = PAD;
    sl(bx, Y_SLABEL2, "MIX");
    bx += 2 * (KNOB_SIZE + PAD) + 16;
    sl(bx, Y_SLABEL2, "DUCKING");
}