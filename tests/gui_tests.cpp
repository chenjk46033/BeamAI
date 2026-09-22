#include <algorithm>
#include <limits>
#include <string>
#include <vector>

#include <Eigen/Core>
#include <gtest/gtest.h>

#include "array/array_data.hpp"
#include "array/array_types.hpp"
#include "gui/case_report_form.hpp"
#include "gui/correction_tab_presenter.hpp"
#include "gui/countdown_presenter.hpp"
#include "gui/initial_placement_presenter.hpp"
#include "gui/mri_overlay_presenter.hpp"
#include "gui/registration_tab_presenter.hpp"
#include "gui/registration_check_presenter.hpp"
#include "gui/safety_presenter.hpp"
#include "gui/session_io.hpp"
#include "gui/sham_orchestrator.hpp"
#include "gui/sonicate_orchestrator.hpp"
#include "gui/sonication_tab_presenter.hpp"
#include "gui/top_targets.hpp"
#include "gui/treatment_session_store.hpp"
#include "gui/treatment_workflow.hpp"
#include "mri/ras_transform.hpp"
#include "safety/intensity.hpp"
#include "sham/sham_audio.hpp"

using beam::gui::checkSonicationSafety;
using beam::gui::computeAvgTransmissionBars;
using beam::gui::computeRfPlotYLimit;
using beam::gui::SonicationSafetyPreconditions;
using beam::gui::SonicationSafetyReport;
using beam::safety::SonicationSafetyParams;

namespace {

// A sonication that passes every beam::safety::checkSonicationParameters
// check: amp low enough for ISPPA/MI, a short pulse duration so the
// burst-interval ISPTA stays well under 0.720 W/cm^2, and
// PD < PI < BD < BI < duration.
SonicationSafetyParams cleanSonication() {
    SonicationSafetyParams s;
    s.amplitudeMPa = 0.7;
    s.centerFrequencyMHz = 0.65;
    s.pd = 0.001;
    s.pi = 0.05;
    s.bd = 0.4;
    s.bi = 0.5;
    s.startTime = 0.0;
    s.endTime = 10.0;
    return s;
}

SonicationSafetyPreconditions okPreconditions() {
    SonicationSafetyPreconditions pre;
    pre.currentRegistrationComplete = true;
    pre.mriRegistrationComplete = true;
    pre.throughTransmitAtt = 0.9;
    pre.attenuationThreshold = 0.5;
    return pre;
}

bool hasMessageContaining(const SonicationSafetyReport& r, const std::string& needle) {
    return std::any_of(r.messages.begin(), r.messages.end(),
                       [&](const std::string& m) { return m.find(needle) != std::string::npos; });
}

}  // namespace

TEST(CheckSonicationSafety, CleanCasePassesAndReportsOnline) {
    const SonicationSafetyReport r = checkSonicationSafety({cleanSonication()}, okPreconditions());
    EXPECT_TRUE(r.pass);
    EXPECT_TRUE(r.messages.empty());
    EXPECT_EQ(r.statusText, "Sonication Online");
}

TEST(CheckSonicationSafety, RegistrationIncompleteFailsWithMessage) {
    SonicationSafetyPreconditions pre = okPreconditions();
    pre.mriRegistrationComplete = false;

    const SonicationSafetyReport r = checkSonicationSafety({cleanSonication()}, pre);
    EXPECT_FALSE(r.pass);
    EXPECT_TRUE(hasMessageContaining(r, "Registration incomplete"));
    EXPECT_NE(r.statusText, "Sonication Online");
}

TEST(CheckSonicationSafety, ThroughTransmitBelowThresholdFails) {
    SonicationSafetyPreconditions pre = okPreconditions();
    pre.throughTransmitAtt = 0.05;  // < 0.5

    const SonicationSafetyReport r = checkSonicationSafety({cleanSonication()}, pre);
    EXPECT_FALSE(r.pass);
    EXPECT_TRUE(hasMessageContaining(r, "Through transmit too low"));
}

TEST(CheckSonicationSafety, InvalidParametersPropagateFromSafetyLayer) {
    SonicationSafetyParams bad = cleanSonication();
    bad.pd = 0.10;  // pd > pi -> "PD > PI"

    const SonicationSafetyReport r = checkSonicationSafety({bad}, okPreconditions());
    EXPECT_FALSE(r.pass);
    EXPECT_TRUE(hasMessageContaining(r, "PD > PI"));
}

// --- setRegistrationCheck.m ---

TEST(ComputeRegistrationCheckLampState, BothCompleteEverythingOn) {
    const beam::gui::RegistrationCheckLampState s = beam::gui::computeRegistrationCheckLampState(
        /*mriRegistrationComplete=*/true, /*currentRegistrationComplete=*/true);
    EXPECT_TRUE(s.rightLampOn);
    EXPECT_TRUE(s.leftLampOn);
    EXPECT_TRUE(s.insideMriLampOn);
    EXPECT_TRUE(s.sonicateButtonEnabled);
}

TEST(ComputeRegistrationCheckLampState, MriCompleteOnlyRightLampStaysOffQuirk) {
    // Disclosed source quirk: the source's second `if` block (keyed on
    // currentRegistrationComplete) always overwrites the Right lamp's
    // color set by the first block -- so an MRI-only registration leaves
    // the Right lamp off despite InsideMRI being on.
    const beam::gui::RegistrationCheckLampState s = beam::gui::computeRegistrationCheckLampState(
        /*mriRegistrationComplete=*/true, /*currentRegistrationComplete=*/false);
    EXPECT_FALSE(s.rightLampOn);
    EXPECT_FALSE(s.leftLampOn);
    EXPECT_TRUE(s.insideMriLampOn);
    EXPECT_FALSE(s.sonicateButtonEnabled);
}

TEST(ComputeRegistrationCheckLampState, CurrentCompleteOnlyRightAndLeftOn) {
    const beam::gui::RegistrationCheckLampState s = beam::gui::computeRegistrationCheckLampState(
        /*mriRegistrationComplete=*/false, /*currentRegistrationComplete=*/true);
    EXPECT_TRUE(s.rightLampOn);
    EXPECT_TRUE(s.leftLampOn);
    EXPECT_FALSE(s.insideMriLampOn);
    EXPECT_FALSE(s.sonicateButtonEnabled);
}

TEST(ComputeRegistrationCheckLampState, NeitherCompleteEverythingOff) {
    const beam::gui::RegistrationCheckLampState s =
        beam::gui::computeRegistrationCheckLampState(false, false);
    EXPECT_FALSE(s.rightLampOn);
    EXPECT_FALSE(s.leftLampOn);
    EXPECT_FALSE(s.insideMriLampOn);
    EXPECT_FALSE(s.sonicateButtonEnabled);
}

TEST(CheckSonicationSafety, MultipleFailuresAllReported) {
    SonicationSafetyParams bad = cleanSonication();
    bad.bd = 0.6;  // bd > bi -> "BD > BI"

    SonicationSafetyPreconditions pre = okPreconditions();
    pre.currentRegistrationComplete = false;
    pre.throughTransmitAtt = 0.0;

    const SonicationSafetyReport r = checkSonicationSafety({bad}, pre);
    EXPECT_FALSE(r.pass);
    EXPECT_TRUE(hasMessageContaining(r, "BD > BI"));
    EXPECT_TRUE(hasMessageContaining(r, "Registration incomplete"));
    EXPECT_TRUE(hasMessageContaining(r, "Through transmit too low"));
    EXPECT_GE(r.messages.size(), 3u);
}

// --- Correction tab presenter ---

