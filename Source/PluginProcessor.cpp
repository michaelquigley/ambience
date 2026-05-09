// ============================================================
//  PluginProcessor.cpp
// ============================================================
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace FDNReverb;

// ─────────────────────────────────────────────────────────────────────────────
//  Parameter layout
// ─────────────────────────────────────────────────────────────────────────────
juce::AudioProcessorValueTreeState::ParameterLayout
FDNReverbAudioProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Algorithm  0-6
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        ParamID::Algorithm, "Algorithm",
        juce::StringArray{ "ROOM1","ROOM2","HALL1","HALL2","PLATE","SPRING","GOLDFOIL" },
        0));

    // Pre-Delay  0–500 ms
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamID::PreDelay, "Pre-Delay",
        juce::NormalisableRange<float>(0.f, 500.f, 0.1f), 10.f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    // Room Size  0.3–2.0
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamID::RoomSize, "Room Size",
        juce::NormalisableRange<float>(0.3f, 2.0f, 0.01f), 1.0f));

    // Decay Time  0.1–20 s  (logarithmic)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamID::DecayTime, "Decay Time",
        juce::NormalisableRange<float>(0.1f, 20.f, 0.01f, 0.35f),  // skew 0.35 = log
        1.5f,
        juce::AudioParameterFloatAttributes().withLabel("s")));

    // HF Damping  0–1
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamID::HFDamping, "HF Damping",
        juce::NormalisableRange<float>(0.f, 1.f, 0.001f), 0.5f));

    // LF Absorption  0–1
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamID::LFAbsorption, "LF Absorption",
        juce::NormalisableRange<float>(0.f, 1.f, 0.001f), 0.5f));

    // Diffusion  0–1
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamID::Diffusion, "Diffusion",
        juce::NormalisableRange<float>(0.f, 1.f, 0.001f), 0.7f));

    // Modulation Amount  0–1
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamID::ModAmount, "Mod Amount",
        juce::NormalisableRange<float>(0.f, 1.f, 0.001f), 0.25f));

    // Modulation Rate  0.05–2 Hz
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamID::ModRate, "Mod Rate",
        juce::NormalisableRange<float>(0.05f, 2.f, 0.01f, 0.5f),
        0.5f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    // Stereo Width  0–1
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamID::StereoWidth, "Stereo Width",
        juce::NormalisableRange<float>(0.f, 1.f, 0.001f), 0.8f));

    // Cross-Feed  0–1
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamID::CrossFeed, "Cross-Feed",
        juce::NormalisableRange<float>(0.f, 1.f, 0.001f), 0.15f));

    // ER Level  0–1
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamID::ERLevel, "ER Level",
        juce::NormalisableRange<float>(0.f, 1.f, 0.001f), 0.6f));

    // Saturation  0–1
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamID::Saturation, "Saturation",
        juce::NormalisableRange<float>(0.f, 1.f, 0.001f), 0.0f));

    // Wet Level  -60–0 dB
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamID::WetLevel, "Wet",
        juce::NormalisableRange<float>(-60.f, 0.f, 0.1f),
        -6.f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    // Dry Level  -60–0 dB
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamID::DryLevel, "Dry",
        juce::NormalisableRange<float>(-60.f, 0.f, 0.1f),
        0.f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    // Ducking Amount  0–20 dB
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamID::DuckAmount, "Ducking",
        juce::NormalisableRange<float>(0.f, 20.f, 0.1f), 0.f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    // Ducking Attack  0.5–100 ms
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamID::DuckAttack, "Duck Attack",
        juce::NormalisableRange<float>(0.5f, 100.f, 0.1f, 0.4f),
        10.f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    // Ducking Release  10–2000 ms
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamID::DuckRelease, "Duck Release",
        juce::NormalisableRange<float>(10.f, 2000.f, 1.f, 0.4f),
        200.f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    // Ducking Threshold  -60–0 dB
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParamID::DuckThresh, "Duck Thresh",
        juce::NormalisableRange<float>(-60.f, 0.f, 0.5f),
        -20.f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    // Oversampling  0=1x 1=2x 2=4x 3=8x
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        ParamID::Oversampling, "Oversampling",
        juce::StringArray{ "1x","2x","4x","8x" }, 0));

    return { params.begin(), params.end() };
}

