#include "PluginProcessor.h"
#include "PluginEditor.h"

static constexpr int Y_HEADER = 8;
static constexpr int Y_ALGO = 48;
static constexpr int Y_SLABEL1 = 86;
static constexpr int Y_ROW1 = 104;
static constexpr int Y_SLABEL2 = 204;
static constexpr int Y_ROW2 = 222;
static constexpr int Y_SEP = 322;
static constexpr int Y_VIZ = 326;

FDNReverbEditor::FDNReverbEditor(FDNReverbAudioProcessor& p)
    : AudioProcessorEditor(&p),
    audioProcessor(p),
    algoSelector(p.apvts),
    vuIn("IN", VUMeter::Side::Input),
    vuOut("OUT", VUMeter::Side::Output)
{
    setLookAndFeel(&laf);
    setSize(W, H);

    titleLabel.setText("AMBIENCE", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions(
        "Helvetica Neue", 14.f, juce::Font::bold)));
    titleLabel.setColour(juce::Label::textColourId, AmbienceColors::TextPrimary);
    addAndMakeVisible(titleLabel);

    addAndMakeVisible(algoSelector);

    auto BK = [&](ArcKnob& k, const char* id, const char* lbl) {
        k.build(p.apvts, id, lbl, this, laf);
        };

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

    proModeButton.setButtonText("PRO");
    proModeButton.setClickingTogglesState(true);
    proModeButton.setColour(juce::TextButton::buttonOnColourId, AmbienceColors::Accent);
    proModeButton.setColour(juce::TextButton::buttonColourId, AmbienceColors::Surface);
    proModeButton.setColour(juce::TextButton::textColourOnId, AmbienceColors::Background);
    proModeButton.setColour(juce::TextButton::textColourOffId, AmbienceColors::TextSecondary);
    addAndMakeVisible(proModeButton);
    proModeAttachment.reset(
        new juce::AudioProcessorValueTreeState::ButtonAttachment(
            p.apvts, "promode", proModeButton));

    erSoloButton.setButtonText("ER SOLO");
    erSoloButton.setClickingTogglesState(true);
    erSoloButton.setColour(juce::TextButton::buttonOnColourId, AmbienceColors::AccentBlue);
    erSoloButton.setColour(juce::TextButton::buttonColourId, AmbienceColors::Surface);
    erSoloButton.setColour(juce::TextButton::textColourOnId, AmbienceColors::Background);
    erSoloButton.setColour(juce::TextButton::textColourOffId, AmbienceColors::TextSecondary);
    addAndMakeVisible(erSoloButton);
    erSoloAttachment.reset(
        new juce::AudioProcessorValueTreeState::ButtonAttachment(
            p.apvts, "ersolo", erSoloButton));

    static const char* rtBandIDs[] = {
        "rtband0","rtband1","rtband2","rtband3","rtband4",
        "rtband5","rtband6","rtband7","rtband8","rtband9"
    };
    static const char* rtBandLbls[] = {
        "31Hz","63Hz","125Hz","250Hz","500Hz",
        "1kHz","2kHz","4kHz","8kHz","16kHz"
    };
    for (int i = 0; i < 10; ++i)
        kRTBands[i].build(p.apvts, rtBandIDs[i], rtBandLbls[i], this, laf);

    satTypeLabel.setText("SAT TYPE", juce::dontSendNotification);
    satTypeLabel.setFont(juce::Font(juce::FontOptions(9.f)));
    satTypeLabel.setColour(juce::Label::textColourId, AmbienceColors::TextSecondary);
    satTypeLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(satTypeLabel);

    satTypeCombo.addItemList({ "Warm","Tape","Tube","Hard" }, 1);
    satTypeCombo.setLookAndFeel(&laf);
    addAndMakeVisible(satTypeCombo);
    satTypeAttachment.reset(
        new juce::AudioProcessorValueTreeState::ComboBoxAttachment(
            p.apvts, "sattype", satTypeCombo));

    BK(kTiltLow, "tiltlow", "TILT LOW");
    BK(kTiltMid, "tiltmid", "TILT MID");
    BK(kTiltHigh, "tilthigh", "TILT HIGH");

    rt60Viz.setProcessor(&p);
    decayCurveViz.setProcessor(&p);
    addAndMakeVisible(rt60Viz);
    addAndMakeVisible(decayCurveViz);

    addAndMakeVisible(vuIn);
    addAndMakeVisible(vuOut);

    labelMetricsTitle.setText("ACOUSTICS", juce::dontSendNotification);
    labelMetricsTitle.setFont(juce::Font(juce::FontOptions(
        "Helvetica Neue", 8.5f, juce::Font::bold)));
    labelMetricsTitle.setColour(juce::Label::textColourId,
        AmbienceColors::Accent.withAlpha(0.75f));
    labelMetricsTitle.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(labelMetricsTitle);

    auto setupCaption = [this](juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        label.setFont(juce::Font(juce::FontOptions(
            "Helvetica Neue", 8.0f, juce::Font::plain)));
        label.setColour(juce::Label::textColourId,
            AmbienceColors::TextSecondary.withAlpha(0.85f));
        label.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(label);
        };
    auto setupValue = [this](juce::Label& label) {
        label.setText("--", juce::dontSendNotification);
        label.setFont(juce::Font(juce::FontOptions(
            "Helvetica Neue", 9.5f, juce::Font::bold)));
        label.setColour(juce::Label::textColourId, AmbienceColors::TextPrimary);
        label.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(label);
        };

    setupCaption(labelD50Caption, "D50:");
    setupCaption(labelC50Caption, "C50:");
    setupCaption(labelC80Caption, "C80:");
    setupCaption(labelEDTCaption, "EDT:");
    setupValue(labelD50Value);
    setupValue(labelC50Value);
    setupValue(labelC80Value);
    setupValue(labelEDTValue);

    updatePanelVisibility();
    startTimerHz(60);
}

