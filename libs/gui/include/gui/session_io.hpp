#pragma once

#include <string>
#include <vector>

#include "registration/fiducial_markers.hpp"

// New infrastructure -- no single BeamV0 `.m` counterpart. The manual's
// Operating Instructions describe a "File -> Save Subject" / "File ->
// Load Subjects" step that exports "the position, stimulation parameters,
// and correction values ... the registration values, and all other
// settings" to a `.mat` file so a session's registration doesn't have to
// be repeated; that step lives in `BeamV0.mlapp`'s own callbacks (App
// Designer, not a separate `.m` file), so there's nothing to port here --
// this is a from-scratch equivalent, not a translation.
//
// Deliberate deviation: this writes Beam's own plain-text, tab-separated
// format (see serializeSession's doc comment below), not BeamV0's `.mat`
// binary -- adding a MAT-file writer dependency for this would be a lot of
// machinery for a session file whose only consumer is this same program.
// Not included (yet): correction measurements and the per-sonication log
// (`app.sys.log`) -- those are recorded observations from a session, not
// configuration to restore into a new one; MRI is its own "File -> Load
// MRI" step already.

namespace beam::gui {

struct StimParamRecord {
    int order = 1;
    bool show = true;
    double x = 0.0, y = 0.0, z = 0.0;
    double amplitude = 0.0;
    double startTime = 0.0, endTime = 0.0;
    double bd = 0.0, bi = 0.0, pd = 0.0, pi = 0.0;
};

struct TreatmentProtocolRecord {
    int number = 1;
    std::string target;
    double duration = 0.0;
    double amplitude = 0.0;
    std::string parameters;
    double pain = 0.0;
    double mood = 0.0;
    std::string notes;
};

struct SessionData {
    std::vector<beam::registration::FiducialMarker> fiducials;  // position in meters, this project's convention
    std::vector<StimParamRecord> stimParams;
    std::vector<TreatmentProtocolRecord> treatmentProtocol;
};

// Beam's own session format: `# Beam session vN` on line 1, then
// `[Fiducials]` / `[StimParams]` / `[TreatmentProtocol]` sections, each a
// header line (column names, for humans -- the parser ignores it and
// reads by fixed position) followed by one tab-separated row per record.
// Free-text fields (`target`/`parameters`/`notes`) must not themselves
// contain a literal tab or newline -- deserializeSession throws
// std::runtime_error if a data row doesn't split into the expected column
// count, which is what a smuggled tab would cause.
std::string serializeSession(const SessionData& data);

// Inverse of serializeSession. Throws std::runtime_error on a missing/
// unrecognized version line, a malformed section header, or a data row
// with the wrong column count. Unknown section names are skipped (forward
// compatibility), not an error.
SessionData deserializeSession(const std::string& text);

}  // namespace beam::gui
