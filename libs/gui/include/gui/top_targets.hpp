#pragma once

#include <string>
#include <vector>

// GUI phase, SonicationTab slice 2: BeamV0/GUIMatlab/BEAM/GUI/SonicationTab/
// getTopTargetsFromTreatmentProtocolTable.m -- a real clinical-protocol
// algorithm (which past sonication targets to carry forward into the next
// treatment session, based on historical mood/pain responses), not GUI
// glue. Deliberately given its own slice (see docs/known_gaps_gui.md)
// rather than being rushed in alongside the smaller SonicationTab
// functions.
//
// Not ported: the `app`-coupled preamble that builds the source's
// `responses` struct (`sessionI`/`count` dropdown lookups, reading
// `app.sys.treatmentProtocolTables(...).sessions(...).Data`, and calling
// the already-ported `getResponseFromTreatmentProtocolData` per row). The
// caller assembles `responses` (this header's `TargetResponse` list) from
// those pieces; this function starts exactly where the source's pure
// computation starts.

namespace beam::gui {

// One entry of the source's `responses` struct-of-arrays, as a single
// record: `sonicationNumber` is the source's 1-based `i`; `duration` is in
// seconds (the source's "block A" is `duration < 60`).
struct TargetResponse {
    int sonicationNumber = 0;
    std::string name;
    double numericResponse = 0.0;
    double duration = 0.0;
};

struct BestTargets {
    std::vector<int> sonicationNumber;
    std::vector<std::string> name;
    std::vector<double> numericResponse;
    std::vector<double> duration;
};

// The source's three `ACCFlag` branches for `bestTargetsRanking`.
enum class AccFlag { kScc, kAmcc, kOther };

// Port of getTopTargetsFromTreatmentProtocolTable.m's pure computation
// (its `bestTargets` output only -- `blockAResponses`/`responses` are
// intermediate, not returned to any real caller in the source either).
//
// Algorithm, faithfully reproduced including its quirks:
//  1. Keep only "block A" responses (`duration < 60`).
//  2. Group by *exact* name (MATLAB `unique` is case-sensitive and sorts
//     alphabetically -- this sort order is what later tie-breaks fall back
//     on), keep each group's highest response (first occurrence on ties),
//     and drop any group named "Sham" (case-*insensitive*, unlike the
//     grouping itself).
//  3. Sort the remaining unique targets by response, descending, stably.
//  4. Split into positive (> 0) and negative (< 0) buckets, in that sorted
//     order. (The source also computes a `neutralResponses` (== 0) bucket
//     that is never used again -- dropped here, along with a `responsesVals`
//     local that's dead for the same reason.)
//  5. If there are more than 3 positives, repeatedly drop the
//     lowest-valued one until 3 remain; ties are broken by dropping the
//     one whose name matches the *worst*-ranked region first (i.e. the
//     better-ranked target of a tied pair survives).
//  6. Else if there are zero positives, scan the ranking list (best region
//     first) and pull in the first ranked name that exists in the full
//     unique set with response >= 0 (not just > 0) and isn't already
//     selected -- stops once one is added.
//  7. If still short of 1 selected target, pull in the least-negative
//     remaining target; ties are broken toward the *best*-ranked region
//     this time (the opposite direction from step 5's tie-break).
//  8. If nothing was ever selected, `name` (only -- not the other three
//     arrays, a genuine length mismatch in the source, preserved here) is
//     padded to exactly 3 "Sham" entries.
//  9. Every remaining "Sham" name is replaced, in order, with the
//     best-ranked region not already present in the result.
BestTargets getTopTargetsFromTreatmentProtocolTable(const std::vector<TargetResponse>& responses,
                                                     AccFlag accFlag);

}  // namespace beam::gui
