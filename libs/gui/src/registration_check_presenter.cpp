#include "gui/registration_check_presenter.hpp"

namespace beam::gui {

RegistrationCheckLampState computeRegistrationCheckLampState(bool mriRegistrationComplete,
                                                               bool currentRegistrationComplete) {
    RegistrationCheckLampState s;
    s.insideMriLampOn = mriRegistrationComplete;
    s.rightLampOn = currentRegistrationComplete;
    s.leftLampOn = currentRegistrationComplete;
    s.sonicateButtonEnabled = mriRegistrationComplete && currentRegistrationComplete;
    return s;
}

}  // namespace beam::gui