TEST(ComputeAvgTransmissionBars, PassAndColorComeFromDifferentQuantities) {
    // Disclosed source quirk: bar color is `att < 0.1`, pass is
    // `transmissionPeak2Peak > couplingThreshold` -- independent.
    // att above 0.1 (green bar) but p2p below threshold (fail):
    const beam::gui::AvgTransmissionBars a = computeAvgTransmissionBars(0.15, 0.05, 0.10);
    EXPECT_FALSE(a.currentBarIsRed);
    EXPECT_FALSE(a.pass);

    // att below 0.1 (red bar) but p2p above threshold (pass):
    const beam::gui::AvgTransmissionBars b = computeAvgTransmissionBars(0.05, 0.20, 0.10);
    EXPECT_TRUE(b.currentBarIsRed);
    EXPECT_TRUE(b.pass);

    EXPECT_DOUBLE_EQ(b.currentValue, 0.05);
    EXPECT_DOUBLE_EQ(b.averageValue, 0.2);
    EXPECT_DOUBLE_EQ(b.averageStd, 0.1);
}

TEST(ComputeRfPlotYLimit, MaxAbsAcrossBothPlusTen) {
    Eigen::VectorXd x0(3);
    x0 << 1.0, -4.0, 2.0;
    Eigen::VectorXd x1(3);
    x1 << 7.0, -3.0, 0.0;
    EXPECT_DOUBLE_EQ(computeRfPlotYLimit(x0, x1), 17.0);  // max(4, 7) + 10
    EXPECT_DOUBLE_EQ(computeRfPlotYLimit(Eigen::VectorXd(), Eigen::VectorXd()), 10.0);
}

TEST(CorrectionInitialState, MatchesInitializeCorrectionValuesLiterals) {
    const beam::gui::CorrectionInitialState s = beam::gui::correctionInitialState();
    EXPECT_DOUBLE_EQ(s.medianAttenuationCh0, 1.0);
    EXPECT_DOUBLE_EQ(s.medianAttenuationCh1, 1.0);
    EXPECT_DOUBLE_EQ(s.ch0Max, 1.0);
    EXPECT_DOUBLE_EQ(s.ch1Max, 1.0);
    EXPECT_DOUBLE_EQ(s.correctionVal, 1.0);
    EXPECT_DOUBLE_EQ(s.att, 1.0);
    EXPECT_DOUBLE_EQ(s.calibrationVal, 1.0);
}

// --- Case Report Form (UIFeatures slice) ---

TEST(CaseReportForm, AutoSaveFilenameMatchesSourceFormat) {
    beam::gui::CaseReportForm crf;
    crf.siteId = "Utah";
    crf.subjectId = "P042";
    crf.visitNumber = 3;
    crf.month = 9;
    crf.day = 10;
    crf.year = 2026;
    // ['S', SiteID, ParticipantID, 'Visit', vn, 'DateM', mo, 'D', dy, 'Y', yr]
    EXPECT_EQ(beam::gui::computeCrfAutoSaveFilename(crf), "SUtahP042Visit3DateM9D10Y2026");
}

TEST(CaseReportForm, BuildSessionFillsConstantsAndFilename) {
    beam::gui::SessionCrfInputs in;
    in.participantId = "P007";
    in.visitNumber = 1;
    in.hydrogelSize = "Medium";
    in.year = 2026;
    in.month = 12;
    in.day = 25;
    in.hour = 14;
    in.minute = 30;
    in.second = 5;

    const beam::gui::CaseReportForm crf = beam::gui::buildSessionCaseReportForm(in);

    EXPECT_EQ(crf.siteId, "Utah");
    EXPECT_EQ(crf.snBeam, "BeamV01");
    EXPECT_EQ(crf.snTransducer1, "xdr001");
    EXPECT_EQ(crf.snTransducer2, "xdr002");
    EXPECT_EQ(crf.softwareVersion, "V1.0.0");
    EXPECT_EQ(crf.subjectId, "P007");   // SubjectID == ParticipantID
    EXPECT_EQ(crf.visitNumber, 1);
    EXPECT_EQ(crf.hydrogelSize, "Medium");
    EXPECT_EQ(crf.hour, 14);
    EXPECT_TRUE(crf.operatorId.empty());  // setCRFOperatorID commented out in source
    EXPECT_EQ(crf.autoSaveFilename, "SUtahP007Visit1DateM12D25Y2026");
}

// --- RegistrationTab presenter ---

TEST(GetAcpcTransform, HandComputedAxes) {
    const Eigen::Vector3d ac(3, 4, 0);
    const Eigen::Vector3d pc(0, 0, 0);
    const Eigen::Vector3d normal(1, 0, 0);
    const Eigen::Vector3d referenceCoords(1, 2, 3);

    const Eigen::Matrix4d t = beam::gui::getAcpcTransform(ac, pc, normal, referenceCoords);

    const Eigen::RowVector4d lastRow = t.row(3);
    EXPECT_TRUE(lastRow.isApprox(Eigen::RowVector4d(0, 0, 0, 1)));
    // Hand-computed: Y = normalize(ac-pc) with Y(0) flipped = (-0.6,0.8,0);
    // X = normalize(normal) + dot(X,Y)*Y = (1.36,-0.48,0) (no X(0) flip
    // needed); Z = normalize(cross(X,Y)) = (0,0,1) (Z(1) flip is a no-op
    // here); origin = -referenceCoords.
    const Eigen::Vector3d xCol = t.block<3, 1>(0, 0);
    const Eigen::Vector3d yCol = t.block<3, 1>(0, 1);
    const Eigen::Vector3d zCol = t.block<3, 1>(0, 2);
    const Eigen::Vector3d originCol = t.block<3, 1>(0, 3);
    EXPECT_TRUE(xCol.isApprox(Eigen::Vector3d(1.36, -0.48, 0), 1e-9));
    EXPECT_TRUE(yCol.isApprox(Eigen::Vector3d(-0.6, 0.8, 0), 1e-9));
    EXPECT_TRUE(zCol.isApprox(Eigen::Vector3d(0, 0, 1), 1e-9));
    EXPECT_TRUE(originCol.isApprox(Eigen::Vector3d(-1, -2, -3), 1e-9));
}

TEST(GetAcpcTransform, ThrowsWhenZAxisPointsWrongWay) {
    // Same landmarks with AC/PC swapped flips the sign of Z's third
    // component (hand-verified: -0.8 instead of +0.8).
    EXPECT_THROW(beam::gui::getAcpcTransform(Eigen::Vector3d(0, 0, 0), Eigen::Vector3d(3, 4, 0),
                                              Eigen::Vector3d(1, 0, 0), Eigen::Vector3d::Zero()),
                std::runtime_error);
}

TEST(ApplyReferenceCoordinateTransform, MriToReferenceThenReferenceToMriRoundTrips) {
    Eigen::Matrix4d t = Eigen::Matrix4d::Identity();
    t.block<3, 1>(0, 3) = Eigen::Vector3d(1, 2, 3);  // pure translation

    const Eigen::Vector3d mri(0, 0, 0);
    const Eigen::Vector3d ref = beam::gui::applyReferenceCoordinateTransform(
        t, mri, beam::gui::ReferenceTransformDirection::kMriToReference);
    EXPECT_TRUE(ref.isApprox(Eigen::Vector3d(1, 2, 3)));

    const Eigen::Vector3d back = beam::gui::applyReferenceCoordinateTransform(
        t, ref, beam::gui::ReferenceTransformDirection::kReferenceToMri);
    EXPECT_TRUE(back.isApprox(mri));
}

TEST(ApplyReferenceCoordinateTransform, RotationAppliedAfterTranslation) {
    Eigen::Matrix4d t = Eigen::Matrix4d::Identity();
    t.block<3, 3>(0, 0) << 0, -1, 0, 1, 0, 0, 0, 0, 1;  // +90 deg about Z

    const Eigen::Vector3d out = beam::gui::applyReferenceCoordinateTransform(
        t, Eigen::Vector3d(1, 0, 0), beam::gui::ReferenceTransformDirection::kMriToReference);
    EXPECT_TRUE(out.isApprox(Eigen::Vector3d(0, 1, 0), 1e-9));
}

