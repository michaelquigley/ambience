#include "SAPFStage.h"

namespace FDNReverb {

    void SAPFStage::prepare(const juce::dsp::ProcessSpec& spec, int delayTargetSamples) {
        M = delayTargetSamples;
        dl.prepare(spec);
        dl.setMaximumDelayInSamples(M + 4);
        dl.setDelay(static_cast<float>(M));
    }

    float SAPFStage::tick(float x) noexcept {
        float d = dl.popSample(0);
        float w = x + gain * d;
        dl.pushSample(0, w);
        return d - gain * w;
    }

    void SAPFStage::reset() noexcept { dl.reset(); }

} // namespace FDNReverb