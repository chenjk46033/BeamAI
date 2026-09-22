#pragma once

#include <stdexcept>
#include <string>
#include <vector>

#include "gui/session_io.hpp"

// Ported from the BeamV0/GUIMatlab/BEAM/GUI/SonicationTab/ multi-visit
// treatment-history cluster: app.sys.treatmentProtocolTables(protocol
// index).sessions(visit index).Data. Each of the 3 real treatment
// protocols (BeamV0.mlapp's own TreatmentProtocolDropDown.Items --
// "PainSCCandAMCC", "PainAMCCandSCC", "PainACC") keeps its own ordered
// list of per-visit treatmentProtocolTable snapshots.
//
// Not ported: the source's own per-protocol-name CSV "blank" template
// (GUI/SonicationTab/getTreatmentProtocolTable.m -- Excluded elsewhere,
// hardcoded wrong-project path, no bundled CSVs). Every protocol here
// starts from the same caller-supplied blank rows instead of a real
// per-protocol template; disclosed, not a silent gap.
//
// initTreatmentProtocolTables.m / addTreatmentProtocolSession.m /
// setProtocolListBox.m / setTreatmentProtocolTableDisplay.m /
// setTreatmentProtocolTableData.m's write-back half are the .m sources;
// setTreatmentProtocolTableData.m's row-coloring half is already ported
// separately (getResponseFromTreatmentProtocolData, see
// TreatmentProtocolTableModel::computeBestTargets).

namespace beam::gui {

struct TreatmentSession {
    int visitNumber = 1;  // 1-based, matches VisitNumberListBox.ItemsData
    std::vector<TreatmentProtocolRecord> rows;
};

struct TreatmentProtocol {
    std::string name;  // e.g. "PainSCCandAMCC"
    std::vector<TreatmentSession> sessions;
};

// initTreatmentProtocolTables.m's fresh-init branch: one TreatmentProtocol
// per name in `protocolNames`, each starting with exactly one session
// (visit 1) holding a copy of `blankRows`.
std::vector<TreatmentProtocol> initTreatmentProtocols(const std::vector<std::string>& protocolNames,
                                                       const std::vector<TreatmentProtocolRecord>& blankRows);

// addTreatmentProtocolSession.m: appends a new session, `blankRows`-
// initialized, whose visitNumber is one past the highest existing
// visitNumber in `protocol`, unless protocol.sessions.size() is already
// >= maxSessions (source: app.maxTreatmentSessions, 15) -- in which case
// this is a no-op. The source compares the CURRENT count (before adding)
// against the cap with `<=`, so the cap actually allows growing to
// maxSessions+1 sessions before it starts refusing -- a real source
// quirk, preserved as-is rather than "fixed".
void addTreatmentProtocolSession(TreatmentProtocol& protocol, const std::vector<TreatmentProtocolRecord>& blankRows,
                                  int maxSessions = 15);

// Throws std::out_of_range if `protocolName` isn't found in `protocols`
// or `visitNumber` isn't a session of that protocol -- both are always
// populated from the store's own names/visit numbers by a real caller,
// so this should never actually happen; matches the source's own
// unguarded struct-array indexing, which would error the same way.
TreatmentProtocol& findProtocol(std::vector<TreatmentProtocol>& protocols, const std::string& protocolName);
TreatmentSession& findSession(TreatmentProtocol& protocol, int visitNumber);

// setTreatmentProtocolTableDisplay.m: which session's rows should be
// shown right now, given the selected protocol name and visit number.
const std::vector<TreatmentProtocolRecord>& currentSessionRows(std::vector<TreatmentProtocol>& protocols,
                                                                 const std::string& protocolName, int visitNumber);

// setTreatmentProtocolTableData.m's write-back half: stores `rows` into
// the matching protocol/visit slot.
void setCurrentSessionRows(std::vector<TreatmentProtocol>& protocols, const std::string& protocolName,
                            int visitNumber, std::vector<TreatmentProtocolRecord> rows);

}  // namespace beam::gui
