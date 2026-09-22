#pragma once

#include <string>

// GUI phase, UIFeatures slice: the Case Report Form (CRF) session-metadata
// logic behind BeamV0/GUIMatlab/BEAM/GUI/UIFeatures/setCRF*.m and
// setSessionCaseReportParameters.m, decoupled from the `app` reads/writes
// (edit fields, button groups). No Qt dependency.
//
// Not ported -- each is a single `app.sys.CRF.<field> = value` assignment,
// so the struct fields below are set directly instead:
//   setCRFOperatorID.m, setCRFHydrogelSize.m, setCRFVisitNumber.m,
//   setCRFParticipantID.m (the last sets SubjectID and ParticipantID to the
//   same value -- one `subjectId` field here).
//
// Deferred (GUI/timer/VSX, no domain math): cleanupCountdown.m,
// currentSonicationTimeMarker.m, progressBarButtonPushed.m.
// startStandaloneCountdown.m/updateFigureTimer.m's own tick/format logic
// is ported -- see gui/countdown_presenter.hpp.
//
// Excluded: setStartTime.m -- `evalin('base','Resource')` + a
// `findall(0,'Type','axes')` scan for a plot whose XLim(2)==20; VSX +
// figure-hunting, no working behaviour to port.

namespace beam::gui {

// The fields BeamV0's CRF code reads/writes on `app.sys.CRF`.
struct CaseReportForm {
    std::string siteId;
    std::string snBeam;
    std::string snTransducer1;
    std::string snTransducer2;
    std::string softwareVersion;
    std::string subjectId;      // setCRFParticipantID.m: SubjectID == ParticipantID
    std::string operatorId;
    std::string hydrogelSize;   // "Small" / "Medium" / "Large"
    int visitNumber = 0;
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    std::string autoSaveFilename;
};

// Port of setCRFDateTime.m's field computation (the
// `app.VisitDateDatePicker.Value` write is presentation, excluded). Takes
// the 6 already-broken-out components rather than a datetime object --
// decomposing a timestamp is a calendar concern the caller owns (e.g. from
// <chrono>).
void setCrfDateTime(CaseReportForm& crf, int year, int month, int day, int hour, int minute,
                     int second);

// Port of setCRFAutoSaveFilename.m:
//   ['S', SiteID, ParticipantID, 'Visit', VisitNumber, 'DateM', mo,
//        'D', dy, 'Y', yr]
// The commented-out `CurrH`/`CurrV` slider suffixes in the source are not
// included (BeamV0 differs from DiademV0 here, which keeps them). Numeric
// pieces use this port's own integer formatting, not MATLAB `num2str`.
std::string computeCrfAutoSaveFilename(const CaseReportForm& crf);

// The operator-entered widget values setSessionCaseReportParameters.m reads
// (ParticipantIDEditField, VisitNumberEditField, HydrogelSizeButtonGroup)
// plus the decomposed `datetime('now')`.
struct SessionCrfInputs {
    std::string participantId;
    int visitNumber = 0;
    std::string hydrogelSize;
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
};

// Port of setSessionCaseReportParameters.m's unconditional body: fills in
// the hardcoded site / serial-number / software-version constants, applies
// the date-time and operator-entered values, and computes the auto-save
// filename -- the same call sequence as the source. The "read previous
// CRF back into the edit fields" preamble and the hydrogel-size ->
// radio-button mapping are GUI state, not carried.
CaseReportForm buildSessionCaseReportForm(const SessionCrfInputs& inputs);

}  // namespace beam::gui
