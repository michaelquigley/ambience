#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace FDNReverb;

FDNReverbAudioProcessor::FDNReverbAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true).withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "FDNReverbState", ParameterHelper::createLayout()) {
}

void FDNReverbAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    int osIdx = (int)*apvts.getRawParameterValue(ParamID::Oversampling);
    oversampler = std::make_unique<juce::dsp::Oversampling<float>>(2, osIdx, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
    oversampler->initProcessing(samplesPerBlock);

    juce::dsp::ProcessSpec spec{ sampleRate * (1 << osIdx), (juce::uint32)(samplesPerBlock * (1 << osIdx)), 2 };
    engine.prepare(spec);
    wetBuffer.setSize(2, samplesPerBlock * (1 << osIdx));
    smoothWetGain.reset(sampleRate, 0.05); smoothDryGain.reset(sampleRate, 0.05);
    lastSampleRate = sampleRate;
}

void FDNReverbAudioProcessor::updateEngineParams() {
    DSPParams p;
    p.algorithmIndex = (int)*apvts.getRawParameterValue(ParamID::Algorithm);
    p.preDelayMs = *apvts.getRawParameterValue(ParamID::PreDelay);
    p.roomSizeScale = *apvts.getRawParameterValue(ParamID::RoomSize) - 0.5f;
    p.decayScale = *apvts.getRawParameterValue(ParamID::DecayTime) / ALL_PRESETS[p.algorithmIndex]->acoustics.rt60[4];
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
    wetBuffer.setSize(2, (int)osBlock.getNumSamples(), false, false, true);

    engine.processBlock(osBlock.getChannelPointer(0), osBlock.getChannelPointer(1), wetBuffer.getWritePointer(0), wetBuffer.getWritePointer(1), (int)osBlock.getNumSamples());

    for (int i = 0; i < (int)osBlock.getNumSamples(); ++i) {
        float w = smoothWetGain.getNextValue(), d = smoothDryGain.getNextValue();
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
    if (xml && xml->hasTagName(apvts.state.getType())) apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorEditor* FDNReverbAudioProcessor::createEditor() { return new FDNReverbEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FDNReverbAudioProcessor(); }