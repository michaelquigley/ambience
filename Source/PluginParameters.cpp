#include "PluginParameters.h"

namespace FDNReverb {

    juce::AudioProcessorValueTreeState::ParameterLayout ParameterHelper::createLayout()
    {
        std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

        // 連続値パラメータ用のヘルパー (skew で対数カーブ等を指定可能)
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

        // ─── Algorithm (列挙型) ───────────────────────────────────────────
        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            ParamID::Algorithm, "Algorithm",
            juce::StringArray{ "ROOM1", "ROOM2", "HALL1", "HALL2",
                               "PLATE", "SPRING", "GOLDFOIL" }, 0));

        // ─── Time / Frequency ─────────────────────────────────────────────
        addFloat(ParamID::PreDelay, "Pre-Delay", 0.0f, 500.0f, 10.0f, 1.0f, "ms");
        addFloat(ParamID::RoomSize, "Room Size", 0.3f, 2.0f, 1.0f);
        addFloat(ParamID::DecayTime, "Decay Time", 0.1f, 20.0f, 1.5f, 0.35f, "s");
        addFloat(ParamID::HFDamping, "HF Damping", 0.0f, 1.0f, 0.5f);
        addFloat(ParamID::LFAbsorption, "LF Absorption", 0.0f, 1.0f, 0.5f);

        // ─── Diffusion / Modulation ───────────────────────────────────────
        addFloat(ParamID::Diffusion, "Diffusion", 0.0f, 1.0f, 0.7f);
        addFloat(ParamID::ModAmount, "Mod Amount", 0.0f, 1.0f, 0.25f);
        addFloat(ParamID::ModRate, "Mod Rate", 0.05f, 2.0f, 0.5f, 1.0f, "Hz");

        // ─── Stereo ───────────────────────────────────────────────────────
        addFloat(ParamID::StereoWidth, "Stereo Width", 0.0f, 1.0f, 0.8f);
        addFloat(ParamID::CrossFeed, "Cross-Feed", 0.0f, 1.0f, 0.15f);

        // ─── Character / Saturation ───────────────────────────────────────
        addFloat(ParamID::ERLevel, "ER Level", 0.0f, 1.0f, 0.6f);
        addFloat(ParamID::Saturation, "Saturation", 0.0f, 1.0f, 0.0f);

        // ─── ★ Saturation Type (ProMode 専用、4 種から選択) ───
        // Ableton Live 等の VST3 ホストでオートメーション対象から
        // 除外したいので withAutomatable(false) を付与する。
        // (ProMode のみで操作可能、Normal Mode では Warm 固定運用)
        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            ParamID::SatType, "Sat Type",
            juce::StringArray{ "Warm", "Tape", "Tube", "Hard" },
            0,
            juce::AudioParameterChoiceAttributes().withAutomatable(false)));

        // ─── Mix ──────────────────────────────────────────────────────────
        addFloat(ParamID::WetLevel, "Wet", -60.0f, 0.0f, -6.0f, 1.0f, "dB");
        addFloat(ParamID::DryLevel, "Dry", -60.0f, 0.0f, 0.0f, 1.0f, "dB");

        // ─── Ducking ──────────────────────────────────────────────────────
        addFloat(ParamID::DuckAmount, "Ducking", 0.0f, 20.0f, 0.0f, 1.0f, "dB");
        addFloat(ParamID::DuckAttack, "Duck Attack", 0.5f, 100.0f, 10.0f, 0.4f, "ms");
        addFloat(ParamID::DuckRelease, "Duck Release", 10.0f, 2000.0f, 200.0f, 0.4f, "ms");
        addFloat(ParamID::DuckThresh, "Duck Thresh", -60.0f, 0.0f, -20.0f, 1.0f, "dB");

        // ─── 廃止 (v1.1.0) ─────────────────────────────────────────
        // Oversampling は FDN ループの HF Damping により不要と判断。
        // 旧プロジェクトファイルとの互換性のため ID 文字列は
        // 完全に削除せず、ここにコメントとして残す。
        //
        // params.push_back(std::make_unique<juce::AudioParameterChoice>(
        //     "oversampling", "Oversampling",
        //     juce::StringArray{ "1x", "2x", "4x", "8x" }, 0));
        // ───────────────────────────────────────────────────────────

        return { params.begin(), params.end() };
    }

} // namespace FDNReverb