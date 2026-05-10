#include "PluginParameters.h"

namespace FDNReverb {

    juce::AudioProcessorValueTreeState::ParameterLayout ParameterHelper::createLayout()
    {
        std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

        auto addFloat = [&](const juce::String& id, const juce::String& name, float min, float max, float def, float skew = 1.0f, const juce::String& label = "") {
            params.push_back(std::make_unique<juce::AudioParameterFloat>(
                id, name, juce::NormalisableRange<float>(min, max, 0.01f, skew), def, juce::AudioParameterFloatAttributes().withLabel(label)));
            };

        params.push_back(std::make_unique<juce::AudioParameterChoice>(ParamID::Algorithm, "Algorithm", juce::StringArray{ "ROOM1","ROOM2","HALL1","HALL2","PLATE","SPRING","GOLDFOIL" }, 0));

        addFloat(ParamID::PreDelay, "Pre-Delay", 0.f, 500.f, 10.f, 1.0f, "ms");
        addFloat(ParamID::RoomSize, "Room Size", 0.3f, 2.0f, 1.0f);
        addFloat(ParamID::DecayTime, "Decay Time", 0.1f, 20.f, 1.5f, 0.35f, "s");
        addFloat(ParamID::HFDamping, "HF Damping", 0.f, 1.f, 0.5f);
        addFloat(ParamID::LFAbsorption, "LF Absorption", 0.f, 1.f, 0.5f);
        addFloat(ParamID::Diffusion, "Diffusion", 0.f, 1.f, 0.7f);
        addFloat(ParamID::ModAmount, "Mod Amount", 0.f, 1.f, 0.25f);
        addFloat(ParamID::ModRate, "Mod Rate", 0.05f, 2.f, 0.5f, 1.0f, "Hz");
        addFloat(ParamID::StereoWidth, "Stereo Width", 0.f, 1.f, 0.8f);
        addFloat(ParamID::CrossFeed, "Cross-Feed", 0.f, 1.f, 0.15f);
        addFloat(ParamID::ERLevel, "ER Level", 0.f, 1.f, 0.6f);
        addFloat(ParamID::Saturation, "Saturation", 0.f, 1.f, 0.0f);
        addFloat(ParamID::WetLevel, "Wet", -60.f, 0.f, -6.f, 1.0f, "dB");
        addFloat(ParamID::DryLevel, "Dry", -60.f, 0.f, 0.f, 1.0f, "dB");
        addFloat(ParamID::DuckAmount, "Ducking", 0.f, 20.f, 0.f, 1.0f, "dB");
        addFloat(ParamID::DuckAttack, "Duck Attack", 0.5f, 100.f, 10.f, 0.4f, "ms");
        addFloat(ParamID::DuckRelease, "Duck Release", 10.f, 2000.f, 200.f, 0.4f, "ms");
        addFloat(ParamID::DuckThresh, "Duck Thresh", -60.f, 0.f, -20.f, 1.0f, "dB");

        params.push_back(std::make_unique<juce::AudioParameterChoice>(ParamID::Oversampling, "Oversampling", juce::StringArray{ "1x","2x","4x","8x" }, 0));

        return { params.begin(), params.end() };
    }

} // namespace FDNReverb