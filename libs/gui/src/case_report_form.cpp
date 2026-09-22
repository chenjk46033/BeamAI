#include "gui/case_report_form.hpp"

#include <string>

namespace beam::gui {

void setCrfDateTime(CaseReportForm& crf, int year, int month, int day, int hour, int minute,
                    int second) {
    crf.year = year;
    crf.month = month;
    crf.day = day;
    crf.hour = hour;
    crf.minute = minute;
    crf.second = second;
}

std::string computeCrfAutoSaveFilename(const CaseReportForm& crf) {
    using std::to_string;
    return "S" + crf.siteId + crf.subjectId + "Visit" + to_string(crf.visitNumber) + "DateM" +
           to_string(crf.month) + "D" + to_string(crf.day) + "Y" + to_string(crf.year);
}

CaseReportForm buildSessionCaseReportForm(const SessionCrfInputs& in) {
    CaseReportForm crf;
    // Hardcoded in setSessionCaseReportParameters.m.
    crf.siteId = "Utah";
    crf.snBeam = "BeamV01";
    crf.snTransducer1 = "xdr001";
    crf.snTransducer2 = "xdr002";
    crf.softwareVersion = "V1.0.0";

    setCrfDateTime(crf, in.year, in.month, in.day, in.hour, in.minute, in.second);
    crf.subjectId = in.participantId;  // setCRFParticipantID.m: SubjectID == ParticipantID
    crf.visitNumber = in.visitNumber;
    crf.hydrogelSize = in.hydrogelSize;
    // setCRFOperatorID is commented out in the source -- operatorId left blank.

    crf.autoSaveFilename = computeCrfAutoSaveFilename(crf);
    return crf;
}

}  // namespace beam::gui