TEST(GetArrayFiducialNormals, AxisPermutedNormalAndEdgeVector) {
    std::vector<beam::registration::FiducialMarker> fm(3);
    fm[0].position = Eigen::Vector3d(0, 0, 0);
    fm[1].position = Eigen::Vector3d(1, 0, 0);
    fm[2].position = Eigen::Vector3d(0, 1, 0);

    const beam::gui::ArrayFiducialNormals n = beam::gui::getArrayFiducialNormals(fm);
    // normalVectorFrom3Points(p1,p2,p3) for these points is (0,0,1)
    // (Geometry.NormalVectorFrom3PointsPicksZeroFacingDirection in
    // array_tests.cpp); v1 = fm[0]-fm[1] = (-1,0,0). Both permuted
    // (x,y,z) -> (z,x,y).
    EXPECT_TRUE(n.v1.isApprox(Eigen::Vector3d(0, -1, 0)));
    EXPECT_TRUE(n.v2.isApprox(Eigen::Vector3d(1, 0, 0)));
}

TEST(GetArrayFiducialNormals, ThrowsWithFewerThanThreeMarkers) {
    std::vector<beam::registration::FiducialMarker> fm(2);
    EXPECT_THROW(beam::gui::getArrayFiducialNormals(fm), std::invalid_argument);
}

// --- SonicationTab presenter ---

TEST(GetResponseFromTreatmentProtocolData, ClampsToPlusMinusTwoAndSums) {
    const beam::gui::MoodPainResponse r = beam::gui::getResponseFromTreatmentProtocolData(5.0, -7.0);
    EXPECT_EQ(r.mood, 2);
    EXPECT_EQ(r.pain, -2);
    EXPECT_EQ(r.total, 0);
}

TEST(GetResponseFromTreatmentProtocolData, NanMapsToZero) {
    const beam::gui::MoodPainResponse r =
        beam::gui::getResponseFromTreatmentProtocolData(std::numeric_limits<double>::quiet_NaN(), 1.0);
    EXPECT_EQ(r.mood, 0);
    EXPECT_EQ(r.pain, 1);
    EXPECT_EQ(r.total, 1);
}

TEST(GetNewProtocolName, NoCollisionUsesPlainName) {
    EXPECT_EQ(beam::gui::getNewProtocolName({"Protocol 1", "Protocol 2", "New"}), "Protocol 3");
}

TEST(GetNewProtocolName, CollisionAppendsThenOverwritesLastChar) {
    // N=2 -> newName starts as "Protocol 2", which collides with
    // existingProtocolNames[0] (the only entry checked -- "1:end-1"
    // excludes the last, "New"); j=1's collision handling appends "_01".
    EXPECT_EQ(beam::gui::getNewProtocolName({"Protocol 2", "New"}), "Protocol 2_01");
}

TEST(ColorMapRgb, HasEightySevenRowsMatchingSourceBug) {
    const Eigen::MatrixX3d m = beam::gui::colorMapRgb();
    ASSERT_EQ(m.rows(), 87);
    EXPECT_TRUE(m.row(0).isApprox(Eigen::RowVector3d(0, 1, 0)));
    EXPECT_TRUE(m.row(12).isApprox(Eigen::RowVector3d(0, 0.5, 0)));
    // First scaled row (0-based row 13): scale = 0.1 -> (0.01, 0.1, 0).
    EXPECT_TRUE(m.row(13).isApprox(Eigen::RowVector3d(0.01, 0.1, 0), 1e-9));
    // Last row (86): scale = 0.1 + 73*0.8/86 ~ 0.77907 -- never reaches 0.9.
    const double lastScale = 0.1 + 73.0 * 0.8 / 86.0;
    Eigen::RowVector3d expectedLast(0.1 * lastScale, lastScale, 0.0);
    EXPECT_TRUE(m.row(86).isApprox(expectedLast, 1e-9));
}

TEST(GetCurrentShownSonication, ReturnsLastCheckedRowOneBased) {
    EXPECT_EQ(beam::gui::getCurrentShownSonication({true, false, true, false}), 3);
}

TEST(GetCurrentShownSonication, DefaultsToOneWhenNoneChecked) {
    EXPECT_EQ(beam::gui::getCurrentShownSonication({false, false, false}), 1);
    EXPECT_EQ(beam::gui::getCurrentShownSonication({}), 1);
}

TEST(SortSonicationTableOrder, ReturnsStableAscendingPermutation) {
    const std::vector<int> idx = beam::gui::sortSonicationTableOrder({3.0, 1.0, 1.0, 2.0});
    EXPECT_EQ(idx, (std::vector<int>{1, 2, 3, 0}));  // ties (index 1,2) keep original order
}

TEST(ComputeTreatmentRowColor, NegativeIsYellowZeroIsWhitePositiveIsScaledGreen) {
    const beam::gui::RowColor negative = beam::gui::computeTreatmentRowColor(-1.0);
    EXPECT_DOUBLE_EQ(negative.r, 1.0);
    EXPECT_DOUBLE_EQ(negative.g, 1.0);
    EXPECT_DOUBLE_EQ(negative.b, 0.0);

    const beam::gui::RowColor zero = beam::gui::computeTreatmentRowColor(0.0);
    EXPECT_DOUBLE_EQ(zero.r, 1.0);
    EXPECT_DOUBLE_EQ(zero.g, 1.0);
    EXPECT_DOUBLE_EQ(zero.b, 1.0);

    const beam::gui::RowColor mid = beam::gui::computeTreatmentRowColor(2.0);
    EXPECT_DOUBLE_EQ(mid.r, 0.0);
    EXPECT_DOUBLE_EQ(mid.g, 0.75);  // 0.5 + 0.5*(2/4)
    EXPECT_DOUBLE_EQ(mid.b, 0.0);

    // currResponse > 4 clamps to the same full-intensity green as 4.
    const beam::gui::RowColor high = beam::gui::computeTreatmentRowColor(10.0);
    const beam::gui::RowColor atCap = beam::gui::computeTreatmentRowColor(4.0);
    EXPECT_DOUBLE_EQ(high.g, atCap.g);
    EXPECT_DOUBLE_EQ(high.g, 1.0);
}

TEST(AccFlagForProtocolName, MapsProtocolNamesToTheirRankingRegion) {
    // 'ACC' matches neither the SCC nor aMCC branch in the ranking
    // function, so PainACC falls into the interleaved "other" ranking.
    EXPECT_EQ(beam::gui::accFlagForProtocolName("PainACC"), beam::gui::AccFlag::kOther);
    EXPECT_EQ(beam::gui::accFlagForProtocolName("painacc"), beam::gui::AccFlag::kOther);
    // Only the first element of the {'SCC','aMCC'} pair is ever read.
    EXPECT_EQ(beam::gui::accFlagForProtocolName("PainSCCandAMCC"), beam::gui::AccFlag::kScc);
    // Every other protocol name falls into the default {'aMCC','SCC'} branch.
    EXPECT_EQ(beam::gui::accFlagForProtocolName("PainAMCCandSCC"), beam::gui::AccFlag::kAmcc);
    EXPECT_EQ(beam::gui::accFlagForProtocolName("PTSD"), beam::gui::AccFlag::kAmcc);
    EXPECT_EQ(beam::gui::accFlagForProtocolName("Default"), beam::gui::AccFlag::kAmcc);
}

TEST(NewProtocolName, NoCollisionUsesPlainCountedName) {
    EXPECT_EQ(beam::gui::newProtocolName({}), "Protocol 0");
    EXPECT_EQ(beam::gui::newProtocolName({"Protocol 0"}), "Protocol 1");
}

