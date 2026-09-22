#pragma once

#include <vector>

namespace beam::stimulation {

// The per-target timing fields getTxAndBurstEvents.m reads out of
// stimParams(iii). All in seconds. (BLKI is forced to 0 by the source, so
// it isn't an input here.)
struct SonicationSchedule {
    double startTime = 0.0;
    double endTime = 0.0;  // only used by computeSonicationEventTimeline below
    double bi = 0.0;       // burst interval
    double bd = 0.0;       // burst duration
    double pi = 0.0;       // pulse interval
    double pd = 0.0;       // pulse duration
};

struct BurstEvent {
    int txNum = 0;  // 1-based target index (MATLAB `iii`)
    double timeOn = 0.0;
    double timeOff = 0.0;
    int numPulseTransmits = 0;
    double pulseInterval = 0.0;
    double pauseIntervals = 0.0;   // always 0 in the source
    double syncInterval = 0.0;     // always 0 in the source
};

struct TxEvent {
    int tx = 0;  // 1-based target index
    double timeOn = 0.0;
    double timeOff = 0.0;
    double pi = 0.0;
};

struct TxAndBurstEvents {
    std::vector<BurstEvent> burstEvents;
    std::vector<TxEvent> txEvents;
};

// Port of BeamV0/GUIMatlab/BEAM/Stimulation/GeneralSonication/getTxAndBurstEvents.m.
// Expands each target's schedule into individual burst and pulse (tx)
// events with absolute on/off times. All the per-target vectors
// (numBlockIntervals, numBurstIntervals, numPulseTransmits) and
// blockPauseIntervals must be the same length as `schedules`.
// blockPauseIntervals[iii] is summed to a per-target block-pause offset
// (MATLAB `sum(blockPauseIntervals{iii})`).
//
// Faithful-port note: the source sets stimParams(iii).BLKI = 0, so the
// block loop just repeats identical events numBlockIntervals(iii) times --
// preserved as-is.
TxAndBurstEvents getTxAndBurstEvents(const std::vector<SonicationSchedule>& schedules,
                                     const std::vector<int>& numBlockIntervals,
                                     const std::vector<std::vector<double>>& blockPauseIntervals,
                                     const std::vector<int>& numBurstIntervals,
                                     const std::vector<int>& numPulseTransmits);

// One segment of a full-duration on/off timeline: a burst (isBurst,
// txNum = the 1-based target index) or a gap between bursts (!isBurst,
// txNum = 0).
struct TimelineSegment {
    bool isBurst = false;
    int txNum = 0;
    double timeOn = 0.0;
    double timeOff = 0.0;
};

// Port of GeneralSonication/setBurstEventsFromStimParams.m's pure
// computational core (its VSX `TW`/`TX` generation, gated behind an
// optional `varargin`, is dropped -- see docs/known_gaps_stimulation.md).
//
// Per target, computes numBurstIntervals/numPulseTransmits (and
// numBlockIntervals=1, blockPauseIntervals={0}) matching the source's
// hardcoded `Block = 0`: the `if Block` branch (BLKD/BLKI asserts,
// block-level pausing) is therefore dead code in the source and not
// ported. `burstPauseIntervals`/`durationPauseIntervals` (computed in
// the source via `getPauseIntervals`) are provably unused afterward --
// getTxAndBurstEvents never reads them -- so they're dropped too.
//
// Calls the already-ported getTxAndBurstEvents, then merges every
// target's burst events into one sorted, gap-filled timeline (a pause
// segment before the first burst if it doesn't start at 0, and between
// every pair of consecutive bursts) -- the source builds this same
// sequence via a MATLAB struct-array `count` index; this returns the
// equivalent flat list directly rather than reproducing that mechanism.
//
// Throws std::invalid_argument if any schedule fails the source's own
// assertions (PI>=PD, BI>=BD, BD>=PI, duration>=BI) -- the source's
// `catch; return` would leave its declared output unassigned, an
// undefined state this port can't reproduce, so it fails loudly instead.
// Throws std::runtime_error on overlapping burst events (matching the
// source's `error('Overlapping burst events')`).
std::vector<TimelineSegment> computeSonicationEventTimeline(const std::vector<SonicationSchedule>& schedules);

}  // namespace beam::stimulation
