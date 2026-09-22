#pragma once

// Port of BeamV0/GUIMatlab/BEAM/Registration/setRegistrationCheck.m: the
// Register tab's 3 status lamps (Right/Left/InsideMRI Registration) plus
// the Sonicate button's enable state, driven off two completion flags.
//
// Disclosed source quirk: the source's two `if` blocks run unconditionally
// in sequence, not as mutually-exclusive branches -- so the *second*
// block's write to the Right lamp always overwrites whatever the first
// block set. Net effect: despite appearances, the Right lamp tracks
// `currentRegistrationComplete` only (the InsideMRI-complete branch's
// write to it is always clobbered); InsideMRI tracks
// `mriRegistrationComplete` only (never touched again); Left tracks
// `currentRegistrationComplete` only. Preserved as-is, not fixed.

namespace beam::gui {

struct RegistrationCheckLampState {
    bool rightLampOn = false;      // == currentRegistrationComplete (see quirk above)
    bool leftLampOn = false;       // == currentRegistrationComplete
    bool insideMriLampOn = false;  // == mriRegistrationComplete
    bool sonicateButtonEnabled = false;  // currentRegistrationComplete && mriRegistrationComplete
};

RegistrationCheckLampState computeRegistrationCheckLampState(bool mriRegistrationComplete,
                                                               bool currentRegistrationComplete);

}  // namespace beam::gui
