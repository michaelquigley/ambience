#pragma once
#include <JuceHeader.h>
#include "DSPConstants.h"
#include "BiquadFilters.h"
#include "SAPFStage.h"

namespace FDNReverb {

    class DelayUnit {
    public:
        void prepare(const juce::dsp::ProcessSpec& spec,
            int targetDelaySamples, int lfoPhaseOffsetDeg,
            const std::array<int, SAPF_STAGES>& sapfDelays);

        void setAbsorption(const std::array<BiquadCoeffs, ABSO_STAGES>& c) noexcept;
        void setLFO(float rateHz, float depthSamples, double sampleRate) noexcept;
        void setDiffusion(float d) noexcept;

        float tick(float input) noexcept;
        void reset() noexcept;

        int nominalDelaySamples{ 0 };

    private:
        juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Thiran> delay;
        std::array<BiquadCoeffs, ABSO_STAGES> absorptionCoeffs;
        std::array<BiquadState, ABSO_STAGES> absorptionState;
        std::array<SAPFStage, SAPF_STAGES> sapf;

        float lfoPhase{ 0.f };
        float lfoIncrement{ 0.f };
        float lfoDepth{ 0.f };
        double fs{ 48000.0 };
    };

} // namespace FDNReverb