#include "PluginParameters.h"

namespace FDNReverb {

    juce::AudioProcessorValueTreeState::ParameterLayout ParameterHelper::createLayout()
    {
        std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

        auto addFloat = [&](const juce::String& id,
            const juce::String& name,
            float min, float max, float def,
            float skew = 1.0f,
            const juce::String& label = "")
            {
                params.push_back(std::make_unique<juce::AudioParameterFloat>(
                    id, name,
                    juce::NormalisableRange<float>(min, max, 0.01f, skew),
                    def,
                    juce::AudioParameterFloatAttributes().withLabel(label)));
            };

        // ─── Algorithm ───
        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            ParamID::Algorithm, "Algorithm",
            juce::StringArray{ "ROOM1","ROOM2","HALL1","HALL2","PLATE","SPRING","GOLDFOIL" }, 0));

        // ─── Time / Frequency ───
        addFloat(ParamID::PreDelay, "Pre-Delay", 0.0f, 500.0f, 10.0f, 1.0f, "ms");
        addFloat(ParamID::RoomSize, "Room Size", 0.3f, 2.0f, 1.0f);
        addFloat(ParamID::DecayTime, "Decay Time", 0.1f, 20.0f, 1.5f, 0.35f, "s");
        addFloat(ParamID::HFDamping, "HF Damping", 0.0f, 1.0f, 0.5f);
        addFloat(ParamID::LFAbsorption, "LF Absorption", 0.0f, 1.0f, 0.5f);

        // ─── Diffusion / Modulation ───
        addFloat(ParamID::Diffusion, "Diffusion", 0.0f, 1.0f, 0.7f);
        addFloat(ParamID::ModAmount, "Mod Amount", 0.0f, 1.0f, 0.25f);
        addFloat(ParamID::ModRate, "Mod Rate", 0.05f, 2.0f, 0.5f, 1.0f, "Hz");

        // ─── Stereo ───
        addFloat(ParamID::StereoWidth, "Stereo Width", 0.0f, 1.0f, 0.8f);
        // ★ CrossFeed 廃止 (v1.2): ノブ不要につき削除

        // ─── Character ───
        addFloat(ParamID::ERLevel, "ER Level", 0.0f, 1.0f, 0.6f);
        addFloat(ParamID::Saturation, "Saturation", 0.0f, 1.0f, 0.0f);

        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            ParamID::SatType, "Sat Type",
            juce::StringArray{ "Warm","Tape","Tube","Hard" },
            0,
            juce::AudioParameterChoiceAttributes().withAutomatable(false)));

        // ─── Mix ───
        // ★ Wet 最大値 -3dB 制限・デフォルト -12dB (v1.2)
        //   Wet 100% (0dB) は FDN の makeup ゲインと合わさり出力が飽和するため、
        //   最大値を -3dB に制限して OutputLimiter の連続作動を防ぐ。
        //   デフォルト -12dB はインサート用途の標準値。
        addFloat(ParamID::WetLevel, "Wet", -60.0f, -3.0f, -12.0f, 1.0f, "dB");
        addFloat(ParamID::DryLevel, "Dry", -60.0f, 0.0f, 0.0f, 1.0f, "dB");

        // ─── Ducking ───
        addFloat(ParamID::DuckAmount, "Ducking", 0.0f, 20.0f, 0.0f, 1.0f, "dB");
        addFloat(ParamID::DuckAttack, "Duck Attack", 0.5f, 100.0f, 10.0f, 0.4f, "ms");
        addFloat(ParamID::DuckRelease, "Duck Release", 10.0f, 2000.0f, 200.0f, 0.4f, "ms");
        addFloat(ParamID::DuckThresh, "Duck Thresh", -60.0f, 0.0f, -20.0f, 1.0f, "dB");

        // ─── ER Solo / ProMode ───
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            ParamID::ERSolo, "ER Solo", false,
            juce::AudioParameterBoolAttributes().withAutomatable(false)));
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            ParamID::ProMode, "Pro Mode", false,
            juce::AudioParameterBoolAttributes().withAutomatable(false)));

        // ─── ProMode: Tilt EQ ───
        addFloat(ParamID::TiltLow, "Tilt Low", 0.5f, 2.0f, 1.0f);
        addFloat(ParamID::TiltMid, "Tilt Mid", 0.5f, 2.0f, 1.0f);
        addFloat(ParamID::TiltHigh, "Tilt High", 0.5f, 2.0f, 1.0f);

        // ─── ProMode: RT60 帯域別ノブ × 10 ───
        addFloat(ParamID::RTBand0, "RT 31Hz", 0.5f, 2.0f, 1.0f);
        addFloat(ParamID::RTBand1, "RT 62Hz", 0.5f, 2.0f, 1.0f);
        addFloat(ParamID::RTBand2, "RT 125Hz", 0.5f, 2.0f, 1.0f);
        addFloat(ParamID::RTBand3, "RT 250Hz", 0.5f, 2.0f, 1.0f);
        addFloat(ParamID::RTBand4, "RT 500Hz", 0.5f, 2.0f, 1.0f);
        addFloat(ParamID::RTBand5, "RT 1kHz", 0.5f, 2.0f, 1.0f);
        addFloat(ParamID::RTBand6, "RT 2kHz", 0.5f, 2.0f, 1.0f);
        addFloat(ParamID::RTBand7, "RT 4kHz", 0.5f, 2.0f, 1.0f);
        addFloat(ParamID::RTBand8, "RT 8kHz", 0.5f, 2.0f, 1.0f);
        addFloat(ParamID::RTBand9, "RT 16kHz", 0.5f, 2.0f, 1.0f);

        return { params.begin(), params.end() };
    }

} // namespace FDNReverb