// ─────────────────────────────────────────────────────────────────────────────
//  Constructor
// ─────────────────────────────────────────────────────────────────────────────
FDNReverbAudioProcessor::FDNReverbAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "FDNReverbState", createLayout())
{
}

// ─────────────────────────────────────────────────────────────────────────────
//  prepareToPlay
// ─────────────────────────────────────────────────────────────────────────────
void FDNReverbAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // Check if OS factor changed
    int osIdx = (int)*apvts.getRawParameterValue(ParamID::Oversampling);
    int osFactor = 1 << osIdx;   // 1,2,4,8

    if (!oversampler || osFactor != currentOSFactor || sampleRate != lastSampleRate) {
        currentOSFactor = osFactor;
        currentOSIndex = osIdx;
        oversampler = std::make_unique<juce::dsp::Oversampling<float>>(
            2, osIdx,
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
        oversampler->initProcessing(static_cast<size_t>(samplesPerBlock));
    }

    double osSampleRate = sampleRate * currentOSFactor;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = osSampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock * currentOSFactor);
    spec.numChannels = 2;
    engine.prepare(spec);

    // Pre-allocate working buffers
    dryBuffer.setSize(2, samplesPerBlock * currentOSFactor, false, true, true);
    wetBuffer.setSize(2, samplesPerBlock * currentOSFactor, false, true, true);

    // Smooth dry/wet (50 ms ramp)
    smoothWetGain.reset(sampleRate, 0.05);
    smoothDryGain.reset(sampleRate, 0.05);
    smoothWetGain.setCurrentAndTargetValue(0.5f);
    smoothDryGain.setCurrentAndTargetValue(1.0f);

    // RMS decay: ~300 ms
    rmsDecay = std::exp(-1.f / (0.3f * (float)sampleRate));

    lastSampleRate = sampleRate;
    lastBlockSize = samplesPerBlock;

    updateEngineParams();
}

// ─────────────────────────────────────────────────────────────────────────────
//  releaseResources
// ─────────────────────────────────────────────────────────────────────────────
void FDNReverbAudioProcessor::releaseResources() {
    engine.reset();
}

// ─────────────────────────────────────────────────────────────────────────────
//  updateEngineParams  (builds DSPParams and pushes to engine)
// ─────────────────────────────────────────────────────────────────────────────
void FDNReverbAudioProcessor::updateEngineParams()
{
    FDNReverb::DSPParams p;
    p.algorithmIndex = (int)*apvts.getRawParameterValue(ParamID::Algorithm);
    p.preDelayMs = *apvts.getRawParameterValue(ParamID::PreDelay);
    p.roomSizeScale = *apvts.getRawParameterValue(ParamID::RoomSize) - 0.5f;  // 0-1.5
    p.decayScale = *apvts.getRawParameterValue(ParamID::DecayTime)
        / FDNReverb::ALL_PRESETS[p.algorithmIndex]->acoustics.rt60[4];
    p.hfDamping = *apvts.getRawParameterValue(ParamID::HFDamping);
    p.lfAbsorption = *apvts.getRawParameterValue(ParamID::LFAbsorption);
    p.diffusion = *apvts.getRawParameterValue(ParamID::Diffusion);
    p.modAmount = *apvts.getRawParameterValue(ParamID::ModAmount);
    p.modRate = *apvts.getRawParameterValue(ParamID::ModRate);
    p.stereoWidth = *apvts.getRawParameterValue(ParamID::StereoWidth);
    p.crossFeed = *apvts.getRawParameterValue(ParamID::CrossFeed);
    p.erLevel = *apvts.getRawParameterValue(ParamID::ERLevel);
    p.saturation = *apvts.getRawParameterValue(ParamID::Saturation);
    p.wetDB = *apvts.getRawParameterValue(ParamID::WetLevel);
    p.dryDB = *apvts.getRawParameterValue(ParamID::DryLevel);
    p.duckingAmount = *apvts.getRawParameterValue(ParamID::DuckAmount);
    p.duckingAttackMs = *apvts.getRawParameterValue(ParamID::DuckAttack);
    p.duckingRelMs = *apvts.getRawParameterValue(ParamID::DuckRelease);
    p.duckingThreshDB = *apvts.getRawParameterValue(ParamID::DuckThresh);

    // Wet/dry gains (for AudioProcessor-level mix)
    smoothWetGain.setTargetValue(juce::Decibels::decibelsToGain(p.wetDB));
    smoothDryGain.setTargetValue(juce::Decibels::decibelsToGain(p.dryDB));

    engine.setParams(p);
}

