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

// One row of a visit, as the ranking sees it. duration is in seconds and
// decides what the row is for: under a minute is a screening sonication, a
// minute or more is a treatment one.
struct PlanRow {
    int sequence = 0;
    std::string target;
    double duration = 0.0;
    double response = 0.0;  // pain + mood, each already clamped to [-2, 2]
    bool delivered = false;
};

// A run of rows of one kind. The plan has no column saying which a row is:
// under a minute screens, a minute or more treats, and a run of each is a
// block. feedFirst/feedLast name the screening block a treatment block takes
// its winners from, or 0 when there is none above it.
struct PlanBlock {
    bool screening = false;
    int first = 0;  // sequence numbers, inclusive
    int last = 0;
    int feedFirst = 0;
    int feedLast = 0;
};

// The same scan rankTreatmentRows makes, exposed so what is shown and what is
// done cannot drift apart.
std::vector<PlanBlock> planBlocks(const std::vector<PlanRow>& plan);

// A planned row and the target it should run instead.
struct RankedTarget {
    int sequence = 0;
    std::string target;
};

// Port of what updateSonicateSettingsForCurrentSonication.m does with the
// function above. BeamV0 screens six short sonications and then runs long
// ones at whichever of them scored best, cycling through the winners in rank
// order -- its schedule is fixed at 24 rows, so it addresses those blocks by
// number (1-6 feed 7-12, 13-18 feed 19-24) and the long rows are named T1-T6,
// placeholders no Target List defines.
//
// A visit here holds whatever rows it was given, so the blocks are read off
// the durations instead: each run of treatment rows takes the winners of the
// screening rows immediately above it, which is the same thing for BeamV0's
// own protocols. The region ranking that breaks ties comes from the names in
// that screening block -- all SCC, all aMCC, or mixed -- where BeamV0 takes
// it from the protocol name.
//
// Returns only the rows whose target changes. A delivered row is never one of
// them: it already happened. A treatment run with no screening above it is
// left alone rather than filled from a ranking of nothing.
std::vector<RankedTarget> rankTreatmentRows(const std::vector<PlanRow>& plan);

}  // namespace beam::gui