TEST(NewProtocolName, FirstCollisionAppendsUnderscoreZeroOne) {
    // n=2, compares against existingNames[0..0] ("Protocol 1" is excluded
    // as the last entry, per the source's own `1:end-1`).
    EXPECT_EQ(beam::gui::newProtocolName({"Protocol 2", "Protocol 1"}), "Protocol 2_01");
}

TEST(NewProtocolName, RepeatCollisionOverwritesLastCharQuirk) {
    // n=3 -> base name "Protocol 3"; compareCount=2 excludes the last
    // entry. Index 0 ("Protocol 3_01") is checked *before* index 1
    // ("Protocol 3") within each j-pass, so it doesn't match on pass
    // j=1 (newName is still "Protocol 3" at that point) -- only index 1
    // matches on j=1, renaming to "Protocol 3_01". On pass j=2, index 0
    // now matches the renamed value, and (j != 1) overwrites the last
    // character instead of appending "_01" again: "Protocol 3_02".
    EXPECT_EQ(beam::gui::newProtocolName({"Protocol 3_01", "Protocol 3", "Protocol 1"}),
              "Protocol 3_02");
}

TEST(ExampleTargetHelpText, VimGetsItsOwnTextEverythingElseGetsTheDefault) {
    const std::string defaultText = beam::gui::exampleTargetHelpText("SCC1");
    EXPECT_EQ(beam::gui::exampleTargetHelpText("ACC"), defaultText);
    EXPECT_EQ(beam::gui::exampleTargetHelpText("aMCC2"), defaultText);
    EXPECT_EQ(beam::gui::exampleTargetHelpText("unknown-flag"), defaultText);
    EXPECT_NE(beam::gui::exampleTargetHelpText("VIM"), defaultText);
}

TEST(DefaultArrayFramePosition, StartsAtSliderMinimumNoShift) {
    const beam::gui::ArrayFramePosition p = beam::gui::defaultArrayFramePosition();
    EXPECT_DOUBLE_EQ(p.horizontal, 1.0);
    EXPECT_DOUBLE_EQ(p.vertical, 1.0);
}

TEST(ComputePulseWaveformPlot, StepsDownAfterPulseDuration) {
    const beam::gui::PulseWaveformPlot p = beam::gui::computePulseWaveformPlot(2e-6, 5e-6, 10.0);
    ASSERT_EQ(p.x.size(), 4);
    EXPECT_DOUBLE_EQ(p.y(0), 10.0);
    EXPECT_DOUBLE_EQ(p.y(1), 10.0);
    EXPECT_DOUBLE_EQ(p.y(2), 0.0);
    EXPECT_DOUBLE_EQ(p.y(3), 0.0);
}

TEST(ComputeBurstWaveformPlot, RepeatsPulseUntilBurstDurationThenZero) {
    // BD=0.5, BI=1.0, PD=0.1, PI=0.3, amplitude=7 -- checked well inside
    // each region, away from any sample-boundary precision edge.
    const beam::gui::PulseWaveformPlot p = beam::gui::computeBurstWaveformPlot(0.5, 1.0, 0.1, 0.3, 7.0);
    ASSERT_EQ(p.x.size(), 100001);
    EXPECT_DOUBLE_EQ(p.y(0), 7.0);       // x=0, first pulse on
    EXPECT_DOUBLE_EQ(p.y(5000), 7.0);    // x=0.05, still first pulse
    EXPECT_DOUBLE_EQ(p.y(15000), 0.0);   // x=0.15, between pulses
    EXPECT_DOUBLE_EQ(p.y(90000), 0.0);   // x=0.9, past burst duration
}

// --- getTopTargetsFromTreatmentProtocolTable ---

namespace {
beam::gui::TargetResponse tr(int num, std::string name, double response, double duration = 30.0) {
    beam::gui::TargetResponse r;
    r.sonicationNumber = num;
    r.name = std::move(name);
    r.numericResponse = response;
    r.duration = duration;
    return r;
}
}  // namespace

TEST(GetTopTargets, TrimsToThreeDroppingLowestWhenNoTies) {
    const std::vector<beam::gui::TargetResponse> responses = {
        tr(1, "A", 4), tr(2, "B", 3), tr(3, "C", 2), tr(4, "D", 1)};
    const beam::gui::BestTargets best =
        beam::gui::getTopTargetsFromTreatmentProtocolTable(responses, beam::gui::AccFlag::kOther);
    EXPECT_EQ(best.name, (std::vector<std::string>{"A", "B", "C"}));
    EXPECT_EQ(best.numericResponse, (std::vector<double>{4, 3, 2}));
    EXPECT_EQ(best.sonicationNumber, (std::vector<int>{1, 2, 3}));
}

TEST(GetTopTargets, TrimTieBrokenTowardBetterRankedRegion) {
    // A=5, B=4, then a tie at 3 between SCC6 and aMCC6, plus E=1 (5 total,
    // need to drop 2). aMCC6 is the last (worst) entry of the "other"
    // ranking, SCC6 the one before it -- on the tie, aMCC6 (worse) should
    // be the one dropped, keeping SCC6.
    const std::vector<beam::gui::TargetResponse> responses = {
        tr(1, "A", 5), tr(2, "B", 4), tr(3, "SCC6", 3), tr(4, "aMCC6", 3), tr(5, "E", 1)};
    const beam::gui::BestTargets best =
        beam::gui::getTopTargetsFromTreatmentProtocolTable(responses, beam::gui::AccFlag::kOther);
    EXPECT_EQ(best.name, (std::vector<std::string>{"A", "B", "SCC6"}));
}

TEST(GetTopTargets, ZeroPositivesBackfillFromRankingRequiresNonNegative) {
    // No positive responses. "aMCC1" (-1, negative) is ranked ahead of
    // "SCC3" (0) but must be skipped (backfill requires response >= 0);
    // "SCC3" is the first ranked, non-negative match.
    const std::vector<beam::gui::TargetResponse> responses = {
        tr(1, "Other", -2), tr(2, "SCC3", 0), tr(3, "aMCC1", -1)};
    const beam::gui::BestTargets best =
        beam::gui::getTopTargetsFromTreatmentProtocolTable(responses, beam::gui::AccFlag::kOther);
    ASSERT_EQ(best.name.size(), 1u);
    EXPECT_EQ(best.name[0], "SCC3");
    EXPECT_DOUBLE_EQ(best.numericResponse[0], 0.0);
}

TEST(GetTopTargets, NegativeFallbackTieBrokenTowardBestRankedRegion) {
    // All negative, no ranked name has response >= 0 to backfill with, so
    // the negative fallback kicks in: SCC2 and aMCC2 tie at -1 (both less
    // negative than Other1's -5); SCC2 is ranked ahead of aMCC2 in the
    // "other" list, so it should win the tie (opposite direction from the
    // trim tie-break).
    const std::vector<beam::gui::TargetResponse> responses = {
        tr(1, "Other1", -5), tr(2, "SCC2", -1), tr(3, "aMCC2", -1)};
    const beam::gui::BestTargets best =
        beam::gui::getTopTargetsFromTreatmentProtocolTable(responses, beam::gui::AccFlag::kOther);
    ASSERT_EQ(best.name.size(), 1u);
    EXPECT_EQ(best.name[0], "SCC2");
    EXPECT_DOUBLE_EQ(best.numericResponse[0], -1.0);
}

