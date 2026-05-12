#include "PluginProcessor.h"
#include "PluginEditor.h"

// Y-座標の計算
static constexpr int Y_HEADER = 8;
static constexpr int Y_ALGO = 48;
static constexpr int Y_SLABEL1 = 86;
static constexpr int Y_ROW1 = 104;
static constexpr int Y_SLABEL2 = 204;
static constexpr int Y_ROW2 = 222;
static constexpr int Y_SEP = 322;
static constexpr int Y_VIZ = 326;

FDNReverbEditor::FDNReverbEditor(FDNReverbAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p), algoSelector(p.apvts),
    vuIn("IN", VUMeter::Side::Input), vuOut("OUT", VUMeter::Side::Output)
{
    setLookAndFeel(&laf);
    setSize(W, H);

    // ─── Title Label ───
    titleLabel.setText("AMBIENCE", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions("Helvetica Neue", 14.f, juce::Font::bold)));
    titleLabel.setColour(juce::Label::textColourId, AmbienceColors::TextPrimary);
    addAndMakeVisible(titleLabel);

    // ─── Algorithm Selector ───
    addAndMakeVisible(algoSelector);

    // ─── Build Knobs ───
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

    // ────────────────────────────────────────────────────────────────────────
    //  ★ 削除: oversamplingCombo, osAttachment
    //    （v1.1.0 で Oversampling パラメータを廃止）
    // ────────────────────────────────────────────────────────────────────────
    // oversamplingCombo.addItemList({ "1x", "2x", "4x", "8x" }, 1);
    // oversamplingCombo.setLookAndFeel(&laf);
    // addAndMakeVisible(oversamplingCombo);
    // osAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
    //     p.apvts, "oversampling", oversamplingCombo);

    // ─── Visualizers ───
    rt60Viz.setProcessor(&p);
    decayCurveViz.setProcessor(&p);
    addAndMakeVisible(decayCurveViz);
    addAndMakeVisible(rt60Viz);

    // ─── VU Meters ───
    addAndMakeVisible(vuIn);
    addAndMakeVisible(vuOut);

    // ────────────────────────────────────────────────────────────────────────
    //  AcousticMetrics ラベル群の初期化
    // ────────────────────────────────────────────────────────────────────────
    labelMetricsTitle.setText("ACOUSTICS", juce::dontSendNotification);
    labelMetricsTitle.setFont(juce::Font(juce::FontOptions("Helvetica Neue", 8.5f, juce::Font::bold)));
    labelMetricsTitle.setColour(juce::Label::textColourId, AmbienceColors::Accent.withAlpha(0.75f));
    labelMetricsTitle.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(labelMetricsTitle);

    auto setupCaption = [this](juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        label.setFont(juce::Font(juce::FontOptions("Helvetica Neue", 8.0f, juce::Font::plain)));
        label.setColour(juce::Label::textColourId, AmbienceColors::TextSecondary.withAlpha(0.85f));
        label.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(label);
        };

    auto setupValue = [this](juce::Label& label) {
        label.setText("--", juce::dontSendNotification);
        label.setFont(juce::Font(juce::FontOptions("Helvetica Neue", 9.5f, juce::Font::bold)));
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

    startTimerHz(60);
}

FDNReverbEditor::~FDNReverbEditor() {
    stopTimer();
    setLookAndFeel(nullptr);
}

void FDNReverbEditor::timerCallback() {
    vuIn.setLevels(audioProcessor.getInputRMSL(), audioProcessor.getInputRMSR());
    vuOut.setLevels(audioProcessor.getOutputRMSL(), audioProcessor.getOutputRMSR());
    vuIn.repaint();
    vuOut.repaint();

    static int metricsCounter = 0;
    if (++metricsCounter >= 2) {
        metricsCounter = 0;
        float d50 = audioProcessor.getD50();
        float c50 = audioProcessor.getC50();
        float c80 = audioProcessor.getC80();
        float edt = audioProcessor.getEDT();

        labelD50Value.setText(juce::String(d50 * 100.0f, 1) + "%",
            juce::dontSendNotification);
        labelC50Value.setText(juce::String(c50, 1) + "dB",
            juce::dontSendNotification);
        labelC80Value.setText(juce::String(c80, 1) + "dB",
            juce::dontSendNotification);
        labelEDTValue.setText(juce::String(edt, 2) + "s",
            juce::dontSendNotification);
    }
}

