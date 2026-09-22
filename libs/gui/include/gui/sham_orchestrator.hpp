#pragma once

#include <vector>

#include "safety/intensity.hpp"

// Ported from BeamV0/GUIMatlab/BEAM/Stimulation/GeneralSonication/
// unfocusedSonicate.m's real content: computing the sonication's
// duration and assembling the masking-audio track via the already-
// ported beam::sham::setShamAudio, with the source's own fixed args
// (randFlag=0, backgroundNoiseFlag=1) at this call site.
//
// Notable: `generalSonicateMaster.m` -- the REAL sonication path, ported
// as beam::gui::prepareSonication -- calls the exact same setShamAudio
// with the same fixed args, right alongside its own
// startStandaloneCountdown call. So a real sonication plays this same
// masking audio underneath the actual ultrasound send too, presumably so
// a patient can't tell real vs. sham sessions apart by sound.
// prepareSonication's header comment dismisses setShamAudio as "no
// effect on whether/what gets sent" -- true for that function's own
// decision logic, but this masking-audio side effect itself was not
// otherwise ported anywhere until now.
//
// Not ported here: the ShamButton widget enable/disable and the
// surrounding app.sys.log / CRF / treatmentProtocolTable / CSV-logging
// bookkeeping in ShamButtonPushed -- app-state glue, the same scoping
// prepareSonication already applies to SonicateButtonPushed's own
// surrounding bookkeeping.

namespace beam::gui {

struct ShamSonicationOutcome {
    double durationSeconds = 0.0;      // stimParams(1).endTime - startTime
    std::vector<double> maskingAudio;  // setShamAudio's assembled buffer
};

// soundData/fs: the burst sound tiled into the masking track. The source
// loads this from `sonicationSound.mat`, which isn't bundled with this
// repo (not redistributable); the caller supplies its own buffer.
ShamSonicationOutcome prepareShamSonication(const beam::safety::SonicationSafetyParams& params,
                                             const std::vector<double>& soundData, double fs);

}  // namespace beam::gui