TEST(GetTopTargets, EmptyInputPadsWithShamThenFillsFromRanking) {
    const beam::gui::BestTargets best =
        beam::gui::getTopTargetsFromTreatmentProtocolTable({}, beam::gui::AccFlag::kOther);
    // Source quirk: only `name` gets padded/filled; the other three arrays
    // are left empty (a genuine length mismatch in the source).
    EXPECT_EQ(best.name, (std::vector<std::string>{"SCC1", "aMCC1", "SCC2"}));
    EXPECT_TRUE(best.sonicationNumber.empty());
    EXPECT_TRUE(best.numericResponse.empty());
    EXPECT_TRUE(best.duration.empty());
}

TEST(GetTopTargets, LongDurationEntriesAreExcludedFromBlockA) {
    const std::vector<beam::gui::TargetResponse> responses = {tr(1, "SCC1", 5, /*duration=*/90.0)};
    const beam::gui::BestTargets best =
        beam::gui::getTopTargetsFromTreatmentProtocolTable(responses, beam::gui::AccFlag::kOther);
    // The one entry is excluded (duration >= 60), so this is the same
    // empty-input path as above.
    EXPECT_EQ(best.name, (std::vector<std::string>{"SCC1", "aMCC1", "SCC2"}));
}

TEST(GetTopTargets, ShamGroupIsExcludedCaseInsensitively) {
    const std::vector<beam::gui::TargetResponse> responses = {tr(1, "sham", 5), tr(2, "SCC4", 2)};
    const beam::gui::BestTargets best =
        beam::gui::getTopTargetsFromTreatmentProtocolTable(responses, beam::gui::AccFlag::kOther);
    ASSERT_EQ(best.name.size(), 1u);
    EXPECT_EQ(best.name[0], "SCC4");
}

namespace {

beam::mri::RasAxisVectors linspaceAxes(double lo, double hi, Eigen::Index n) {
    beam::mri::RasAxisVectors axes;
    axes.dimLR = Eigen::VectorXd::LinSpaced(n, lo, hi);
    axes.dimAP = Eigen::VectorXd::LinSpaced(n, lo, hi);
    axes.dimIS = Eigen::VectorXd::LinSpaced(n, lo, hi);
    return axes;
}

}  // namespace

TEST(ImagePositionToVoxelIndex, NearestIndexPerAxisIsZeroBased) {
    beam::mri::RasAxisVectors axes;
    axes.dimLR = Eigen::VectorXd::LinSpaced(6, -5.0, 5.0);  // -5,-3,-1,1,3,5
    axes.dimAP = axes.dimLR;
    axes.dimIS = axes.dimLR;

    const beam::gui::VoxelIndex idx = beam::gui::imagePositionToVoxelIndex(Eigen::Vector3d(2.2, -4.9, 0.0), axes);
    EXPECT_EQ(idx.i, 4);  // axis[4]==3, dist 0.8, closer than axis[3]==1 at dist 1.2
    EXPECT_EQ(idx.j, 0);  // nearest to -5
    EXPECT_EQ(idx.k, 2);  // -1 and 1 tie at dist 1; MATLAB's min (and this port) keeps the first, -1
}

TEST(RasterizeArrayOntoMriGrid, MarksVoxelsUnderASingleElement) {
    const beam::mri::RasAxisVectors axes = linspaceAxes(-10.0, 10.0, 21);  // 1mm spacing

    beam::array::ArrayElement element;
    element.corners.col(0) = Eigen::Vector3d(0.004, 0.004, 0.000);  // meters
    element.corners.col(1) = Eigen::Vector3d(0.006, 0.004, 0.000);
    element.corners.col(2) = Eigen::Vector3d(0.006, 0.006, 0.000);
    element.corners.col(3) = Eigen::Vector3d(0.004, 0.006, 0.000);

    beam::array::ArrayStruct arrayTotal;
    arrayTotal.element = {element};

    const beam::mri::Volume3D mask = beam::gui::rasterizeArrayOntoMriGrid(arrayTotal, axes, 21, 21, 21);
    ASSERT_EQ(mask.nx, 21);
    ASSERT_EQ(mask.nz, 21);

    double total = 0.0;
    for (const Eigen::MatrixXd& slice : mask.kSlices) {
        total += slice.sum();
    }
    EXPECT_GT(total, 0.0);  // the 4-6mm x 4-6mm face rasterizes to at least one voxel

    // Every marked voxel should fall within the element's 4-6mm footprint (+/- 1 voxel).
    for (Eigen::Index k = 0; k < mask.nz; ++k) {
        for (Eigen::Index i = 0; i < mask.nx; ++i) {
            for (Eigen::Index j = 0; j < mask.ny; ++j) {
                if (mask(i, j, k) != 0.0) {
                    EXPECT_NEAR(axes.dimLR(i), 5.0, 3.0);
                    EXPECT_NEAR(axes.dimAP(j), 5.0, 3.0);
                }
            }
        }
    }
}

TEST(RasterizeArrayOntoMriGrid, AxisLongerThanGridDropsOutOfRangeIndicesInsteadOfCrashing) {
    // Simulates beam::mri::MriVolumeRas's disclosed RAS-reordering/axis-length
    // mismatch: the axis vectors are longer than the mask volume passed in,
    // so imagePositionToVoxelIndex can return indices >= nx/ny/nz. MATLAB's
    // unchecked `arrayImage(i,j,k)=1` would error here; this port clips
    // instead, so the same inputs must produce a mask with no marked voxel
    // outside [0,nx)x[0,ny)x[0,nz) and no undefined behavior (an out-of-range
    // Eigen access would be UB, not a clean crash, so the assertions below
    // are the only signal available).
    const beam::mri::RasAxisVectors axes = linspaceAxes(-10.0, 10.0, 21);  // 1mm spacing, values -10..10

    beam::array::ArrayElement element;
    element.corners.col(0) = Eigen::Vector3d(0.008, 0.008, 0.000);  // 8mm: index 18 of 21 -- beyond a 5-cell grid
    element.corners.col(1) = Eigen::Vector3d(0.009, 0.008, 0.000);
    element.corners.col(2) = Eigen::Vector3d(0.009, 0.009, 0.000);
    element.corners.col(3) = Eigen::Vector3d(0.008, 0.009, 0.000);

    beam::array::ArrayStruct arrayTotal;
    arrayTotal.element = {element};

    const beam::mri::Volume3D mask = beam::gui::rasterizeArrayOntoMriGrid(arrayTotal, axes, 5, 5, 5);
    ASSERT_EQ(mask.kSlices.size(), 5u);
    for (const Eigen::MatrixXd& slice : mask.kSlices) {
        EXPECT_DOUBLE_EQ(slice.sum(), 0.0);  // every sample's nearest index (~18) is dropped, not clamped in
    }
}

TEST(RasterizeFiducialMarkersOntoMriGrid, PaintsACubeCenteredOnTheNearestVoxel) {
    const beam::mri::RasAxisVectors axes = linspaceAxes(-10.0, 10.0, 21);  // 1mm spacing
    const std::vector<Eigen::Vector3d> positions = {Eigen::Vector3d(0.0, 0.0, 0.0)};

    const beam::mri::Volume3D mask =
        beam::gui::rasterizeFiducialMarkersOntoMriGrid(positions, axes, 21, 21, 21, /*radiusVoxels=*/1);

    // Center voxel (index 10, value 0mm) and its 3x3x3 neighborhood should be marked.
    EXPECT_DOUBLE_EQ(mask(10, 10, 10), 1.0);
    EXPECT_DOUBLE_EQ(mask(9, 10, 10), 1.0);
    EXPECT_DOUBLE_EQ(mask(11, 11, 11), 1.0);
    EXPECT_DOUBLE_EQ(mask(0, 0, 0), 0.0);  // far corner untouched
}

// --- generalSonicateMaster.m orchestration ---

