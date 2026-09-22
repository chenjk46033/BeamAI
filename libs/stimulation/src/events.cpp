#include "stimulation/events.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace beam::stimulation {

TxAndBurstEvents getTxAndBurstEvents(const std::vector<SonicationSchedule>& schedules,
                                     const std::vector<int>& numBlockIntervals,
                                     const std::vector<std::vector<double>>& blockPauseIntervals,
                                     const std::vector<int>& numBurstIntervals,
                                     const std::vector<int>& numPulseTransmits) {
    TxAndBurstEvents out;

    for (std::size_t iii = 0; iii < schedules.size(); ++iii) {
        const SonicationSchedule& s = schedules[iii];
        const int txNum = static_cast<int>(iii) + 1;  // MATLAB 1-based `iii`
        const double blockPauseTime =
            std::accumulate(blockPauseIntervals[iii].begin(), blockPauseIntervals[iii].end(), 0.0);

        for (int b = 0; b < numBlockIntervals[iii]; ++b) {
            // (B-1)*BLKI == 0 (BLKI forced to 0 by the source).
            for (int bursti = 0; bursti < numBurstIntervals[iii]; ++bursti) {
                const double burstBase = s.startTime + blockPauseTime + bursti * s.bi;

                BurstEvent be;
                be.txNum = txNum;
                be.timeOn = burstBase;
                be.timeOff = burstBase + s.bd;
                be.numPulseTransmits = numPulseTransmits[iii];
                be.pulseInterval = s.pi;
                out.burstEvents.push_back(be);

                for (int pulsei = 0; pulsei < numPulseTransmits[iii]; ++pulsei) {
                    TxEvent te;
                    te.tx = txNum;
                    te.timeOn = burstBase + pulsei * s.pi;
                    te.timeOff = te.timeOn + s.pd;
                    te.pi = s.pi;
                    out.txEvents.push_back(te);
                }
            }
        }
    }

    return out;
}

std::vector<TimelineSegment> computeSonicationEventTimeline(const std::vector<SonicationSchedule>& schedules) {
    const std::size_t n = schedules.size();
    std::vector<int> numBurstIntervals(n);
    std::vector<int> numPulseTransmits(n);
    const std::vector<int> numBlockIntervals(n, 1);                                // Block hardcoded 0 -> ~Block always
    const std::vector<std::vector<double>> blockPauseIntervals(n, std::vector<double>{0.0});

    for (std::size_t i = 0; i < n; ++i) {
        const SonicationSchedule& s = schedules[i];
        const double duration = s.endTime - s.startTime;
        if (!(s.pi >= s.pd) || !(s.bi >= s.bd) || !(s.bd >= s.pi) || !(duration >= s.bi)) {
            throw std::invalid_argument(
                "computeSonicationEventTimeline: schedule fails PI>=PD, BI>=BD, BD>=PI, or duration>=BI");
        }
        numBurstIntervals[i] = static_cast<int>(std::floor(duration / s.bi));
        numPulseTransmits[i] = static_cast<int>(std::floor(s.bd / s.pi));
    }

    const TxAndBurstEvents events =
        getTxAndBurstEvents(schedules, numBlockIntervals, blockPauseIntervals, numBurstIntervals, numPulseTransmits);

    std::vector<BurstEvent> bursts = events.burstEvents;
    std::sort(bursts.begin(), bursts.end(), [](const BurstEvent& a, const BurstEvent& b) { return a.timeOn < b.timeOn; });
    for (std::size_t i = 0; i + 1 < bursts.size(); ++i) {
        if (bursts[i].timeOff > bursts[i + 1].timeOn) {
            throw std::runtime_error("computeSonicationEventTimeline: overlapping burst events");
        }
    }

    std::vector<TimelineSegment> timeline;
    if (bursts.empty()) return timeline;

    if (bursts.front().timeOn != 0.0) {
        TimelineSegment gap;
        gap.isBurst = false;
        gap.txNum = 0;
        gap.timeOn = 0.0;
        gap.timeOff = bursts.front().timeOn;
        timeline.push_back(gap);
    }
    for (std::size_t i = 0; i < bursts.size(); ++i) {
        TimelineSegment burstSeg;
        burstSeg.isBurst = true;
        burstSeg.txNum = bursts[i].txNum;
        burstSeg.timeOn = bursts[i].timeOn;
        burstSeg.timeOff = bursts[i].timeOff;
        timeline.push_back(burstSeg);

        if (i + 1 < bursts.size()) {
            TimelineSegment gap;
            gap.isBurst = false;
            gap.txNum = 0;
            gap.timeOn = bursts[i].timeOff;
            gap.timeOff = bursts[i + 1].timeOn;
            timeline.push_back(gap);
        }
    }
    return timeline;
}

}  // namespace beam::stimulation
