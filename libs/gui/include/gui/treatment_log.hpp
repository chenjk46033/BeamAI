#pragma once

#include <optional>
#include <string>
#include <vector>

// What was actually delivered, as opposed to what was planned.
//
// BeamV0 keeps this as treatmentProtocolTables(i).sessions(visit).Data: one
// copy of the protocol's schedule per visit, seeded from blank.Data by
// initTreatmentProtocolTables.m and written back by
// setTreatmentProtocolTableData.m as the visit runs. The displayed table IS
// that record -- updateSonicateSettingsForCurrentSonication.m even rewrites
// Data.Name when adaptive targeting picks a different target than the schedule
// named, so the log says what happened rather than what was intended.
//
// Two things BeamV0 means to record and does not: it parses a Parameters
// string in setLogInfoToCSV.m that nothing ever writes, and its commented-out
// rename list names SonicationStartDTM / SonicationEndDTM columns that are
// never populated. Both are recorded here at the moment of firing.

namespace beam::gui {

struct TreatmentLogRow {
    int visit = 1;
    // 1-based position within the visit, in the order delivered. Not the
    // protocol's row number: a visit may skip, repeat or re-order.
    int sequence = 1;
    std::string protocol;
    // The target actually sonicated.
    std::string target;
    // Where it fired, in RAS millimetres.
    double xMm = 0.0;
    double yMm = 0.0;
    double zMm = 0.0;
    // What was sent, not what the Target List holds -- the schedule's duration
    // is already folded into startTime/endTime by this point.
    double amplitude = 0.5;
    double startTimeSeconds = 0.0;
    double endTimeSeconds = 30.0;
    double burstDurationSeconds = 0.03;
    double burstIntervalSeconds = 0.70;
    double pulseDurationSeconds = 0.005;
    double pulseIntervalSeconds = 0.01;
    // Sham deliveries are logged too: a masked sonication is part of the
    // record, and omitting it would leave an unexplained gap in the sequence.
    bool sham = false;
    // ISO-8601 local time. Empty end means it was aborted or is still running.
    std::string startedAt;
    std::string endedAt;
    // False once the operator has put this row here by hand. A visit that
    // has been edited stops tracking later protocol edits: the operator
    // skipping or adding a sonication outranks the plan for that visit.
    bool fromPlan = true;
    // The correction values in force when this sonication was sent --
    // setLogInfoToCSV.m's AttenuationVal and DC, which it reads from
    // app.sys.log{i}. Empty for a sham, which sends nothing.
    std::optional<double> attenuation;
    std::optional<double> dutyCycle;
    // Operator-entered, filled in after the fact.
    std::optional<double> responsePain;
    std::optional<double> responseMood;
    std::string notes;
};

class TreatmentLog {
public:
    // CSV with a header row; '#' starts a comment. A missing file is not an
    // error -- a patient with no deliveries yet simply has no log.
    void load(const std::string& path);
    void save(const std::string& path) const;

    const std::vector<TreatmentLogRow>& rows() const { return rows_; }
    // Appends, numbering the row for its protocol and visit. Returns the
    // sequence given.
    int append(TreatmentLogRow row);
    // Writes at row.sequence, replacing any row already there. This is what
    // BeamV0's Sonicate does -- it assigns into treatmentProtocolTable row i,
    // where i is the Current Sonication Number -- so re-firing a sonication
    // the operator stepped back to overwrites it rather than adding a row.
    void record(TreatmentLogRow row);
    // Brings the visit's copy of a protocol up to date with the plan. This is
    // initTreatmentProtocolTables.m's `sessions(j).Data = blank.Data`, with one
    // difference it has to have: a row that has NOT been fired is still just
    // the plan, so an edit to the protocol has to reach it -- otherwise the
    // visit shows a schedule nobody can change. A row that HAS been fired is a
    // record and is never touched, and trailing unfired rows go when the
    // schedule shortens. Responses and notes survive either way.
    void seedVisit(int visit, const std::string& protocol,
                   const std::vector<TreatmentLogRow>& planned);
    std::vector<TreatmentLogRow> rowsFor(const std::string& protocol, int visit) const;
    // Puts a sonication into the visit after the given position, moving the
    // rows below it down. Refuses if that would move one already fired --
    // its sequence is where it happened. The new row is the operator's, not
    // the plan's. False if the position is unknown.
    bool insertAfter(const std::string& protocol, int visit, int sequence, TreatmentLogRow row);
    // Whether that insert would be accepted. The menu offering it asks this,
    // so an action that cannot work is disabled rather than refused after the
    // operator has chosen a target.
    bool canInsertAfter(const std::string& protocol, int visit, int sequence) const;
    // Drops a sonication the visit has not run, closing the gap. Refuses a
    // row that has been fired, and one whose removal would move a fired row.
    bool removeAt(const std::string& protocol, int visit, int sequence);
    bool canRemoveAt(const std::string& protocol, int visit, int sequence) const;
    // The row at one scheduled position, or nullptr.
    const TreatmentLogRow* find(const std::string& protocol, int visit, int sequence) const;
    // True once the visit has stopped following the plan -- something has been
    // delivered, skipped or added. seedVisit only adds missing positions then,
    // so a protocol edited now will not reach this visit.
    bool visitFrozen(const std::string& protocol, int visit) const;
    // True once the row has actually been fired, as opposed to merely seeded.
    static bool delivered(const TreatmentLogRow& row) { return !row.startedAt.empty(); }
    // Records a response or a note against an existing row. False if no such
    // row exists.
    bool setResponse(const std::string& protocol, int visit, int sequence,
                     std::optional<double> pain, std::optional<double> mood);
    bool setNotes(const std::string& protocol, int visit, int sequence, const std::string& notes);
    // Stamps the end time on a row left open by a delivery.
    bool close(const std::string& protocol, int visit, int sequence, const std::string& endedAt);

    void clear() { rows_.clear(); }

    // getResponseFromTreatmentProtocolData.m: each score rounded and clamped to
    // [-2, 2], a missing one zero, the total their sum.
    static double responseTotal(const TreatmentLogRow& row);
    // getTreatmentProtocolParamString.m's format.
    static std::string parameterString(const TreatmentLogRow& row);

private:
    std::vector<TreatmentLogRow> rows_;
};

}  // namespace beam::gui
