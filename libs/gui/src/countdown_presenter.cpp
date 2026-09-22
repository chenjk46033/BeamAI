#include "gui/countdown_presenter.hpp"

#include <cstdio>

namespace beam::gui {

namespace {

// MATLAB's floor(a/b) and mod(a,b) for integer a and positive integer b:
// both use floor-division semantics (result of mod has the same sign as
// b), unlike C++'s truncating `/`/`%`.
int matlabFloorDiv(int a, int b) {
    const int q = a / b;
    const int r = a % b;
    return (r != 0 && ((r < 0) != (b < 0))) ? q - 1 : q;
}

int matlabMod(int a, int b) {
    const int r = a % b;
    return (r != 0 && ((r < 0) != (b < 0))) ? r + b : r;
}

}  // namespace

int countdownTimeLeftSeconds(int durationSeconds, int tickCount) { return durationSeconds - 2 * tickCount; }

bool isCountdownDone(int timeLeftSeconds) { return timeLeftSeconds < -2; }

std::string countdownDisplayText(int timeLeftSeconds) {
    const int minutes = matlabFloorDiv(timeLeftSeconds, 60);
    const int seconds = matlabMod(timeLeftSeconds, 60);
    char buf[48];
    std::snprintf(buf, sizeof(buf), "Time remaining: %d:%02d", minutes, seconds);
    return std::string(buf);
}

}  // namespace beam::gui