void FDNReverbEditor::resized()
{
    titleLabel.setBounds(PAD, Y_HEADER, 180, 32);

    // ────────────────────────────────────────────────────────────────────────
    //  ★ 削除: oversamplingCombo.setBounds(...)
    //    （v1.1.0 で Oversampling パラメータを廃止）
    // ────────────────────────────────────────────────────────────────────────
    // oversamplingCombo.setBounds(W - 76, Y_HEADER + 4, 68, 18);

    vuIn.setBounds(W - 220, Y_HEADER + 2, 96, 28);
    vuOut.setBounds(W - 120, Y_HEADER + 2, 96, 28);

    algoSelector.setBounds(PAD, Y_ALGO, W - PAD * 2, 30);

    // ─── Knob Layout ───
    auto place = [&](ArcKnob& k, int& x, int y) {
        k.label.setBounds(x, y, KNOB_W, KNOB_LBL_H);
        k.slider.setBounds(x, y + KNOB_LBL_H, KNOB_W, KNOB_H);
        x += KNOB_W + PAD;
        };

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

    // ─── 2つのグラフを上下に分割配置 ───
    // 上半分: 既存のRT60グラフ（周波数軸）
    // 下半分: 新規Decayグラフ（時間軸）
    const int vizTotalH = H - Y_VIZ - PAD;        // 利用可能な総高さ
    const int rt60Height = vizTotalH / 2 - 2;     // 上半分（隙間2px）
    const int decayHeight = vizTotalH / 2 - 2;    // 下半分
    const int decayY = Y_VIZ + rt60Height + 4;    // 4pxの隙間

    rt60Viz.setBounds(PAD, Y_VIZ, W - PAD * 2, rt60Height);
    decayCurveViz.setBounds(PAD, decayY, W - PAD * 2, decayHeight);

    // ─── AcousticMetrics ラベル配置（グラフの右上に配置） ───
    const int metricsRight = W - PAD - 8;
    const int metricsW = 280;
    const int metricsLeft = metricsRight - metricsW;
    const int metricsTop = decayY + 6;
    labelMetricsTitle.setBounds(metricsLeft, metricsTop, 80, 12);

    const int metricsRowH = 14;
    const int metricsRow1Y = metricsTop + 14;
    const int metricsRow2Y = metricsRow1Y + metricsRowH;
    const int captionW = 28;
    const int valueW = 52;
    const int colSpacing = 8;
    const int colW = captionW + valueW;

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

    juce::ColourGradient grad(AmbienceColors::Surface.withAlpha(0.12f), 0.f, 0.f,
        AmbienceColors::Background, 0.f, (float)H, false);
    g.setGradientFill(grad);
    g.fillAll();

    // ─── Background Footer Text ───
    g.setFont(juce::Font(juce::FontOptions(8.f)));
    g.setColour(AmbienceColors::TextSecondary.withAlpha(0.35f));
    // ★ 修正: "8ch FDN" → "16ch FDN"、"1-8x OS" を削除
    g.drawText("16ch FDN | SAPF | ISM-ER | 44.1-192kHz",
        PAD + 190, Y_HEADER + 10, W / 2, 12, juce::Justification::centredLeft);

    // ─── Vertical Separator Lines ───
    static const int gw[] = { 3, 2, 3, 2, 2 };
    int lx = PAD;
    g.setColour(AmbienceColors::Separator);

    for (int gi = 0; gi < 4; ++gi) {
        lx += gw[gi] * (KNOB_W + PAD) + 6;
        g.drawVerticalLine(lx - 3, (float)Y_SLABEL1, (float)(Y_ROW1 + UNIT_H));
    }

    int lx2 = PAD + 2 * (KNOB_W + PAD) + 16;
    g.drawVerticalLine(lx2 - 3, (float)Y_SLABEL2, (float)(Y_ROW2 + UNIT_H));

    g.drawHorizontalLine(Y_SEP, (float)PAD, (float)(W - PAD));

    // ─── Section Labels ───
    g.setFont(juce::Font(juce::FontOptions("Helvetica Neue", 8.5f, juce::Font::bold)));
    g.setColour(AmbienceColors::Accent.withAlpha(0.75f));

    auto sl = [&](int x, int y, const char* t) {
        g.drawText(t, x, y, 120, 14, juce::Justification::centredLeft);
        };

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