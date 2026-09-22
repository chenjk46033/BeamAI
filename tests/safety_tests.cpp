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
    EXPECT_EQ(r.messages[0], "Invalid Parameter- Stimulation 1: PD > PI");
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
    ASSERT_EQ(r.messages.size(), 1u);
    EXPECT_NE(r.messages[0].find("ISPTA"), std::string::npos);
}

TEST(CheckCouplingSafety, LowAttenuationWarns) {
    EXPECT_FALSE(checkCouplingSafety(0.05).pass);
    EXPECT_TRUE(checkCouplingSafety(0.5).pass);
    EXPECT_TRUE(checkCouplingSafety(0.5).messages.empty());
}