namespace {

beam::safety::SonicationSafetyParams sonicateableParams() {
    beam::safety::SonicationSafetyParams p;
    p.amplitudeMPa = 0.7;
    p.centerFrequencyMHz = 0.300;
    p.pd = 0.005;
    p.pi = 0.01;
    p.bd = 0.03;
    p.bi = 0.7;
    p.startTime = 0.0;
    p.endTime = 30.0;
    return p;
}

}  // namespace

TEST(PrepareSonication, GoodCouplingStartsAndComputesDutyCycle) {
    const beam::gui::SonicateOutcome out =
        beam::gui::prepareSonication(sonicateableParams(), /*transmissionPeak2Peak=*/0.30,
                                      /*couplingThreshold=*/0.10, /*waitForExternalTrigger=*/false);
    EXPECT_TRUE(out.started);
    EXPECT_TRUE(out.messages.empty());
    EXPECT_GE(out.dutyCycle, 0.4);
    EXPECT_LE(out.dutyCycle, 0.75);
    EXPECT_FALSE(out.waitForTrigger);
    EXPECT_DOUBLE_EQ(out.durationSeconds, 30.0);
}

TEST(PrepareSonication, LowTransmissionFailsWithMessageAndNoStart) {
    const beam::gui::SonicateOutcome out =
        beam::gui::prepareSonication(sonicateableParams(), /*transmissionPeak2Peak=*/0.02,
                                      /*couplingThreshold=*/0.10, /*waitForExternalTrigger=*/false);
    EXPECT_FALSE(out.started);
    ASSERT_EQ(out.messages.size(), 1u);
    EXPECT_EQ(out.messages[0], "Transmission below coupling threshold");
}

TEST(PrepareSonication, ExternalTriggerFlagPassesThrough) {
    const beam::gui::SonicateOutcome out =
        beam::gui::prepareSonication(sonicateableParams(), 0.30, 0.10, /*waitForExternalTrigger=*/true);
    EXPECT_TRUE(out.waitForTrigger);
    EXPECT_TRUE(out.started);
}

TEST(PrepareSonication, DutyCycleNeverExceedsNinetyPercentSoThatCheckIsDead) {
    // pressureToDutyCycleGivenTransmission clamps to [0.4, 0.75] for any
    // input, so generalSonicateMaster.m's `DutyCycle > 0.90` check (ported
    // verbatim) can never fire -- confirm across a spread of inputs that
    // "Input pressure too high" never appears.
    for (double amp : {0.1, 0.5, 0.75, 1.0, 2.0}) {
        for (double transmission : {0.0, 0.1, 0.3, 0.5, 1.0}) {
            beam::safety::SonicationSafetyParams p = sonicateableParams();
            p.amplitudeMPa = amp;
            const beam::gui::SonicateOutcome out = beam::gui::prepareSonication(p, transmission, 0.10, false);
            for (const std::string& m : out.messages) {
                EXPECT_NE(m, "Input pressure too high");
            }
        }
    }
}

TEST(StimParamsFromTableRow, CopiesFieldsAndHardcodesCenterFrequency) {
    const beam::safety::SonicationSafetyParams p =
        beam::gui::stimParamsFromTableRow(/*pd=*/0.005, /*pi=*/0.01, /*bd=*/0.03, /*bi=*/0.7,
                                           /*amplitudeMPa=*/0.6, /*startTime=*/0.0, /*endTime=*/20.0);
    EXPECT_DOUBLE_EQ(p.pd, 0.005);
    EXPECT_DOUBLE_EQ(p.pi, 0.01);
    EXPECT_DOUBLE_EQ(p.bd, 0.03);
    EXPECT_DOUBLE_EQ(p.bi, 0.7);
    EXPECT_DOUBLE_EQ(p.amplitudeMPa, 0.6);
    EXPECT_DOUBLE_EQ(p.startTime, 0.0);
    EXPECT_DOUBLE_EQ(p.endTime, 20.0);
    EXPECT_DOUBLE_EQ(p.centerFrequencyMHz, 0.300);  // setStimParamsFromApp.m's hardcoded constant
}

// --- Session save/load (new infrastructure, no .m counterpart) ---

TEST(SessionIo, RoundTripsAllThreeSections) {
    beam::gui::SessionData data;
    beam::registration::FiducialMarker f1;
    f1.name = "LeftY1Z4";
    f1.position = Eigen::Vector3d(0.019, -0.0225, 0.02);
    beam::registration::FiducialMarker f2;
    f2.name = "RightY1Z1";
    f2.position = Eigen::Vector3d(-0.019, -0.0225, -0.02);
    data.fiducials = {f1, f2};

    beam::gui::StimParamRecord s1;
    s1.order = 1;
    s1.show = true;
    s1.x = 10.0;
    s1.amplitude = 0.7;
    s1.endTime = 30.0;
    s1.bd = 0.03;
    s1.bi = 0.7;
    s1.pd = 0.005;
    s1.pi = 0.01;
    data.stimParams = {s1};

    beam::gui::TreatmentProtocolRecord t1;
    t1.number = 1;
    t1.target = "SCC1";
    t1.duration = 30.0;
    t1.amplitude = 0.75;
    t1.parameters = "X:0,Y:0,Z:0";  // contains commas -- exercises the tab-delimited choice
    t1.pain = 1.0;
    t1.mood = -2.0;
    t1.notes = "repeat, weaker response";
    data.treatmentProtocol = {t1};

    const std::string text = beam::gui::serializeSession(data);
    const beam::gui::SessionData back = beam::gui::deserializeSession(text);

    ASSERT_EQ(back.fiducials.size(), 2u);
    EXPECT_EQ(back.fiducials[0].name, "LeftY1Z4");
    EXPECT_TRUE(back.fiducials[1].position.isApprox(f2.position));

    ASSERT_EQ(back.stimParams.size(), 1u);
    EXPECT_EQ(back.stimParams[0].order, 1);
    EXPECT_TRUE(back.stimParams[0].show);
    EXPECT_DOUBLE_EQ(back.stimParams[0].x, 10.0);
    EXPECT_DOUBLE_EQ(back.stimParams[0].pi, 0.01);

    ASSERT_EQ(back.treatmentProtocol.size(), 1u);
    EXPECT_EQ(back.treatmentProtocol[0].target, "SCC1");
    EXPECT_EQ(back.treatmentProtocol[0].parameters, "X:0,Y:0,Z:0");
    EXPECT_EQ(back.treatmentProtocol[0].notes, "repeat, weaker response");
    EXPECT_DOUBLE_EQ(back.treatmentProtocol[0].mood, -2.0);
}

TEST(SessionIo, EmptySectionsRoundTripToEmpty) {
    const beam::gui::SessionData empty;
    const beam::gui::SessionData back = beam::gui::deserializeSession(beam::gui::serializeSession(empty));
    EXPECT_TRUE(back.fiducials.empty());
    EXPECT_TRUE(back.stimParams.empty());
    EXPECT_TRUE(back.treatmentProtocol.empty());
}

TEST(SessionIo, MissingVersionLineThrows) {
    EXPECT_THROW(beam::gui::deserializeSession("not a session file"), std::runtime_error);
}

TEST(SessionIo, WrongColumnCountThrows) {
    const std::string bad = "# Beam session v1\n[Fiducials]\nname\tx\ty\tz\nOnlyTwo\t1.0\t2.0\n";
    EXPECT_THROW(beam::gui::deserializeSession(bad), std::runtime_error);
}

TEST(RasterizeFocusEllipsoidOntoMriGrid, CenterVoxelIsInsideAndFarVoxelIsOutside) {
    const beam::mri::RasAxisVectors axes = linspaceAxes(-40.0, 40.0, 81);  // 1mm spacing
    const beam::mri::Volume3D mask =
        beam::gui::rasterizeFocusEllipsoidOntoMriGrid(Eigen::Vector3d(0.0, 0.0, 0.0), axes, 81, 81, 81);
    ASSERT_EQ(mask.nx, 81);

    // Center of the ellipsoid (0,0,0 -> voxel index 40,40,40) is deep inside.
    EXPECT_GT(mask(40, 40, 40), 0.99);

    // 40mm along Y is far outside the 5mm y-semi-axis.
    EXPECT_LT(mask(40, 80, 40), 0.01);
}

