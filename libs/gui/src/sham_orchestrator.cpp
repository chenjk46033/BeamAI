#include "gui/sham_orchestrator.hpp"

#include "sham/sham_audio.hpp"

namespace beam::gui {

ShamSonicationOutcome prepareShamSonication(const beam::safety::SonicationSafetyParams& params,
                                             const std::vector<double>& soundData, double fs) {
    ShamSonicationOutcome outcome;
    outcome.durationSeconds = params.endTime - params.startTime;
    // setShamAudio(duration, BD, BI, randFlag=0, backgroundNoiseFlag=1, writeFlag=0) in the
    // source; randFlag/writeFlag already dropped by the ported setShamAudio itself.
    outcome.maskingAudio =
        beam::sham::setShamAudio(soundData, fs, outcome.durationSeconds, params.bd, params.bi,
                                  /*backgroundNoise=*/true);
    return outcome;
}

}  // namespace beam::gui
