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

    titleLabel.setText("AMBIENCE", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions("Helvetica Neue", 14.f, juce::Font::bold)));
    titleLabel.setColour(juce::Label::textColourId, AmbienceColors::TextPrimary);
    addAndMakeVisible(titleLabel);

    addAndMakeVisible(algoSelector);

    // Build Knobs
    auto BK = [&](ArcKnob& k, const char* id, const char* lbl) { k.build(p.apvts, id, lbl, this, laf); };
    BK(kPreDelay, "predelay", "PRE-DELAY"); BK(kRoomSize, "roomsize", "ROOM SIZE"); BK(kDecay, "decaytime", "DECAY");
    BK(kHFDamp, "hfdamping", "HF DAMP"); BK(kLFAbsorb, "lfabsorption", "LF ABSORB");
    BK(kDiffusion, "diffusion", "DIFFUSION"); BK(kModAmt, "modamount", "MOD AMT"); BK(kModRate, "modrate", "MOD RATE");
    BK(kStereoW, "stereowidth", "WIDTH"); BK(kCrossFeed, "crossfeed", "X-FEED");
    BK(kERLevel, "erlevel", "ER LEVEL"); BK(kSaturation, "saturation", "SATURATE");
    BK(kWet, "wetlevel", "WET"); BK(kDry, "drylevel", "DRY");
    BK(kDuckAmt, "duckamount", "AMOUNT"); BK(kDuckThr, "duckthresh", "THRESH"); BK(kDuckAtt, "duckattack", "ATTACK"); BK(kDuckRel, "duckrelease", "RELEASE");

    oversamplingCombo.addItemList({ "1x", "2x", "4x", "8x" }, 1);
    oversamplingCombo.setLookAndFeel(&laf);
    addAndMakeVisible(oversamplingCombo);
    osAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.apvts, "oversampling", oversamplingCombo);

    rt60Viz.setProcessor(&p);
    addAndMakeVisible(rt60Viz);
    addAndMakeVisible(vuIn); addAndMakeVisible(vuOut);

    startTimerHz(60);
}

FDNReverbEditor::~FDNReverbEditor() { stopTimer(); setLookAndFeel(nullptr); }

void FDNReverbEditor::timerCallback() {
    vuIn.setLevels(audioProcessor.getInputRMSL(), audioProcessor.getInputRMSR());
    vuOut.setLevels(audioProcessor.getOutputRMSL(), audioProcessor.getOutputRMSR());
    vuIn.repaint(); vuOut.repaint();
}

void FDNReverbEditor::resized()
{
    titleLabel.setBounds(PAD, Y_HEADER, 180, 32);
    oversamplingCombo.setBounds(W - 76, Y_HEADER + 4, 68, 18);
    vuIn.setBounds(W - 220, Y_HEADER + 2, 96, 28);
    vuOut.setBounds(W - 120, Y_HEADER + 2, 96, 28);
    algoSelector.setBounds(PAD, Y_ALGO, W - PAD * 2, 30);

    auto place = [&](ArcKnob& k, int& x, int y) {
        k.label.setBounds(x, y, KNOB_W, KNOB_LBL_H);
        k.slider.setBounds(x, y + KNOB_LBL_H, KNOB_W, KNOB_H);
        x += KNOB_W + PAD;
        };

    int kx = PAD;
    place(kPreDelay, kx, Y_ROW1); place(kRoomSize, kx, Y_ROW1); place(kDecay, kx, Y_ROW1); kx += 6;
    place(kHFDamp, kx, Y_ROW1); place(kLFAbsorb, kx, Y_ROW1); kx += 6;
    place(kDiffusion, kx, Y_ROW1); place(kModAmt, kx, Y_ROW1); place(kModRate, kx, Y_ROW1); kx += 6;
    place(kStereoW, kx, Y_ROW1); place(kCrossFeed, kx, Y_ROW1); kx += 6;
    place(kERLevel, kx, Y_ROW1); place(kSaturation, kx, Y_ROW1);

    kx = PAD;
    place(kWet, kx, Y_ROW2); place(kDry, kx, Y_ROW2); kx += 16;
    place(kDuckAmt, kx, Y_ROW2); place(kDuckThr, kx, Y_ROW2); place(kDuckAtt, kx, Y_ROW2); place(kDuckRel, kx, Y_ROW2);

    rt60Viz.setBounds(PAD, Y_VIZ, W - PAD * 2, H - Y_VIZ - PAD);
}

void FDNReverbEditor::paint(juce::Graphics& g)
{
    g.fillAll(AmbienceColors::Background);
    juce::ColourGradient grad(AmbienceColors::Surface.withAlpha(0.12f), 0.f, 0.f, AmbienceColors::Background, 0.f, (float)H, false);
    g.setGradientFill(grad);
    g.fillAll();

    // サブタイトル
    g.setFont(juce::Font(juce::FontOptions(8.f)));
    g.setColour(AmbienceColors::TextSecondary.withAlpha(0.35f));
    g.drawText("8ch FDN | SAPF | ISM-ER | 44.1-192kHz | 1-8x OS",
        PAD + 190, Y_HEADER + 10, W / 2, 12, juce::Justification::centredLeft);

    // ── Row 1 のセクション縦区切り線 ──
    static const int gw[] = { 3, 2, 3, 2, 2 };  // グループごとのノブの数
    int lx = PAD;
    g.setColour(AmbienceColors::Separator);
    for (int gi = 0; gi < 4; ++gi) {
        lx += gw[gi] * (KNOB_W + PAD) + 6;
        g.drawVerticalLine(lx - 3, (float)Y_SLABEL1, (float)(Y_ROW1 + UNIT_H));
    }

    // ── Row 2 のセクション縦区切り線 ──
    int lx2 = PAD + 2 * (KNOB_W + PAD) + 16;
    g.drawVerticalLine(lx2 - 3, (float)Y_SLABEL2, (float)(Y_ROW2 + UNIT_H));

    // Visualizer直前の横線
    g.drawHorizontalLine(Y_SEP, (float)PAD, (float)(W - PAD));

    // ── オレンジ色のセクションラベル ──
    g.setFont(juce::Font(juce::FontOptions("Helvetica Neue", 8.5f, juce::Font::bold)));
    g.setColour(AmbienceColors::Accent.withAlpha(0.75f));
    auto sl = [&](int x, int y, const char* t) { g.drawText(t, x, y, 120, 14, juce::Justification::centredLeft); };

    // Row 1 ラベル
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

    // Row 2 ラベル
    bx = PAD;
    sl(bx, Y_SLABEL2, "MIX");
    bx += 2 * (KNOB_W + PAD) + 16;
    sl(bx, Y_SLABEL2, "DUCKING");
}