FDNReverbEditor::~FDNReverbEditor() {
    stopTimer();
    setLookAndFeel(nullptr);
}

void FDNReverbEditor::timerCallback()
{
    vuIn.setLevels(audioProcessor.getInputRMSL(),
        audioProcessor.getInputRMSR());
    vuOut.setLevels(audioProcessor.getOutputRMSL(),
        audioProcessor.getOutputRMSR());
    vuIn.repaint();
    vuOut.repaint();

    static int metricsCounter = 0;
    if (++metricsCounter >= 2) {
        metricsCounter = 0;
        labelD50Value.setText(
            juce::String(audioProcessor.getD50() * 100.0f, 1) + "%",
            juce::dontSendNotification);
        labelC50Value.setText(
            juce::String(audioProcessor.getC50(), 1) + "dB",
            juce::dontSendNotification);
        labelC80Value.setText(
            juce::String(audioProcessor.getC80(), 1) + "dB",
            juce::dontSendNotification);
        labelEDTValue.setText(
            juce::String(audioProcessor.getEDT(), 2) + "s",
            juce::dontSendNotification);
    }

    bool newProMode = (*audioProcessor.apvts.getRawParameterValue("promode") > 0.5f);
    if (newProMode != isProMode) {
        isProMode = newProMode;
        updatePanelVisibility();
        resized();
        repaint();
    }
}

void FDNReverbEditor::updatePanelVisibility()
{
    auto setKnob = [](ArcKnob& k, bool vis) {
        k.slider.setVisible(vis);
        k.label.setVisible(vis);
        };

    const bool showNormal = !isProMode;
    const bool showPro = isProMode;

    setKnob(kPreDelay, showNormal);
    setKnob(kRoomSize, showNormal);
    setKnob(kDecay, showNormal);
    setKnob(kHFDamp, showNormal);
    setKnob(kLFAbsorb, showNormal);
    setKnob(kDiffusion, showNormal);
    setKnob(kModAmt, showNormal);
    setKnob(kModRate, showNormal);
    setKnob(kStereoW, showNormal);
    setKnob(kCrossFeed, showNormal);
    setKnob(kERLevel, showNormal);
    setKnob(kSaturation, showNormal);
    setKnob(kWet, showNormal);
    setKnob(kDry, showNormal);
    setKnob(kDuckAmt, showNormal);
    setKnob(kDuckThr, showNormal);
    setKnob(kDuckAtt, showNormal);
    setKnob(kDuckRel, showNormal);

    for (auto& k : kRTBands) setKnob(k, showPro);
    satTypeLabel.setVisible(showPro);
    satTypeCombo.setVisible(showPro);
    setKnob(kTiltLow, showPro);
    setKnob(kTiltMid, showPro);
    setKnob(kTiltHigh, showPro);
}

