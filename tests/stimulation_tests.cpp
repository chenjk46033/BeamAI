#include <cmath>
#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>

#include "array/array_types.hpp"
#include "stimulation/apodization.hpp"
#include "stimulation/delays.hpp"
#include "stimulation/duty_cycle.hpp"
#include "stimulation/events.hpp"
#include "stimulation/interp.hpp"
#include "stimulation/pause_intervals.hpp"
#include "stimulation/stim_freqs.hpp"
#include "stimulation/stim_params.hpp"

using namespace beam::stimulation;

namespace {

beam::array::ArrayStruct arrayWithElementsOnZ(const std::vector<double>& zMeters) {
    beam::array::ArrayStruct a;
    for (double z : zMeters) {
        beam::array::ArrayElement e;
        e.position = Eigen::Vector3d(0, 0, z);
        a.element.push_back(e);
    }
    return a;
}

}  // namespace

TEST(CalculateMultifrequencySuperpositionDelays, KnownValue) {
    Eigen::VectorXd freqs(2);
    freqs << 1.0, 2.0;
    const Eigen::VectorXd d = calculateMultifrequencySuperpositionDelays(freqs, 0.0);
    Eigen::VectorXd expected(2);
    expected << 0.25, 0.375;  // 2*max(0.25,0.125) - [0.25,0.125]
    EXPECT_TRUE(d.isApprox(expected));
}

TEST(FocusArrayAtPoint, SteeringDelaysAndFarthestElement) {
    const beam::array::ArrayStruct a = arrayWithElementsOnZ({0.0, 2.0});
    const FocusResult f = focusArrayAtPoint(a, Eigen::Vector3d(0, 0, 0), 1.0);
    EXPECT_EQ(f.mi, 2);
    EXPECT_TRUE(f.delaysTRaw.isApprox(Eigen::Vector2d(0.0, 2.0)));
    EXPECT_TRUE(f.delaysT.isApprox(Eigen::Vector2d(2.0, 0.0)));
}

TEST(GetApodFromAtt, ClampsAndNormalises) {
    Eigen::VectorXd att(3);
    att << 1.0, 2.0, 0.5;
    const ApodResult r = getApodFromAtt(10.0, att, 1.0);  // att -> [1,2,1], amps [10,5,10]
    EXPECT_DOUBLE_EQ(r.v, 10.0);
    Eigen::VectorXd expected(3);
    expected << 1.0, 0.5, 1.0;
    EXPECT_TRUE(r.apods.isApprox(expected));
}

TEST(Interp1, LinearAndOutOfRange) {
    Eigen::VectorXd x(3);
    x << 0.0, 1.0, 2.0;
    Eigen::VectorXd y(3);
    y << 0.0, 10.0, 30.0;
    EXPECT_DOUBLE_EQ(interp1(x, y, 0.5), 5.0);
    EXPECT_DOUBLE_EQ(interp1(x, y, 1.5), 20.0);
    EXPECT_DOUBLE_EQ(interp1(x, y, 2.0), 30.0);
    EXPECT_TRUE(std::isnan(interp1(x, y, -0.1)));
    EXPECT_TRUE(std::isnan(interp1(x, y, 2.1)));
}

TEST(PressureToDutyCycleGivenTransmission, CalibrationCurveLookup) {
    Eigen::VectorXd calDuty(7);
    calDuty << 40, 45, 56, 75, 80, 90, 97.5;
    Eigen::VectorXd calPressure(7);
    calPressure << 0.75, 1.12, 1.72, 2.95, 3.1, 3.72, 3.92;

    const double dc = pressureToDutyCycleGivenTransmission(0.65, 0.2, calDuty, calPressure);
    EXPECT_NEAR(dc, 0.65023, 1e-3);
    EXPECT_GE(dc, 0.4);
    EXPECT_LE(dc, 0.75);
}

