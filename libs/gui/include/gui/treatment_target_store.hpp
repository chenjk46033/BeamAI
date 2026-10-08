#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

// BeamAI's own store for the Target List and the protocols that schedule it.
// Not a port and not a reader of BeamV0's .mat or its TreatmentProtocols/*.csv
// -- those stay a migration source, not a runtime dependency.
//
// Three levels, matching what the data actually varies by:
//
//   target point    one sonication of a target: its parameters, and its place
//                   in that target's own ordering. A target owns a list.
//   target entry    a Target List row: a name and its points.
//   protocol entry  an ordered reference to a target by name, carrying the
//                   duration that entry runs for. BeamV0's protocols list the
//                   same target repeatedly with different values, so these
//                   cannot live only on the target.
//   session         X/Y/Z placement, per patient. Not here.

namespace beam::gui {

// BeamV0 keeps a whole stimParamTable per Target List entry:
// setProtocolTableWithStimParamTable.m assigns the entire grid Data into
// sys.protocolTables(selected target).stimParamTableData, so the target's
// sonications are rows of one table and each carries its own parameters.
// Flattening a target to a single parameter set loses every row past the
// first -- which is what this type exists to stop.
//
// X/Y/Z are not here: placement is per-patient session data, and the grid
// supplies it from the array centre until the operator places the point.
struct TreatmentTargetPoint {
    int order = 1;
    bool show = true;
    // createStimParamTable.m's own default, for a point with no data behind
    // it. The shipped seed overrides it per target; BeamV0 never shows this
    // value itself, because startUpFunction.m:41 always replaces the created
    // table with sys.protocolTables(1).stimParamTableData.
    double amplitude = 0.5;
    double startTimeSeconds = 0.0;
    double endTimeSeconds = 30.0;
    // BeamV0's values (createStimParamTable.m and every TreatmentProtocols
    // CSV). BeamV0 is the reference; changing these diverges from it.
    double burstDurationSeconds = 0.03;
    double burstIntervalSeconds = 0.70;
    double pulseDurationSeconds = 0.005;
    double pulseIntervalSeconds = 0.01;
};

struct TreatmentTarget {
    std::string name;
    // Never empty after a load: a target with no row could not be sonicated.
    // createStimParamTable.m's own N is 1, so the shipped seed has one each.
    std::vector<TreatmentTargetPoint> points;
};

struct TreatmentProtocolEntry {
    int order = 1;
    std::string target;
    // Empty means "use the target's own value". Note that BeamV0 pushes only
    // the duration onto the target's rows -- updateSonicateSettingsForCurrent-
    // Sonication.m sets startTime and endTime and leaves Amplitude alone --
    // so the amplitude here is the schedule's record, not an override.
    std::optional<double> amplitude;
    std::optional<double> durationSeconds;
};

struct TreatmentProtocolDefinition {
    std::string name;
    std::vector<TreatmentProtocolEntry> entries;
};

class TreatmentTargetStore {
public:
    // Both are CSV with a header row; '#' starts a comment. Throws
    // std::runtime_error if a file is unreadable or a row is malformed, so a
    // typo in the data surfaces at load rather than as a silent default.
    void loadTargets(const std::string& path);
    void loadProtocols(const std::string& path);

    const std::vector<TreatmentTarget>& targets() const { return targets_; }
    const std::vector<TreatmentProtocolDefinition>& protocols() const { return protocols_; }

    const TreatmentTarget* findTarget(const std::string& name) const;
    // Replaces the named target's points, keeping the Target List's order.
    // Returns false if the name is unknown. Points are renumbered 1..n in the
    // order given, so a caller can reorder or add by passing them reordered.
    bool updateTarget(const TreatmentTarget& target);
    // The same write from the grid's point of view: replaces just the points.
    // Refuses an unknown name or an empty list -- a target with no row could
    // not be sonicated.
    bool setTargetPoints(const std::string& name, std::vector<TreatmentTargetPoint> points);
    // BeamV0's own combination at sonication time
    // (updateSonicateSettingsForCurrentSonication.m): every row of the target
    // takes startTime 0 and endTime from the protocol entry's duration, and
    // keeps its own amplitude. Empty if the target is unknown.
    std::vector<TreatmentTargetPoint> pointsForSonication(const std::string& targetName,
                                                           std::optional<double> durationSeconds) const;

    // Writes the current state back in the same format loadTargets /
    // loadProtocols read. Throws std::runtime_error if the path is unwritable.
    void saveTargets(const std::string& path) const;
    void saveProtocols(const std::string& path) const;

    bool addTarget(const TreatmentTarget& target);
    bool removeTarget(const std::string& name);

    const TreatmentProtocolDefinition* findProtocol(const std::string& name) const;
    // The protocol's targets in schedule order, each name once. Empty if the
    // protocol is unknown.
    std::vector<std::string> distinctTargetsOf(const std::string& protocolName) const;

    // Protocol CRUD. add/rename refuse a name that is empty or already taken;
    // the others refuse an unknown name. Entries are renumbered 1..n in the
    // order given, so a caller can reorder by passing them reordered.
    bool addProtocol(const std::string& name);
    bool renameProtocol(const std::string& from, const std::string& to);
    bool removeProtocol(const std::string& name);
    bool setProtocolEntries(const std::string& name, std::vector<TreatmentProtocolEntry> entries);
    // Convenience for the common edit: append one reference to a target.
    bool appendProtocolEntry(const std::string& protocolName, const TreatmentProtocolEntry& entry);

    // Names a protocol references that loadTargets never defined -- a data
    // error worth reporting rather than defaulting around.
    std::vector<std::string> danglingTargetNames() const;

private:
    std::vector<TreatmentTarget> targets_;
    std::vector<TreatmentProtocolDefinition> protocols_;
    std::map<std::string, std::size_t> targetIndex_;
};

}  // namespace beam::gui