void FDNReverbEditor::resized()
{
    titleLabel.setBounds(PAD, Y_HEADER, 180, 32);
    proModeButton.setBounds(196, Y_HEADER + 5, 52, 22);
    erSoloButton.setBounds(256, Y_HEADER + 5, 72, 22);
    vuIn.setBounds(W - 220, Y_HEADER + 2, 96, 28);
    vuOut.setBounds(W - 120, Y_HEADER + 2, 96, 28);

    algoSelector.setBounds(PAD, Y_ALGO, W - PAD * 2, 30);

    auto place = [&](ArcKnob& k, int& x, int y) {
        k.label.setBounds(x, y, KNOB_W, KNOB_LBL_H);
        k.slider.setBounds(x, y + KNOB_LBL_H, KNOB_W, KNOB_H);
        x += KNOB_W + PAD;
        };

    if (!isProMode) {
        int kx = PAD;
        place(kPreDelay, kx, Y_ROW1);
        place(kRoomSize, kx, Y_ROW1);
        place(kDecay, kx, Y_ROW1);
        kx += 6;
        place(kHFDamp, kx, Y_ROW1);
        place(kLFAbsorb, kx, Y_ROW1);
        kx += 6;
        place(kDiffusion, kx, Y_ROW1);
        place(kModAmt, kx, Y_ROW1);
        place(kModRate, kx, Y_ROW1);
        kx += 6;
        place(kStereoW, kx, Y_ROW1);
        place(kCrossFeed, kx, Y_ROW1);
        kx += 6;
        place(kERLevel, kx, Y_ROW1);
        place(kSaturation, kx, Y_ROW1);

        kx = PAD;
        place(kWet, kx, Y_ROW2);
        place(kDry, kx, Y_ROW2);
        kx += 16;
        place(kDuckAmt, kx, Y_ROW2);
        place(kDuckThr, kx, Y_ROW2);
        place(kDuckAtt, kx, Y_ROW2);
        place(kDuckRel, kx, Y_ROW2);

    }
    else {
        // ── ProMode 1段目: RT60 帯域ノブ × 10 ──
        {
            int kx = PAD;
            for (int i = 0; i < 10; ++i)
                place(kRTBands[i], kx, Y_ROW1);
        }

        // ── ProMode 2段目: SatType + Tilt EQ ──
        {
            int kx2 = PAD;

            satTypeLabel.setBounds(kx2, Y_SLABEL2, KNOB_W, KNOB_LBL_H);
            satTypeCombo.setBounds(kx2, Y_SLABEL2 + KNOB_LBL_H + 2, KNOB_W + PAD, 24);
            kx2 += KNOB_W + PAD + PAD + 8;

            place(kTiltLow, kx2, Y_ROW2);
            place(kTiltMid, kx2, Y_ROW2);
            place(kTiltHigh, kx2, Y_ROW2);
        }
    }

    const int vizTotalH = H - Y_VIZ - PAD;
    const int rt60Height = vizTotalH / 2 - 2;
    const int decayHeight = vizTotalH / 2 - 2;
    const int decayY = Y_VIZ + rt60Height + 4;

    rt60Viz.setBounds(PAD, Y_VIZ, W - PAD * 2, rt60Height);
    decayCurveViz.setBounds(PAD, decayY, W - PAD * 2, decayHeight);

    const int metricsRight = W - PAD - 8;
    const int metricsW = 280;
    const int metricsLeft = metricsRight - metricsW;
    const int metricsTop = decayY + 6;
    const int metricsRowH = 14;
    const int metricsRow1Y = metricsTop + 14;
    const int metricsRow2Y = metricsRow1Y + metricsRowH;
    const int captionW = 28;
    const int valueW = 52;
    const int colSpacing = 8;
    const int colW = captionW + valueW;

    labelMetricsTitle.setBounds(metricsLeft, metricsTop, 80, 12);
    labelD50Caption.setBounds(metricsLeft, metricsRow1Y, captionW, metricsRowH);
    labelD50Value.setBounds(metricsLeft + captionW, metricsRow1Y, valueW, metricsRowH);
    labelC50Caption.setBounds(metricsLeft + colW + colSpacing, metricsRow1Y, captionW, metricsRowH);
    labelC50Value.setBounds(metricsLeft + colW + colSpacing + captionW, metricsRow1Y, valueW, metricsRowH);
    labelC80Caption.setBounds(metricsLeft, metricsRow2Y, captionW, metricsRowH);
    labelC80Value.setBounds(metricsLeft + captionW, metricsRow2Y, valueW, metricsRowH);
    labelEDTCaption.setBounds(metricsLeft + colW + colSpacing, metricsRow2Y, captionW, metricsRowH);
    labelEDTValue.setBounds(metricsLeft + colW + colSpacing + captionW, metricsRow2Y, valueW, metricsRowH);
}

