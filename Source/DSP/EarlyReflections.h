#pragma once
#include <JuceHeader.h>
#include "DSPConstants.h"
#include "BiquadFilters.h"

namespace FDNReverb {

    struct ERTap {
        float delaySamples{ 0.f };
        float gainL{ 0.f };
        float gainR{ 0.f };
    };

    class EarlyReflections {
    public:
        void prepare(const juce::dsp::ProcessSpec& spec);
        void buildTaps(const AlgorithmPreset& preset, float roomSizeScale, double sampleRate);
        void setPreDelay(float ms, double sampleRate) noexcept;

        std::pair<float, float> tick(float mono) noexcept;
        void reset() noexcept;

    private:
        juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> buf;
        std::array<ERTap, ER_TAPS> taps;
        int preDelaySamples{ 0 };

        BiquadCoeffs erHPCoeffs, erLPCoeffs;
        BiquadState  erHPL, erHPR, erLPL, erLPR;
    };

} // namespace FDNReverb