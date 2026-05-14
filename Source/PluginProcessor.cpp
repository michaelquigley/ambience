#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace FDNReverb;

FDNReverbAudioProcessor::FDNReverbAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "FDNReverbState", ParameterHelper::createLayout())
{
}

void FDNReverbAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    int osIdx = 0;
    oversampler = std::make_unique<juce::dsp::Oversampling<float>>(
        2, osIdx,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true);
    oversampler->initProcessing(static_cast<size_t>(samplesPerBlock));

    double osSampleRate = sampleRate;
    int osBlockSize = samplesPerBlock;

    engine.prepare(osSampleRate, osBlockSize);

    wetBuffer.setSize(2, osBlockSize);
    smoothWetGain.reset(sampleRate, 0.05);
    smoothDryGain.reset(sampleRate, 0.05);

    lastSampleRate = sampleRate;
}

void FDNReverbAudioProcessor::updateEngineParams() {
    int currentAlgo = (int)*apvts.getRawParameterValue(ParamID::Algorithm);
    if (currentAlgo != lastAlgorithmIndex) {
        if (lastAlgorithmIndex >= 0) {
            loadPresetDefaults(currentAlgo);
        }
        lastAlgorithmIndex = currentAlgo;
    }

    DSPParams p;
    p.algorithmIndex = (int)*apvts.getRawParameterValue(ParamID::Algorithm);
    p.preDelayMs = *apvts.getRawParameterValue(ParamID::PreDelay);
    p.roomSizeScale = *apvts.getRawParameterValue(ParamID::RoomSize) - 0.5f;

    p.decayScale = *apvts.getRawParameterValue(ParamID::DecayTime)
        / ALL_PRESETS[p.algorithmIndex]->acoustics.rt60[4];

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

    p.satTypeIdx = (int)*apvts.getRawParameterValue(ParamID::SatType);

    // ─── Phase 3-1 追加: ER Solo ───
    p.erSolo = (*apvts.getRawParameterValue(ParamID::ERSolo)) > 0.5f;

    // ─── Phase 4 追加: ProMode + Tilt EQ + 帯域別 RT60 ───
    p.proMode = (*apvts.getRawParameterValue(ParamID::ProMode)) > 0.5f;
    p.tiltLow = *apvts.getRawParameterValue(ParamID::TiltLow);
    p.tiltMid = *apvts.getRawParameterValue(ParamID::TiltMid);
    p.tiltHigh = *apvts.getRawParameterValue(ParamID::TiltHigh);

    p.rtBands[0] = *apvts.getRawParameterValue(ParamID::RTBand0);
    p.rtBands[1] = *apvts.getRawParameterValue(ParamID::RTBand1);
    p.rtBands[2] = *apvts.getRawParameterValue(ParamID::RTBand2);
    p.rtBands[3] = *apvts.getRawParameterValue(ParamID::RTBand3);
    p.rtBands[4] = *apvts.getRawParameterValue(ParamID::RTBand4);
    p.rtBands[5] = *apvts.getRawParameterValue(ParamID::RTBand5);
    p.rtBands[6] = *apvts.getRawParameterValue(ParamID::RTBand6);
    p.rtBands[7] = *apvts.getRawParameterValue(ParamID::RTBand7);
    p.rtBands[8] = *apvts.getRawParameterValue(ParamID::RTBand8);
    p.rtBands[9] = *apvts.getRawParameterValue(ParamID::RTBand9);

    smoothWetGain.setTargetValue(juce::Decibels::decibelsToGain(p.wetDB));
    smoothDryGain.setTargetValue(juce::Decibels::decibelsToGain(p.dryDB));

    engine.setParams(p);
}

void FDNReverbAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;

    updateEngineParams();

    inputRMS_L.store(buffer.getRMSLevel(0, 0, buffer.getNumSamples()));
    inputRMS_R.store(buffer.getRMSLevel(1, 0, buffer.getNumSamples()));

    juce::dsp::AudioBlock<float> block(buffer);
    auto osBlock = oversampler->processSamplesUp(block);
    int numSamples = static_cast<int>(osBlock.getNumSamples());

    wetBuffer.setSize(2, numSamples, false, false, true);

    engine.processBlock(osBlock.getChannelPointer(0), osBlock.getChannelPointer(1),
        wetBuffer.getWritePointer(0), wetBuffer.getWritePointer(1),
        numSamples);

    for (int i = 0; i < numSamples; ++i) {
        float w = smoothWetGain.getNextValue();
        float d = smoothDryGain.getNextValue();

        osBlock.setSample(0, i, osBlock.getSample(0, i) * d + wetBuffer.getSample(0, i) * w);
        osBlock.setSample(1, i, osBlock.getSample(1, i) * d + wetBuffer.getSample(1, i) * w);
    }

    oversampler->processSamplesDown(block);

    outputRMS_L.store(buffer.getRMSLevel(0, 0, buffer.getNumSamples()));
    outputRMS_R.store(buffer.getRMSLevel(1, 0, buffer.getNumSamples()));
}

void FDNReverbAudioProcessor::getStateInformation(juce::MemoryBlock& d) {
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, d);
}

void FDNReverbAudioProcessor::setStateInformation(const void* d, int s) {
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(d, s));
    if (xml && xml->hasTagName(apvts.state.getType())) {
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
    }
}

juce::AudioProcessorEditor* FDNReverbAudioProcessor::createEditor() {
    return new FDNReverbEditor(*this);
}

void FDNReverbAudioProcessor::loadPresetDefaults(int algorithmIndex) {
    if (algorithmIndex < 0 || algorithmIndex >= 7) return;

    const auto& def = PRESET_DEFAULTS[algorithmIndex];

    auto setParam = [this](const juce::String& paramID, float value) {
        if (auto* param = apvts.getParameter(paramID)) {
            float normalizedValue = param->convertTo0to1(value);
            param->setValueNotifyingHost(normalizedValue);
        }
        };

    setParam(ParamID::RoomSize, def.roomSize);
    setParam(ParamID::DecayTime, def.decayTime);
    setParam(ParamID::HFDamping, def.hfDamp);
    setParam(ParamID::LFAbsorption, def.lfAbsorb);
    setParam(ParamID::Diffusion, def.diffusion);
    setParam(ParamID::ModAmount, def.modAmount);
    setParam(ParamID::ModRate, def.modRate);
    setParam(ParamID::ERLevel, def.erLevel);
    setParam(ParamID::Saturation, def.saturation);  // saturation も同期
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new FDNReverbAudioProcessor();
}