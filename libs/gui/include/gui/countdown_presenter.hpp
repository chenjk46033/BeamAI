#pragma once

#include <string>

// Ported from BeamV0/GUIMatlab/BEAM/GUI/UIFeatures/startStandaloneCountdown.m
// and updateFigureTimer.m: the pure tick -> remaining-time -> display-text
// logic behind the standalone countdown window shown while a sonication
// runs, decoupled from the MATLAB `timer` object / figure it's normally
// wired to.
//
// Not ported here (pure MATLAB figure/timer mechanics, no domain logic):
// the standalone black figure window itself, the `timer` object's
// construction/`start`/`stop` calls, and cleanupCountdown.m (closes the
// figure, clears a `sound` object). currentSonicationTimeMarker.m
// (a moving vertical line swept across a plot via a blocking `pause`
// loop, driven by a hardcoded `endTime = 20`, not the real sonication
// duration) and progressBarButtonPushed.m (a custom button-icon
// progress-bar animation) stay deferred too -- neither has any logic
// beyond MATLAB-specific rendering.

namespace beam::gui {

// Port of updateFigureTimer.m's per-tick arithmetic: `duration -
// 2*tasksExecuted` (the timer's fixed 2-second period is hardcoded in the
// source, reproduced as the `2` here). `tickCount` is 1-based, matching
// MATLAB's `TasksExecuted` after the first firing.
int countdownTimeLeftSeconds(int durationSeconds, int tickCount);

// Port of updateFigureTimer.m's stop condition (`if timeLeft < -2`).
bool isCountdownDone(int timeLeftSeconds);

// Port of updateFigureTimer.m's display string:
// `sprintf('Time remaining: %d:%02d', floor(timeLeft/60), mod(timeLeft,60))`.
// Faithful-port note: MATLAB's `floor`/`mod` use floor-division semantics
// (the result of `mod` always has the same sign as the divisor, 60 here),
// unlike C++'s `/`/`%` on negative operands -- this matters because
// `timeLeft` can be slightly negative (as low as -2) in the final tick
// before the countdown stops. E.g. timeLeft=-1 prints "Time remaining:
// -1:59" (MATLAB: floor(-1/60)=-1, mod(-1,60)=59), not "0:-1" or "-1:-1" --
// reproduced exactly, not "fixed".
std::string countdownDisplayText(int timeLeftSeconds);

}  // namespace beam::gui
