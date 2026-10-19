#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "safety/checks.hpp"
#include "safety/intensity.hpp"
#include "safety/limits.hpp"

using namespace beam::safety;

TEST(Intensity, IsppaFormula) {
    // (1e6)^2 / (2*1040*1546) / 1e4
    EXPECT_NEAR(isppa(1.0), 31.09766, 1e-4);
    EXPECT_NEAR(isppa(3.0), 9.0 * 31.09766, 1e-3);  // scales with amplitude^2
    EXPECT_NEAR(isppa(1.0, 1046.0), 30.91924, 1e-4);  // getISPTAFromStimParams density
}

TEST(Intensity, MechanicalIndex) {
    EXPECT_NEAR(mechanicalIndex(1.9, 1.0), 1.9, 1e-12);
    EXPECT_NEAR(mechanicalIndex(2.0, 4.0), 1.0, 1e-12);  // 2 / sqrt(4)
}

TEST(Intensity, IsptaBurstDurationAndInterval) {
    SonicationSafetyParams p;
    p.amplitudeMPa = 1.0;
    p.pd = 0.005;
    p.pi = 0.01;
    p.bd = 0.1;
    p.bi = 1.0;
    const IsptaResult r = isptaFromParams(p);
    // I(1046) = 30.91924; numPulseTransmits = floor(0.1/0.01) = 10;
    // onOverOff = 10 * 0.005 / 0.1 = 0.5.
    EXPECT_NEAR(r.isptaBurstDuration, 30.91924 * 0.5, 1e-3);
    EXPECT_NEAR(r.isptaBurstInterval, 30.91924 * 0.5 * 0.1 / 1.0, 1e-3);
}

TEST(Limits, ConstantsAndSteeringRange) {
    EXPECT_DOUBLE_EQ(kIsptaThresholdWPerCm2, 0.720);
    EXPECT_DOUBLE_EQ(kIsppaThresholdWPerCm2, 190.0);
    EXPECT_DOUBLE_EQ(kMechanicalIndexThreshold, 1.9);
    EXPECT_DOUBLE_EQ(kMaxSonicationAmplitudeMPa, 3.0);

    Eigen::Matrix<double, 3, 2> expected;
    expected << -45, 45, -28, 28, -15, 15;
    EXPECT_TRUE(maxSteeringRangeDegrees().isApprox(expected));
}

namespace {
SonicationSafetyParams safeParams() {
    SonicationSafetyParams p;
    p.amplitudeMPa = 0.3;
    p.centerFrequencyMHz = 0.65;
    p.pd = 0.001;
    p.pi = 0.01;
    p.bd = 0.05;
    p.bi = 1.0;
    p.startTime = 0.0;
    p.endTime = 10.0;
    return p;
}
}  // namespace

TEST(CheckSonicationParameters, AllWithinLimitsPasses) {
    const SafetyReport r = checkSonicationParameters({safeParams()});
    EXPECT_TRUE(r.pass);
    EXPECT_TRUE(r.messages.empty());
}

TEST(CheckSonicationParameters, IsppaOverLimitFailsAndSkipsIsptaBlock) {
    SonicationSafetyParams p = safeParams();
    p.amplitudeMPa = 3.0;  // isppa ~ 280 > 190
    const SafetyReport r = checkSonicationParameters({p});
    EXPECT_FALSE(r.pass);
    ASSERT_EQ(r.messages.size(), 1u);  // only the ISPPA message; ISPTA/MI block skipped
    EXPECT_NE(r.messages[0].find("ISPPA"), std::string::npos);
}

TEST(CheckSonicationParameters, InvalidPdGreaterThanPi) {
    SonicationSafetyParams p = safeParams();
    p.pd = 0.02;  // pd > pi
    const SafetyReport r = checkSonicationParameters({p});
    EXPECT_FALSE(r.pass);
    EXPECT_EQ(r.messages[0], "[Rule 210] Invalid Parameter- Stimulation 1: PD > PI");
    ASSERT_EQ(r.violations.size(), 1u);
    EXPECT_EQ(r.violations[0].kind, ViolationKind::kPulseDurationOverInterval);
}

// The numbers are written on screen and into the log, so they are part of
// the contract: a rule keeps its number, and a retired one leaves a hole
// rather than having its number reused.
TEST(CheckSonicationParameters, RuleNumbersAreFixed) {
    EXPECT_EQ(ruleNumber(ViolationKind::kIsppa), 110);
    EXPECT_EQ(ruleNumber(ViolationKind::kIsptaBurstInterval), 120);
    EXPECT_EQ(ruleNumber(ViolationKind::kIsptaAll), 130);
    EXPECT_EQ(ruleNumber(ViolationKind::kMechanicalIndex), 140);
    EXPECT_EQ(ruleNumber(ViolationKind::kPulseDurationOverInterval), 210);
    EXPECT_EQ(ruleNumber(ViolationKind::kBurstDurationOverInterval), 220);
    EXPECT_EQ(ruleNumber(ViolationKind::kPulseIntervalOverBurst), 230);
    EXPECT_EQ(ruleNumber(ViolationKind::kBurstIntervalOverDuration), 240);
}