TEST(DefineStimFreqs, KnownControlsAndErrors) {
    EXPECT_TRUE(defineStimFreqs("650", 4).isApprox(Eigen::VectorXd::Constant(4, 0.65)));
    EXPECT_TRUE(defineStimFreqs("high", 2).isApprox(Eigen::VectorXd::Constant(2, 0.7)));
    EXPECT_THROW(defineStimFreqs("MFS252", 4), std::invalid_argument);
    EXPECT_THROW(defineStimFreqs("bogus", 4), std::invalid_argument);
}

TEST(GetPauseIntervals, SplitsIntoChunks) {
    EXPECT_EQ(getPauseIntervals(2.5, 1.0), (std::vector<double>{1.0, 1.0, 0.5}));
    EXPECT_TRUE(getPauseIntervals(0.0, 1.0).empty());
    EXPECT_THROW(getPauseIntervals(2.0, 0.0), std::invalid_argument);
}

TEST(DefineStimParams, SingleGroupSingleTarget) {
    const beam::array::ArrayStruct a = arrayWithElementsOnZ({0.0, 0.1});
    const std::vector<std::vector<int>> txElements{{1, 2}};
    const std::vector<std::vector<int>> txElementsArray{{1, 2}};
    const std::vector<int> rxElements{1, 2};
    Eigen::MatrixX3d positions(1, 3);
    positions << 0, 0, 0;
    Eigen::VectorXd freqs(2);
    freqs << 0.65, 0.65;
    Eigen::VectorXd apods(2);
    apods << 1.0, 0.8;
    Eigen::VectorXd correctionDelays = Eigen::VectorXd::Zero(2);

    const std::vector<StimParams> out =
        defineStimParams(a, 1500.0, txElements, txElementsArray, rxElements, positions, freqs, apods,
                         correctionDelays);

    ASSERT_EQ(out.size(), 1u);
    const StimParams& sp = out[0];
    EXPECT_DOUBLE_EQ(sp.centerFrequencyMHz, 0.65);
    EXPECT_EQ(sp.waveform, 3);
    EXPECT_DOUBLE_EQ(sp.c, 1500.0);
    EXPECT_TRUE(sp.delaysSteering.isApprox(Eigen::Vector2d(0.1 / 1500.0, 0.0)));
    EXPECT_TRUE(sp.apods.isApprox(Eigen::Vector2d(1.0, 0.8)));
    EXPECT_NEAR(sp.delaysCycle(1), 0.25, 1e-9);      // pure waveform delay * 650 kHz
    EXPECT_NEAR(sp.delaysCycle(0), 43.5833, 1e-3);
    EXPECT_EQ(sp.txElementsArray, (std::vector<int>{1, 2}));
}

TEST(GetTxAndBurstEvents, ExpandsBurstsAndPulses) {
    SonicationSchedule s;
    s.startTime = 0.0;
    s.bi = 1.0;
    s.bd = 0.5;
    s.pi = 0.1;
    s.pd = 0.05;

    const TxAndBurstEvents ev = getTxAndBurstEvents(
        {s}, /*numBlockIntervals=*/{1}, /*blockPauseIntervals=*/{{}}, /*numBurstIntervals=*/{2},
        /*numPulseTransmits=*/{2});

    ASSERT_EQ(ev.burstEvents.size(), 2u);
    ASSERT_EQ(ev.txEvents.size(), 4u);
    EXPECT_DOUBLE_EQ(ev.burstEvents[0].timeOn, 0.0);
    EXPECT_DOUBLE_EQ(ev.burstEvents[0].timeOff, 0.5);
    EXPECT_EQ(ev.burstEvents[0].numPulseTransmits, 2);
    EXPECT_DOUBLE_EQ(ev.burstEvents[1].timeOn, 1.0);
    EXPECT_DOUBLE_EQ(ev.txEvents[3].timeOn, 1.1);
    EXPECT_DOUBLE_EQ(ev.txEvents[3].timeOff, 1.15);
}