TEST(RasterizeFocusEllipsoidOntoMriGrid, StampIsElongatedAlongXOverYAndZ) {
    // Semi-axes are (30, 5, 5) mm -- along X it should still be "inside"
    // well past where Y/Z would already be "outside".
    const beam::mri::RasAxisVectors axes = linspaceAxes(-40.0, 40.0, 81);  // 1mm spacing
    const beam::mri::Volume3D mask =
        beam::gui::rasterizeFocusEllipsoidOntoMriGrid(Eigen::Vector3d(0.0, 0.0, 0.0), axes, 81, 81, 81);

    EXPECT_GT(mask(60, 40, 40), 0.5);  // +20mm along X: inside (20/30 < 1)
    EXPECT_LT(mask(40, 60, 40), 0.5);  // +20mm along Y: outside (20/5 > 1)
    EXPECT_LT(mask(40, 40, 60), 0.5);  // +20mm along Z: outside (20/5 > 1)
}

TEST(RasterizeFocusEllipsoidOntoMriGrid, CenteredOnTheFocusVoxelNotVolumeOrigin) {
    // Y's semi-axis is only 5mm, so shifting the focus 15mm along Y should
    // clearly relocate which voxels read "inside": if this were (bug)
    // centered on the volume origin instead, these two results would flip.
    const beam::mri::RasAxisVectors axes = linspaceAxes(-40.0, 40.0, 81);  // 1mm spacing
    const beam::mri::Volume3D mask =
        beam::gui::rasterizeFocusEllipsoidOntoMriGrid(Eigen::Vector3d(0.0, 15.0, 0.0), axes, 81, 81, 81);

    EXPECT_GT(mask(40, 55, 40), 0.99);  // y=15mm (the new center): inside
    EXPECT_LT(mask(40, 40, 40), 0.01);  // y=0 (volume origin): now 15mm/5mm semi-axis away, outside
}

// --- Countdown (UIFeatures/startStandaloneCountdown.m + updateFigureTimer.m) ---

TEST(CountdownTimeLeftSeconds, StepsDownByTwoPerTick) {
    EXPECT_EQ(beam::gui::countdownTimeLeftSeconds(30, 1), 28);
    EXPECT_EQ(beam::gui::countdownTimeLeftSeconds(30, 15), 0);
    EXPECT_EQ(beam::gui::countdownTimeLeftSeconds(30, 16), -2);
    EXPECT_EQ(beam::gui::countdownTimeLeftSeconds(30, 17), -4);
}

TEST(IsCountdownDone, StopsOnlyStrictlyBelowNegativeTwo) {
    EXPECT_FALSE(beam::gui::isCountdownDone(-2));
    EXPECT_FALSE(beam::gui::isCountdownDone(0));
    EXPECT_TRUE(beam::gui::isCountdownDone(-3));
}

TEST(CountdownDisplayText, FormatsMinutesAndSeconds) {
    EXPECT_EQ(beam::gui::countdownDisplayText(90), "Time remaining: 1:30");
    EXPECT_EQ(beam::gui::countdownDisplayText(0), "Time remaining: 0:00");
    EXPECT_EQ(beam::gui::countdownDisplayText(5), "Time remaining: 0:05");
}

TEST(CountdownDisplayText, NegativeTimeLeftUsesMatlabFloorModSemantics) {
    // MATLAB: floor(-1/60) = -1, mod(-1,60) = 59 -- reproduced exactly,
    // including the odd-looking "-1:59" this produces right before the
    // countdown stops (see the header comment on countdownDisplayText).
    EXPECT_EQ(beam::gui::countdownDisplayText(-1), "Time remaining: -1:59");
    EXPECT_EQ(beam::gui::countdownDisplayText(-2), "Time remaining: -1:58");
}

// --- initTransducers.m's real computation ---

namespace {

// 100 elements along x, same idea as array_tests'/registration_tests'
// own synthetic rect builders.
Eigen::MatrixXd buildSyntheticRectForCentering(int n) {
    Eigen::MatrixXd rect(19, n);
    const double spacing = 0.002, h = 0.0003;
    for (int i = 0; i < n; ++i) {
        const Eigen::Vector3d c(static_cast<double>(i + 1) * spacing, 0, 0);
        rect(0, i) = i + 1;
        rect.block<3, 1>(1, i) = c + Eigen::Vector3d(-h, -h, 0);
        rect.block<3, 1>(4, i) = c + Eigen::Vector3d(h, -h, 0);
        rect.block<3, 1>(7, i) = c + Eigen::Vector3d(h, h, 0);
        rect.block<3, 1>(10, i) = c + Eigen::Vector3d(-h, h, 0);
        rect.block<3, 1>(13, i).setZero();
        rect.block<3, 1>(16, i) = c;
    }
    return rect;
}

}  // namespace

TEST(CenterArrayOnMri, PlacesArrayAtMriCenterPlusFixedOffset) {
    const beam::array::ArrayData data = beam::array::defineArrayData(buildSyntheticRectForCentering(100));

    beam::mri::RasAxisVectors axes;
    axes.dimLR = Eigen::VectorXd::LinSpaced(21, -50.0, 50.0);   // mean 0 mm
    axes.dimAP = Eigen::VectorXd::LinSpaced(21, 0.0, 100.0);    // mean 50 mm
    axes.dimIS = Eigen::VectorXd::LinSpaced(21, 100.0, 300.0);  // mean 200 mm

    const beam::registration::AffineArrayResult result = beam::gui::centerArrayOnMri(data, axes);

    const Eigen::Vector3d newCentroid = result.arrayData.arrayTotal.rect
                                             .block(beam::array::kRectCenterStartRow, 0, 3,
                                                    result.arrayData.arrayTotal.rect.cols())
                                             .rowwise()
                                             .mean();
    // centerMRI (0, 50, 200)mm + the source's fixed (0, 50, -25)mm offset,
    // both /1000 for meters.
    const Eigen::Vector3d expected = (Eigen::Vector3d(0.0, 50.0, 200.0) + Eigen::Vector3d(0.0, 50.0, -25.0)) / 1000.0;
    EXPECT_TRUE(newCentroid.isApprox(expected, 1e-9));
    EXPECT_EQ(result.fiducialMarkers.size(), 6u);
}

// --- unfocusedSonicate.m's real content ---

TEST(PrepareShamSonication, ComputesDurationAndDelegatesToSetShamAudio) {
    beam::safety::SonicationSafetyParams params;
    params.startTime = 2.0;
    params.endTime = 3.0;  // duration 1.0s
    params.bd = 0.4;
    params.bi = 0.5;

    const std::vector<double> sound = {1, 1, 1, 1};
    const double fs = 10.0;

    const beam::gui::ShamSonicationOutcome outcome = beam::gui::prepareShamSonication(params, sound, fs);

    EXPECT_DOUBLE_EQ(outcome.durationSeconds, 1.0);
    // Matches setShamAudio's own signature/backgroundNoise=true directly --
    // proves the wiring (duration/bd/bi/backgroundNoise), not setShamAudio
    // itself (already covered by SetShamAudio.*).
    const std::vector<double> expected =
        beam::sham::setShamAudio(sound, fs, 1.0, params.bd, params.bi, /*backgroundNoise=*/true);
    EXPECT_EQ(outcome.maskingAudio, expected);
}

// --- Multi-visit treatment-session store ---