// Every message carries the number of the rule that produced it, and the
// two lists stay in step so a caller can act on one by reading the other.
TEST(CheckSonicationParameters, EveryMessageCarriesItsRuleNumber) {
    SonicationSafetyParams p = safeParams();
    p.amplitudeMPa = 9.0;  // ISPPA, and the schedule is still valid
    const SafetyReport r = checkSonicationParameters({p});
    EXPECT_FALSE(r.pass);
    ASSERT_EQ(r.messages.size(), r.violations.size());
    for (std::size_t i = 0; i < r.messages.size(); ++i) {
        const std::string expected =
            "[Rule " + std::to_string(ruleNumber(r.violations[i].kind)) + "] ";
        EXPECT_EQ(r.messages[i].rfind(expected, 0), 0u) << r.messages[i];
    }
}

TEST(CheckSonicationParameters, IsptaBurstIntervalOverLimit) {
    SonicationSafetyParams p = safeParams();
    p.amplitudeMPa = 1.0;
    p.pd = 0.005;
    p.pi = 0.01;
    p.bd = 0.1;
    p.bi = 1.0;  // isptaBurstInterval ~ 1.55 > 0.72
    const SafetyReport r = checkSonicationParameters({p});
    EXPECT_FALSE(r.pass);
    // Both ISPTA tests fail here, and the source reports them separately:
    // IsptaAll first, then the burst-interval one.
    ASSERT_EQ(r.messages.size(), 2u);
    EXPECT_NE(r.messages[0].find(": ISPTA "), std::string::npos);
    EXPECT_NE(r.messages[1].find("Burst Parameters ISPTA"), std::string::npos);
}

// IsptaAll averages over the whole sonication; IsptaBurstInterval averages
// over one burst interval. They agree only when the duration is a whole
// number of intervals -- the silent remainder at the end is the difference,
// and it is the reason the source computes both.
TEST(Intensity, IsptaAllAccountsForTheSilentRemainder) {
    SonicationSafetyParams p;
    p.amplitudeMPa = 1.0;
    p.centerFrequencyMHz = 0.3;
    p.pd = 0.005;
    p.pi = 0.01;
    p.bd = 0.1;
    p.bi = 1.0;
    p.startTime = 0.0;
    p.endTime = 3.0;  // exactly three burst intervals
    const IsptaResult whole = isptaFromParams(p);
    EXPECT_NEAR(isptaAll(p), whole.isptaBurstInterval, 1e-9);

    // Two and a half: two bursts fire, the last half-interval is silent, so
    // the average over the whole sonication is 2 * 0.1 / 2.5 of the
    // intensity rather than 0.1 / 1.0 of it.
    p.endTime = 2.5;
    const IsptaResult half = isptaFromParams(p);
    EXPECT_NEAR(isptaAll(p), half.isptaBurstInterval * (2.0 * 0.1 / 2.5) / (0.1 / 1.0), 1e-9);
    EXPECT_LT(isptaAll(p), half.isptaBurstInterval);
}

// A schedule the parameter checks have already rejected cannot be averaged
// over: the source leaves its own output unassigned there.
TEST(Intensity, IsptaAllIsZeroForAScheduleThatCannotRun) {
    SonicationSafetyParams p;
    p.amplitudeMPa = 1.0;
    p.pd = 0.02;
    p.pi = 0.01;  // PD > PI
    p.bd = 0.1;
    p.bi = 1.0;
    p.startTime = 0.0;
    p.endTime = 3.0;
    EXPECT_DOUBLE_EQ(isptaAll(p), 0.0);
}

TEST(CheckCouplingSafety, LowAttenuationWarns) {
    EXPECT_FALSE(checkCouplingSafety(0.05).pass);
    EXPECT_TRUE(checkCouplingSafety(0.5).pass);
    EXPECT_TRUE(checkCouplingSafety(0.5).messages.empty());
}

// The coupling warning is numbered like the rest, in its own band: it is
// about the measurement rather than about a sonication.
TEST(CheckCouplingSafety, CarriesItsRuleNumber) {
    EXPECT_EQ(ruleNumber(ViolationKind::kCouplingTooLow), 410);
    const SafetyReport r = checkCouplingSafety(0.05);
    EXPECT_FALSE(r.pass);
    ASSERT_EQ(r.messages.size(), 1u);
    EXPECT_EQ(r.messages[0], "[Rule 410] WARNING: Coupling transmission is abnormally low");
    ASSERT_EQ(r.violations.size(), 1u);
    EXPECT_EQ(r.violations[0].kind, ViolationKind::kCouplingTooLow);
}