TEST(GetTxAndBurstEvents, BlockPauseShiftsAllTimes) {
    SonicationSchedule s;
    s.startTime = 0.0;
    s.bi = 1.0;
    const TxAndBurstEvents ev = getTxAndBurstEvents({s}, {1}, {{0.2, 0.3}}, {1}, {1});
    ASSERT_EQ(ev.burstEvents.size(), 1u);
    EXPECT_DOUBLE_EQ(ev.burstEvents[0].timeOn, 0.5);  // startTime + sum(blockPause)
}

TEST(ComputeSonicationEventTimeline, BuildsGapFilledTimelineWithoutLeadingPauseWhenBurstStartsAtZero) {
    SonicationSchedule s;
    s.startTime = 0.0;
    s.endTime = 2.0;
    s.bi = 1.0;
    s.bd = 0.5;
    s.pi = 0.1;
    s.pd = 0.05;

    const std::vector<TimelineSegment> tl = computeSonicationEventTimeline({s});
    ASSERT_EQ(tl.size(), 3u);
    EXPECT_TRUE(tl[0].isBurst);
    EXPECT_DOUBLE_EQ(tl[0].timeOn, 0.0);
    EXPECT_DOUBLE_EQ(tl[0].timeOff, 0.5);
    EXPECT_FALSE(tl[1].isBurst);
    EXPECT_DOUBLE_EQ(tl[1].timeOn, 0.5);
    EXPECT_DOUBLE_EQ(tl[1].timeOff, 1.0);
    EXPECT_TRUE(tl[2].isBurst);
    EXPECT_DOUBLE_EQ(tl[2].timeOn, 1.0);
    EXPECT_DOUBLE_EQ(tl[2].timeOff, 1.5);
    // No trailing pause out to endTime -- the source's eventVector stops
    // right after the last burst, it never extends to the full duration.
}

TEST(ComputeSonicationEventTimeline, AddsLeadingPauseWhenFirstBurstDoesNotStartAtZero) {
    SonicationSchedule s;
    s.startTime = 0.3;
    s.endTime = 1.3;
    s.bi = 1.0;
    s.bd = 0.5;
    s.pi = 0.1;
    s.pd = 0.05;

    const std::vector<TimelineSegment> tl = computeSonicationEventTimeline({s});
    ASSERT_EQ(tl.size(), 2u);
    EXPECT_FALSE(tl[0].isBurst);
    EXPECT_DOUBLE_EQ(tl[0].timeOn, 0.0);
    EXPECT_DOUBLE_EQ(tl[0].timeOff, 0.3);
    EXPECT_TRUE(tl[1].isBurst);
    EXPECT_DOUBLE_EQ(tl[1].timeOn, 0.3);
}

TEST(ComputeSonicationEventTimeline, ThrowsOnInvalidSchedule) {
    SonicationSchedule s;
    s.startTime = 0.0;
    s.endTime = 2.0;
    s.bi = 1.0;
    s.bd = 0.05;  // BD < PI violates BD >= PI
    s.pi = 0.1;
    s.pd = 0.05;
    EXPECT_THROW(computeSonicationEventTimeline({s}), std::invalid_argument);
}

TEST(ComputeSonicationEventTimeline, ThrowsOnOverlappingBurstsAcrossTargets) {
    SonicationSchedule s1;
    s1.startTime = 0.0;
    s1.endTime = 10.0;
    s1.bi = 10.0;
    s1.bd = 5.0;
    s1.pi = 1.0;
    s1.pd = 0.5;

    SonicationSchedule s2;
    s2.startTime = 2.0;
    s2.endTime = 12.0;
    s2.bi = 10.0;
    s2.bd = 5.0;
    s2.pi = 1.0;
    s2.pd = 0.5;

    EXPECT_THROW(computeSonicationEventTimeline({s1, s2}), std::runtime_error);
}