namespace {

std::vector<beam::gui::TreatmentProtocolRecord> oneBlankRow() {
    beam::gui::TreatmentProtocolRecord r;
    r.number = 1;
    r.target = "SCC1";
    r.duration = 30.0;
    return {r};
}

}  // namespace

TEST(InitTreatmentProtocols, OneSessionPerProtocolWithBlankRows) {
    const std::vector<std::string> names = {"PainSCCandAMCC", "PainAMCCandSCC", "PainACC"};
    std::vector<beam::gui::TreatmentProtocol> protocols = beam::gui::initTreatmentProtocols(names, oneBlankRow());

    ASSERT_EQ(protocols.size(), 3u);
    for (const beam::gui::TreatmentProtocol& p : protocols) {
        ASSERT_EQ(p.sessions.size(), 1u);
        EXPECT_EQ(p.sessions[0].visitNumber, 1);
        ASSERT_EQ(p.sessions[0].rows.size(), 1u);
        EXPECT_EQ(p.sessions[0].rows[0].target, "SCC1");
    }
    EXPECT_EQ(protocols[0].name, "PainSCCandAMCC");
    EXPECT_EQ(protocols[2].name, "PainACC");
}

TEST(AddTreatmentProtocolSession, AppendsNextVisitNumberUntilCapThenNoOps) {
    beam::gui::TreatmentProtocol protocol;
    protocol.name = "PainACC";
    protocol.sessions.push_back({1, oneBlankRow()});

    beam::gui::addTreatmentProtocolSession(protocol, oneBlankRow(), /*maxSessions=*/2);
    ASSERT_EQ(protocol.sessions.size(), 2u);
    EXPECT_EQ(protocol.sessions[1].visitNumber, 2);

    // source compares the CURRENT count against the cap with <=, so it still
    // adds once more here (2 <= 2) before refusing -- a disclosed quirk.
    beam::gui::addTreatmentProtocolSession(protocol, oneBlankRow(), /*maxSessions=*/2);
    ASSERT_EQ(protocol.sessions.size(), 3u);
    EXPECT_EQ(protocol.sessions[2].visitNumber, 3);

    beam::gui::addTreatmentProtocolSession(protocol, oneBlankRow(), /*maxSessions=*/2);
    EXPECT_EQ(protocol.sessions.size(), 3u);  // 3 > 2 now -- no-op
}

TEST(TreatmentSessionStore, CurrentAndSetRowsRoundTripThroughTheCorrectSlot) {
    std::vector<beam::gui::TreatmentProtocol> protocols =
        beam::gui::initTreatmentProtocols({"PainSCCandAMCC", "PainACC"}, oneBlankRow());
    beam::gui::addTreatmentProtocolSession(beam::gui::findProtocol(protocols, "PainACC"), oneBlankRow());

    std::vector<beam::gui::TreatmentProtocolRecord> edited = oneBlankRow();
    edited[0].target = "aMCC1";
    beam::gui::setCurrentSessionRows(protocols, "PainACC", 2, edited);

    EXPECT_EQ(beam::gui::currentSessionRows(protocols, "PainACC", 2)[0].target, "aMCC1");
    // visit 1 of the same protocol, and the other protocol entirely,
    // untouched by the write into (PainACC, visit 2).
    EXPECT_EQ(beam::gui::currentSessionRows(protocols, "PainACC", 1)[0].target, "SCC1");
    EXPECT_EQ(beam::gui::currentSessionRows(protocols, "PainSCCandAMCC", 1)[0].target, "SCC1");
}

TEST(TreatmentSessionStore, UnknownProtocolOrVisitThrows) {
    std::vector<beam::gui::TreatmentProtocol> protocols = beam::gui::initTreatmentProtocols({"PainACC"}, oneBlankRow());
    EXPECT_THROW(beam::gui::currentSessionRows(protocols, "NoSuchProtocol", 1), std::out_of_range);
    EXPECT_THROW(beam::gui::currentSessionRows(protocols, "PainACC", 99), std::out_of_range);
}

TEST(TreatmentWorkflow, StartsAtCaseSetupAndBlocksSkippingAhead) {
    beam::gui::TreatmentWorkflow workflow;
    EXPECT_EQ(workflow.state(beam::gui::WorkflowStage::CaseSetup).status,
              beam::gui::WorkflowStatus::InProgress);
    EXPECT_EQ(workflow.nextStage(), beam::gui::WorkflowStage::CaseSetup);

    std::string reason;
    EXPECT_FALSE(workflow.begin(beam::gui::WorkflowStage::Imaging, &reason));
    EXPECT_EQ(reason, "Case setup must be complete first.");
    EXPECT_EQ(workflow.state(beam::gui::WorkflowStage::Imaging).status,
              beam::gui::WorkflowStatus::NotStarted);
}

TEST(TreatmentWorkflow, AdvancesOnlyInPrerequisiteOrder) {
    beam::gui::TreatmentWorkflow workflow;
    for (std::size_t i = 0;
         i <= static_cast<std::size_t>(beam::gui::WorkflowStage::SafetyReview); ++i) {
        const auto stage = static_cast<beam::gui::WorkflowStage>(i);
        EXPECT_TRUE(workflow.complete(stage));
    }

    EXPECT_TRUE(workflow.canStartTreatment());
    EXPECT_EQ(workflow.nextStage(), beam::gui::WorkflowStage::Treatment);
    EXPECT_TRUE(workflow.complete(beam::gui::WorkflowStage::Treatment));
    EXPECT_TRUE(workflow.complete(beam::gui::WorkflowStage::Report));
    EXPECT_EQ(workflow.state(beam::gui::WorkflowStage::Report).status,
              beam::gui::WorkflowStatus::Complete);
}

TEST(TreatmentWorkflow, ChangedImagingInvalidatesEveryStartedDownstreamStage) {
    beam::gui::TreatmentWorkflow workflow;
    for (std::size_t i = 0;
         i <= static_cast<std::size_t>(beam::gui::WorkflowStage::SafetyReview); ++i) {
        ASSERT_TRUE(workflow.complete(static_cast<beam::gui::WorkflowStage>(i)));
    }

    workflow.change(beam::gui::WorkflowStage::Imaging, "MRI changed; review required.");

    EXPECT_EQ(workflow.state(beam::gui::WorkflowStage::CaseSetup).status,
              beam::gui::WorkflowStatus::Complete);
    EXPECT_EQ(workflow.state(beam::gui::WorkflowStage::SystemCheck).status,
              beam::gui::WorkflowStatus::Complete);
    EXPECT_EQ(workflow.state(beam::gui::WorkflowStage::Imaging).status,
              beam::gui::WorkflowStatus::InProgress);
    EXPECT_EQ(workflow.state(beam::gui::WorkflowStage::Registration).status,
              beam::gui::WorkflowStatus::Invalidated);
    EXPECT_EQ(workflow.state(beam::gui::WorkflowStage::SafetyReview).message,
              "MRI changed; review required.");

    std::string reason;
    EXPECT_FALSE(workflow.canStartTreatment(&reason));
    EXPECT_EQ(reason, "Final safety review must be complete before treatment.");
}

TEST(TreatmentWorkflow, BlockedStageRetainsOperatorFacingReason) {
    beam::gui::TreatmentWorkflow workflow;
    ASSERT_TRUE(workflow.complete(beam::gui::WorkflowStage::CaseSetup));
    workflow.block(beam::gui::WorkflowStage::SystemCheck, "Device did not respond on COM4.");

    EXPECT_EQ(workflow.state(beam::gui::WorkflowStage::SystemCheck).status,
              beam::gui::WorkflowStatus::Blocked);
    EXPECT_EQ(workflow.state(beam::gui::WorkflowStage::SystemCheck).message,
              "Device did not respond on COM4.");
    EXPECT_FALSE(workflow.begin(beam::gui::WorkflowStage::Imaging));
}
