#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "mri/ras_transform.hpp"
#include "mri/slice.hpp"

namespace beam::infra::mat {

// Read-only import of the MRI portion of a BeamV0/Diadem MATLAB session.
// The source file is never modified. Other session state is deliberately
// ignored until each field has a reviewed workflow mapping.
struct LegacyBeamMri {
    struct Fiducial { std::string name; Eigen::Vector3d positionMm; };
    beam::mri::Volume3D volume;
    beam::mri::RasAxisVectors axes;
    std::vector<Fiducial> fiducials;
};

LegacyBeamMri loadLegacyBeamMri(const std::string& path);

// The treatment plan's *index*, read from a session's own sys. The tables
// themselves are MATLAB `table` objects, stored as MCOS references that matio
// resolves to nothing -- so this recovers the names and the counts and says so
// about the rest, rather than pretending to a parity it cannot reach.
//
// The names are worth recovering on their own: setProtocolTableWithStimParamTable.m
// keys sys.protocolTables by Target List position, so this order IS the Target
// List. See docs/known_gaps_treatment.md.
struct LegacyBeamPlan {
    struct Protocol { std::string name; std::size_t sessionCount = 0; };
    // sys.protocolTables(i).name, in index order.
    std::vector<std::string> targetNames;
    // sys.treatmentProtocolTables(i).name, with its session count.
    std::vector<Protocol> protocols;
    // True when a table payload was present but unreadable -- the normal case
    // for a MATLAB table, and the reason this is not a parity source.
    bool tablePayloadsUnreadable = false;
};

LegacyBeamPlan loadLegacyBeamPlan(const std::string& path);

}  // namespace beam::infra::mat
