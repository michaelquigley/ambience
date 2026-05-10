#pragma once
#include <JuceHeader.h>

namespace FDNReverb {

    class SAPFStage {
    public:
        void prepare(const juce::dsp::ProcessSpec& spec, int delayTargetSamples);
        void setGain(float g) noexcept { gain = juce::jlimit(0.3f, 0.72f, g); }
        float tick(float x) noexcept;
        void reset() noexcept;

    private:
        juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Thiran> dl;
        float gain{ 0.618f };
        int M{ 0 };
    };

} // namespace FDNReverb