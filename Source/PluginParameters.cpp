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
            juce::StringArray{ "ROOM1", "ROOM2", "HALL1", "HALL2",
                               "PLATE", "SPRING", "GOLDFOIL" }, 0));

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
        addFloat(ParamID::CrossFeed, "Cross-Feed", 0.0f, 1.0f, 0.15f);

        // ─── Character ───
        addFloat(ParamID::ERLevel, "ER Level", 0.0f, 1.0f, 0.6f);
        addFloat(ParamID::Saturation, "Saturation", 0.0f, 1.0f, 0.0f);

        // ─── Saturation Type (ProMode 専用) ───
        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            ParamID::SatType, "Sat Type",
            juce::StringArray{ "Warm", "Tape", "Tube", "Hard" },
            0,
            juce::AudioParameterChoiceAttributes().withAutomatable(false)));

        // ─── Mix ───
        addFloat(ParamID::WetLevel, "Wet", -60.0f, 0.0f, -6.0f, 1.0f, "dB");
        addFloat(ParamID::DryLevel, "Dry", -60.0f, 0.0f, 0.0f, 1.0f, "dB");

        // ─── Ducking ───
        addFloat(ParamID::DuckAmount, "Ducking", 0.0f, 20.0f, 0.0f, 1.0f, "dB");
        addFloat(ParamID::DuckAttack, "Duck Attack", 0.5f, 100.0f, 10.0f, 0.4f, "ms");
        addFloat(ParamID::DuckRelease, "Duck Release", 10.0f, 2000.0f, 200.0f, 0.4f, "ms");
        addFloat(ParamID::DuckThresh, "Duck Thresh", -60.0f, 0.0f, -20.0f, 1.0f, "dB");

        // ─────────────────────────────────────────────────────────────────────
        // ★ Phase 3-1 追加: ER Solo (boolean)
        // ─────────────────────────────────────────────────────────────────────
        //   ER のみを出力するソロボタン。Late (FDN) をミュート。
        //   オートメーション対象外 (ユーザーのモニタリング用)。
        // ─────────────────────────────────────────────────────────────────────
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            ParamID::ERSolo, "ER Solo", false,
            juce::AudioParameterBoolAttributes().withAutomatable(false)));

        // ─────────────────────────────────────────────────────────────────────
        // ★ Phase 4 追加: Pro Mode (boolean)
        // ─────────────────────────────────────────────────────────────────────
        //   通常パネルとプロパネルの切り替え。
        //   オートメーション対象外 (UI 切替用)。
        // ─────────────────────────────────────────────────────────────────────
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            ParamID::ProMode, "Pro Mode", false,
            juce::AudioParameterBoolAttributes().withAutomatable(false)));

        // ─────────────────────────────────────────────────────────────────────
        // ★ Phase 4 追加: Tilt EQ x3 (低・中・高域の RT60 倍率)
        // ─────────────────────────────────────────────────────────────────────
        //   ProMode 時のみ適用される、RT60 カーブのスペクトル整形ノブ。
        //   低域: bands 0-2 (31, 62.5, 125 Hz)
        //   中域: bands 3-6 (250, 500, 1k, 2k Hz)
        //   高域: bands 7-9 (4k, 8k, 16k Hz)
        //
        //   範囲 0.5 ~ 2.0 (1.0 = neutral, 0.5 = 半分, 2.0 = 2倍)
        //   オートメーション対応 (音作りの一部)。
        // ─────────────────────────────────────────────────────────────────────
        addFloat(ParamID::TiltLow, "Tilt Low", 0.5f, 2.0f, 1.0f);
        addFloat(ParamID::TiltMid, "Tilt Mid", 0.5f, 2.0f, 1.0f);
        addFloat(ParamID::TiltHigh, "Tilt High", 0.5f, 2.0f, 1.0f);

        // ─────────────────────────────────────────────────────────────────────
        // ★ Phase 4 追加: RT60 帯域別ノブ x10
        // ─────────────────────────────────────────────────────────────────────
        //   ProMode 時のみ適用される、各オクターブバンドの RT60 倍率。
        //   バンド中心周波数: 31.25, 62.5, 125, 250, 500, 1k, 2k, 4k, 8k, 16k Hz
        //
        //   範囲 0.5 ~ 2.0 (1.0 = neutral)
        //   Tilt EQ の上にさらに乗算される (微調整用)。
        // ─────────────────────────────────────────────────────────────────────
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