// ─────────────────────────────────────────────────────────────────────────────
//  processBlock
// ─────────────────────────────────────────────────────────────────────────────
void FDNReverbAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Always update params (cheap pointer read from APVTS atomic floats)
    updateEngineParams();

    auto* chanL = buffer.getReadPointer(0);
    auto* chanR = buffer.getReadPointer(1);
    int   N = buffer.getNumSamples();

    // ── RMS input metering (before processing) ────────────────────────────
    float rmsL_in = 0.f, rmsR_in = 0.f;
    for (int i = 0; i < N; ++i) {
        rmsL_in += chanL[i] * chanL[i];
        rmsR_in += chanR[i] * chanR[i];
    }
    inputRMS_L.store(std::sqrt(rmsL_in / N));
    inputRMS_R.store(std::sqrt(rmsR_in / N));

    // ── Oversampling upsample ──────────────────────────────────────────────
    juce::dsp::AudioBlock<float> inputBlock(buffer);
    auto oversampledBlock = oversampler->processSamplesUp(inputBlock);

    int osN = (int)oversampledBlock.getNumSamples();
    auto* osL = oversampledBlock.getChannelPointer(0);
    auto* osR = oversampledBlock.getChannelPointer(1);

    // Ensure wet buffer is large enough
    wetBuffer.setSize(2, osN, false, false, true);
    auto* wL = wetBuffer.getWritePointer(0);
    auto* wR = wetBuffer.getWritePointer(1);

    // ── FDN engine process ────────────────────────────────────────────────
    engine.processBlock(osL, osR, wL, wR, osN);

    // ── Mix wet into oversampled block ────────────────────────────────────
    for (int i = 0; i < osN; ++i) {
        float wet = smoothWetGain.getNextValue();
        float dry = smoothDryGain.getNextValue();
        oversampledBlock.setSample(0, i, osL[i] * dry + wL[i] * wet);
        oversampledBlock.setSample(1, i, osR[i] * dry + wR[i] * wet);
    }

    // ── Downsample ────────────────────────────────────────────────────────
    oversampler->processSamplesDown(inputBlock);

    // ── RMS output metering ───────────────────────────────────────────────
    chanL = buffer.getReadPointer(0);
    chanR = buffer.getReadPointer(1);
    float rmsL_out = 0.f, rmsR_out = 0.f;
    for (int i = 0; i < N; ++i) {
        rmsL_out += chanL[i] * chanL[i];
        rmsR_out += chanR[i] * chanR[i];
    }
    outputRMS_L.store(std::sqrt(rmsL_out / N));
    outputRMS_R.store(std::sqrt(rmsR_out / N));
}

void FDNReverbAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>&,
    juce::MidiBuffer&) {
}

// ─────────────────────────────────────────────────────────────────────────────
//  State save / restore
// ─────────────────────────────────────────────────────────────────────────────
void FDNReverbAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void FDNReverbAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Editor
// ─────────────────────────────────────────────────────────────────────────────
juce::AudioProcessorEditor* FDNReverbAudioProcessor::createEditor() {
    return new FDNReverbEditor(*this);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Plugin entry point
// ─────────────────────────────────────────────────────────────────────────────
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new FDNReverbAudioProcessor();
}