void FDNReverbEditor::paint(juce::Graphics& g)
{
    g.fillAll(AmbienceColors::Background);
    juce::ColourGradient grad(
        AmbienceColors::Surface.withAlpha(0.12f), 0.f, 0.f,
        AmbienceColors::Background, 0.f, (float)H, false);
    g.setGradientFill(grad);
    g.fillAll();

    g.setFont(juce::Font(juce::FontOptions(8.f)));
    g.setColour(AmbienceColors::TextSecondary.withAlpha(0.35f));
    g.drawText("16ch FDN | SAPF | ISM-ER | 44.1-192kHz",
        PAD + 336, Y_HEADER + 10, W / 2, 12,
        juce::Justification::centredLeft);

    g.setColour(AmbienceColors::Separator);
    g.drawHorizontalLine(Y_SEP, (float)PAD, (float)(W - PAD));

    g.setFont(juce::Font(juce::FontOptions(
        "Helvetica Neue", 8.5f, juce::Font::bold)));
    g.setColour(AmbienceColors::Accent.withAlpha(0.75f));

    auto sl = [&](int x, int y, const char* t) {
        g.drawText(t, x, y, 160, 14, juce::Justification::centredLeft);
        };

    if (!isProMode) {
        static const int gw[] = { 3, 2, 3, 2, 2 };
        int lx = PAD;
        g.setColour(AmbienceColors::Separator);
        for (int gi = 0; gi < 4; ++gi) {
            lx += gw[gi] * (KNOB_W + PAD) + 6;
            g.drawVerticalLine(lx - 3, (float)Y_SLABEL1, (float)(Y_ROW1 + UNIT_H));
        }
        int lx2 = PAD + 2 * (KNOB_W + PAD) + 16;
        g.drawVerticalLine(lx2 - 3, (float)Y_SLABEL2, (float)(Y_ROW2 + UNIT_H));

        g.setColour(AmbienceColors::Accent.withAlpha(0.75f));
        int bx = PAD;
        sl(bx, Y_SLABEL1, "TIME");
        bx += gw[0] * (KNOB_W + PAD) + 6;
        sl(bx, Y_SLABEL1, "FREQUENCY");
        bx += gw[1] * (KNOB_W + PAD) + 6;
        sl(bx, Y_SLABEL1, "DIFFUSION");
        bx += gw[2] * (KNOB_W + PAD) + 6;
        sl(bx, Y_SLABEL1, "STEREO");
        bx += gw[3] * (KNOB_W + PAD) + 6;
        sl(bx, Y_SLABEL1, "CHARACTER");

        bx = PAD;
        sl(bx, Y_SLABEL2, "MIX");
        bx += 2 * (KNOB_W + PAD) + 16;
        sl(bx, Y_SLABEL2, "DUCKING");

    }
    else {
        sl(PAD, Y_SLABEL1, "RT60 PER BAND");

        g.setColour(AmbienceColors::Separator.withAlpha(0.5f));
        g.drawHorizontalLine(Y_SLABEL2 - 4, (float)PAD, (float)(W - PAD));

        // "SAT TYPE" は satTypeLabel コンポーネントが描画するため省略
        // Tilt EQ ラベルのみ paint() で描画
        g.setColour(AmbienceColors::Accent.withAlpha(0.75f));
        const int tiltLabelX = PAD + KNOB_W + PAD + PAD + 8;
        sl(tiltLabelX, Y_SLABEL2, "TILT EQ");
    }
}