#include <limits>
#include <optional>
#include <stdexcept>

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <vector>

#include "array/array_types.hpp"
#include "correction/attenuation.hpp"
#include "correction/correlation.hpp"
#include "correction/localize.hpp"
#include "correction/peaks.hpp"
#include "correction/receive_elements.hpp"
#include "correction/receive_waveform.hpp"
#include "correction/scan_params.hpp"
#include "correction/signal.hpp"
#include "correction/waveforms.hpp"

using namespace beam::correction;

TEST(Xcorr, AutocorrelationOfShortSignal) {
    Eigen::VectorXd x(3);
    x << 1.0, 2.0, 3.0;
    const XcorrResult r = xcorr(x, x);
    ASSERT_EQ(r.c.size(), 5);
    EXPECT_EQ(r.lags(0), -2);
    EXPECT_EQ(r.lags(4), 2);
    Eigen::VectorXd expected(5);
    expected << 3.0, 8.0, 14.0, 8.0, 3.0;
    EXPECT_TRUE(r.c.isApprox(expected));
}

TEST(CorrCoefficient, IdenticalAndOpposite) {
    Eigen::VectorXd a(4);
    a << 1.0, 2.0, 3.0, 4.0;
    EXPECT_NEAR(corrCoefficient(a, a), 1.0, 1e-12);
    EXPECT_NEAR(corrCoefficient(a, (-a).eval()), -1.0, 1e-12);
}

TEST(Nanmean, IgnoresNaN) {
    Eigen::VectorXd v(4);
    v << 2.0, std::numeric_limits<double>::quiet_NaN(), 4.0, 6.0;
    EXPECT_DOUBLE_EQ(nanmean(v), 4.0);
}

TEST(ShiftAndSumWaveforms, ShiftsRowsAndSumsColumns) {
    Eigen::MatrixXd wv(2, 3);
    wv << 1.0, 2.0, 3.0,
          4.0, 5.0, 6.0;
    Eigen::VectorXi delays(2);
    delays << 0, 1;  // row 1 circularly shifted right by 1 -> [6,4,5]

    const ShiftAndSumResult r = shiftAndSumWaveforms(wv, delays);
    Eigen::VectorXd expectedSum(3);
    expectedSum << 7.0, 6.0, 8.0;
    EXPECT_TRUE(r.waveformSum.isApprox(expectedSum));
    EXPECT_TRUE(r.wvShifted.row(1).transpose().isApprox(Eigen::Vector3d(6.0, 4.0, 5.0)));
}

TEST(XcorrS1ToS2, RecoversKnownShift) {
    Eigen::VectorXd s1(10);
    s1 << 0, 0, 1, 2, 1, 0, 0, 0, 0, 0;
    Eigen::VectorXd s2(10);
    s2 << 0, 0, 0, 0, 0, 1, 2, 1, 0, 0;  // s1 shifted right by 3

    const XcorrS1ToS2Result r = xcorrS1ToS2(s1, s2, Eigen::Vector2d(-8, 8));
    EXPECT_EQ(r.d12, 3);
    EXPECT_GT(r.cc, 0.9);
}

TEST(CorrSpeedUp, SelfCorrelationPeaksAtLagMinusOne) {
    // corrSpeedUp keeps only lags in [-150, -1] (lag 0 and >=0 are zeroed).
    // For a smooth symmetric bump the autocorrelation is largest at lag -1,
    // giving speedupsa = n - mi - 1 = 1.
    const int n = 60;
    Eigen::VectorXd bump(n);
    for (int i = 0; i < n; ++i) {
        const double d = i - 30.0;
        bump(i) = std::exp(-(d * d) / (2.0 * 5.0 * 5.0));
    }
    const CorrSpeedUpResult r = corrSpeedUp(bump, bump);
    EXPECT_EQ(r.speedupsa, 1);
    EXPECT_GT(r.cc, 0.9);
}

TEST(ComputeMaxCorrelationDelays, AutoPicksStrongestChannelAndSignedDelays) {
    Eigen::VectorXd base(8);
    base << 0, 0, 1, 2, 1, 0, 0, 0;
    auto circ = [](const Eigen::VectorXd& v, int s) {
        const Eigen::Index n = v.size();
        Eigen::VectorXd o(n);
        for (Eigen::Index i = 0; i < n; ++i) o(i) = v(((i - s) % n + n) % n);
        return o;
    };
    Eigen::MatrixXd wv(3, 8);
    wv.row(0) = circ(base, 2).transpose();
    wv.row(1) = (3.0 * base).transpose();  // strongest -> maxChannel 2 (1-based)
    wv.row(2) = circ(base, -1).transpose();

    const MaxCorrelationDelaysResult r = computeMaxCorrelationDelays(wv);
    EXPECT_EQ(r.maxChannel, 2);
    EXPECT_EQ(r.delaysI(1), 0);   // reference row untouched
    EXPECT_EQ(r.delaysI(0), -2);
    EXPECT_EQ(r.delaysI(2), 1);
}

TEST(DefineTxRxScanParams, LiteralConstants) {
    const TxRxScanParams p = defineTxRxScanParams();
    EXPECT_DOUBLE_EQ(p.desiredDepth, 135.0);
    EXPECT_DOUBLE_EQ(p.sampleRateHz, 29.25e6);
    EXPECT_DOUBLE_EQ(p.voltageAmplitude, 12.0);
    ASSERT_EQ(p.arrayToVSXMapping.size(), 252u);
    EXPECT_EQ(p.arrayToVSXMapping.front(), 1);
    EXPECT_EQ(p.arrayToVSXMapping[125], 126);
    EXPECT_EQ(p.arrayToVSXMapping[126], 129);
    EXPECT_EQ(p.arrayToVSXMapping.back(), 254);
}

TEST(SetAdjustedAttValues, ClampsBelowThresholdAndCapsHigh) {
    Eigen::VectorXd att(3);
    att << 10.0, 20.0, 0.5;
    const Eigen::VectorXd out = setAdjustedAttValues(att, /*vAmplitudeToMPa=*/60.0, /*attThreshold=*/1.0);
    Eigen::VectorXd expected(3);
    expected << 2.0, 2.0, 1.0;  // maxAdjustAtt = 60/30 = 2; 0.5 -> threshold 1
    EXPECT_TRUE(out.isApprox(expected));
}

TEST(SetAdjustedAttValues, LeavesInRangeValuesUntouched) {
    Eigen::VectorXd att(3);
    att << 1.5, 1.8, 2.0;
    const Eigen::VectorXd out = setAdjustedAttValues(att, 60.0, 1.0);  // maxAdjustAtt = 2, nothing above
    EXPECT_TRUE(out.isApprox(att));
}

namespace {

// Minimal ArrayData: two genuinely distinct sub-arrays (numbers {1,2} and
// {3,4,5}) so getOpposingElements returns a clean opposing set, plus an
// arrayTotal whose elements carry the positions/normals the angle test needs.
beam::array::ArrayData makeTwoArrayData() {
    using beam::array::ArrayData;
    using beam::array::ArrayElement;
    using beam::array::ArrayStruct;

    auto makeStruct = [](std::vector<int> numbers) {
        ArrayStruct s;
        s.rect = Eigen::MatrixXd::Zero(19, static_cast<Eigen::Index>(numbers.size()));
        for (size_t i = 0; i < numbers.size(); ++i) {
            s.rect(0, static_cast<Eigen::Index>(i)) = numbers[i];
            ArrayElement e;
            e.number = numbers[i];
            s.element.push_back(e);
        }
        return s;
    };

    ArrayData d;
    d.array[0] = makeStruct({1, 2});
    d.array[1] = makeStruct({3, 4, 5});

    d.arrayTotal = makeStruct({1, 2, 3, 4, 5});
    d.arrayTotal.element[0].position = Eigen::Vector3d(0, 0, 0);
    d.arrayTotal.element[0].normalVector = Eigen::Vector3d(0, 0, 1);
    d.arrayTotal.element[2].position = Eigen::Vector3d(0, 0, 1);   // along +z  -> angle 0
    d.arrayTotal.element[3].position = Eigen::Vector3d(1, 0, 0);   // sideways  -> angle 90
    d.arrayTotal.element[4].position = Eigen::Vector3d(0, 0, -1);  // along -z  -> min(+/-) angle 0
    return d;
}

}  // namespace

TEST(GetReceiveElementsUnderAngle, NormModeSelectsElementsWithinThreshold) {
    const beam::array::ArrayData d = makeTwoArrayData();
    const std::vector<int> got =
        getReceiveElementsUnderAngle(d, /*eli1Based=*/1, /*targetPosMm=*/std::nullopt, /*angleThreshold=*/30.0);
    EXPECT_EQ(got, (std::vector<int>{3, 5}));
}

TEST(FindPeaks, StrictLocalMaxima) {
    Eigen::VectorXd v(7);
    v << 0, 1, 0, 2, 1, 3, 0;
    const std::vector<Eigen::Index> idx = findPeaks(v);
    EXPECT_EQ(idx, (std::vector<Eigen::Index>{1, 3, 5}));
}

TEST(FindPeakNegativeVoltage, LowNCyclesReturnsNegMin) {
    Eigen::VectorXd v(3);
    v << 1.0, -3.0, 2.0;
    EXPECT_DOUBLE_EQ(findPeakNegativeVoltage(v, 1), 3.0);
}

TEST(FindPeakNegativeVoltage, MedianOfLargestPeaks) {
    Eigen::VectorXd v(9);
    v << 0, 5, 0, -5, 0, 3, 0, -3, 0;  // mean 0; abs peaks {5,5,3,3}
    EXPECT_DOUBLE_EQ(findPeakNegativeVoltage(v, 2), 5.0);
    EXPECT_DOUBLE_EQ(findPeakNegativeVoltage(v, 4), 4.0);   // median(5,5,3,3)
    EXPECT_DOUBLE_EQ(findPeakNegativeVoltage(v, 10), 0.0);  // fewer than 10 peaks -> -min(abs)
}

TEST(CalculateAttenuation, PeakToPeakRatio) {
    Eigen::VectorXd subject(4);
    subject << 1, 2, 3, 10;  // mean 4 -> [-3,-2,-1,6], p2p 9
    Eigen::VectorXd freeField(4);
    freeField << 0, 0, 0, 5;  // mean 1.25 -> p2p 5
    EXPECT_DOUBLE_EQ(calculateAttenuation(subject, freeField), 1.8);
}

TEST(Localize, DistanceIsEuclidean) {
    EXPECT_DOUBLE_EQ(distance(Eigen::Vector3d(0, 0, 0), Eigen::Vector3d(3, 4, 0)), 5.0);
}

TEST(Localize, UpdateArrayPositionsShiftsCornersAndCenter) {
    beam::array::ArrayStruct a;
    a.rect = Eigen::MatrixXd::Zero(19, 1);
    a.rect.block<3, 1>(1, 0) = Eigen::Vector3d(-0.5, -0.5, -0.5);  // corner 1
    a.rect.block<3, 1>(16, 0) = Eigen::Vector3d(0, 0, 0);          // center
    a.element.resize(1);

    Eigen::MatrixX3d rs(1, 3);
    rs << 1, 2, 3;
    const beam::array::ArrayStruct out = updateArrayPositions(a, rs);

    const Eigen::Vector3d corner1 = out.rect.block<3, 1>(1, 0);
    const Eigen::Vector3d center = out.rect.block<3, 1>(16, 0);
    EXPECT_TRUE(corner1.isApprox(Eigen::Vector3d(0.5, 1.5, 2.5)));
    EXPECT_TRUE(center.isApprox(Eigen::Vector3d(1, 2, 3)));
    EXPECT_TRUE(out.element[0].position.isApprox(Eigen::Vector3d(1, 2, 3)));
}

TEST(Localize, NonlinRelativeDistanceFunResidual) {
    // 3 elements: e1=(0,0,0), e2=(6,0,0), e3=(3,4,0); c=2, fs=1000.
    Eigen::VectorXd x(10);
    x << 2, /*x*/ 0, 6, 3, /*y*/ 0, 0, 4, /*z*/ 0, 0, 0;
    Eigen::MatrixXi mData(2, 3);
    mData << 1, 2, 3,
             2, 3, 1;
    Eigen::VectorXd dData(2);
    dData << 0.001, 0.0;

    const Eigen::VectorXd f = nonlinRelativeDistanceFun(mData, dData, /*nElements=*/3, /*fs=*/1000.0, x);
    // row 0: (dist(e2,e3)-dist(e1,e3))/2 = (5-5)/2 = 0 -> |0-0.001|*1000 = 1
    // row 1: (dist(e3,e1)-dist(e2,e1))/2 = (5-6)/2 = -0.5 -> |-0.5|*1000 = 500
    Eigen::VectorXd expected(2);
    expected << 1.0, 500.0;
    EXPECT_TRUE(f.isApprox(expected));
}

TEST(AnalyticEnvelope, ConstantForPureSinusoid) {
    const int n = 256;
    Eigen::VectorXd x(n);
    for (int i = 0; i < n; ++i) x(i) = 3.0 * std::sin(2.0 * std::numbers::pi * 8.0 * i / n);
    const Eigen::VectorXd env = analyticEnvelope(x);
    ASSERT_EQ(env.size(), n);
    // Away from the FFT-wrap edges the envelope is ~ the amplitude.
    for (int i = 40; i < n - 40; ++i) EXPECT_NEAR(env(i), 3.0, 1e-6);
}

TEST(FilterTransmitSignal, NoBurstLeavesWaveformUnblanked) {
    const Eigen::VectorXd wv = Eigen::VectorXd::Zero(500);
    const FilterTransmitResult r = filterTransmitSignal(wv);
    EXPECT_TRUE(r.filtWave.isApprox(wv));
    EXPECT_EQ(r.startSample, 1 + 175);  // default startSample 1, advanced by 175
}

TEST(FilterTransmitSignal, BlanksAroundDetectedBurst) {
    Eigen::VectorXd wv = Eigen::VectorXd::Zero(600);
    for (int i = 250; i < 320; ++i) wv(i) = 20.0 * std::sin(2.0 * std::numbers::pi * i / 8.0);
    const FilterTransmitResult r = filterTransmitSignal(wv);

    EXPECT_GT(r.startSample, 175);
    // A 351-sample window centred on the detected start is zeroed.
    const int start1 = r.startSample - 175;  // == detected startSample
    const int a = start1 - 175 - 1;          // 0-based
    ASSERT_GE(a, 0);
    EXPECT_TRUE(r.filtWave.segment(a, 351).isZero());
}

namespace {
void addBurst(Eigen::VectorXd& v, double center, double width, double amplitude) {
    for (Eigen::Index i = 0; i < v.size(); ++i) {
        const double dt = static_cast<double>(i) - center;
        v(i) += amplitude * std::exp(-(dt * dt) / (2.0 * width * width)) *
                std::sin(2.0 * std::numbers::pi * static_cast<double>(i) / 6.0);
    }
}

// A single Gaussian-modulated sinusoidal burst -- long enough for
// txRxSignalAmplitude's x(20:end-200) crop.
Eigen::VectorXd burstRf(double amplitude, int n = 1500, double center = 700.0, double width = 45.0) {
    Eigen::VectorXd v = Eigen::VectorXd::Zero(n);
    addBurst(v, center, width, amplitude);
    return v;
}

// A transmit feedthrough pulse (which filterTransmitSignal blanks) followed
// by a weaker through-transmit echo -- the shape getTransmissionAfterThroughTransmit
// actually sees.
Eigen::VectorXd txPlusEcho(double echoAmplitude, int n = 1600) {
    Eigen::VectorXd v = Eigen::VectorXd::Zero(n);
    addBurst(v, 260.0, 12.0, 60.0);            // transmit feedthrough
    addBurst(v, 780.0, 45.0, echoAmplitude);   // through-transmit echo
    return v;
}
}  // namespace

TEST(TxRxSignalAmplitude, RecoversBurstAmplitudeAndEnvelope) {
    const Eigen::VectorXd x = burstRf(30.0);
    const TxRxSignalAmplitude a = txRxSignalAmplitude(x);
    // Envelope peak is the burst amplitude (minus tiny FFT-edge error).
    EXPECT_NEAR(a.envelope, 30.0, 0.5);
    // peak2peak is the median height of the detected abs-signal peaks --
    // below the crest (the burst tapers) but a solid fraction of it.
    EXPECT_GT(a.peak2peak, 3.0);
    EXPECT_LT(a.peak2peak, 30.0);
    EXPECT_GT(a.rms, 0.0);
}

TEST(TxRxSignalAmplitude, ThrowsOnTooShortSignal) {
    EXPECT_THROW(txRxSignalAmplitude(Eigen::VectorXd::Zero(200)), std::invalid_argument);
}

TEST(ThroughTransmitAmplitude, StrongerCouplingGivesHigherAmp) {
    // filterTransmitSignal blanks the transmit feedthrough; the echo is
    // what's measured.
    const beam::correction::ThroughTransmitAmplitude weak =
        throughTransmitAmplitude(txPlusEcho(6.0), txPlusEcho(5.0));
    const beam::correction::ThroughTransmitAmplitude strong =
        throughTransmitAmplitude(txPlusEcho(45.0), txPlusEcho(38.0));
    EXPECT_GT(strong.amp, weak.amp);
    EXPECT_GT(weak.amp, 0.0);
    // amp = mean(sqrt(echoEnv / 1200)); echoEnv ~ 40 -> ~ sqrt(0.033) ~ 0.18.
    EXPECT_NEAR(strong.amp, 0.18, 0.06);
}

TEST(LocalizeArrays, ReducesObjectiveWithinBounds) {
    const int n = 12;
    const double cTrue = 1490.0;
    const double fs = 1e6;

    Eigen::MatrixX3d pTrue(n, 3);
    for (int i = 0; i < n; ++i) {
        pTrue.row(i) = Eigen::RowVector3d(0.010 * i, 0.005 * (i % 3), 0.003 * (i % 4));
    }

    // Exact relative-distance-difference data for (i,j,k) triples.
    std::vector<Eigen::RowVector3i> triples;
    std::vector<double> d;
    for (int i = 1; i <= n; ++i) {
        for (int j = i + 1; j <= n; ++j) {
            for (int k = 1; k <= n; ++k) {
                if (k == i || k == j) continue;
                const double di = (pTrue.row(i - 1) - pTrue.row(k - 1)).norm();
                const double dj = (pTrue.row(j - 1) - pTrue.row(k - 1)).norm();
                triples.emplace_back(i, j, k);
                d.push_back((dj - di) / cTrue);
            }
        }
    }
    LocalizeSystem sys;
    sys.mData.resize(static_cast<Eigen::Index>(triples.size()), 3);
    sys.dData.resize(static_cast<Eigen::Index>(d.size()));
    for (std::size_t r = 0; r < triples.size(); ++r) {
        sys.mData.row(static_cast<Eigen::Index>(r)) = triples[r];
        sys.dData(static_cast<Eigen::Index>(r)) = d[r];
    }

    Eigen::MatrixX3d init = pTrue;
    for (int e = 0; e < n; ++e) {
        const int ob = e + 1;
        if (ob % 9 == 1 || ob % 9 == 0) continue;  // pinned elements
        init.row(e).array() += 0.0015;
    }

    const auto cost = [&](const Eigen::MatrixX3d& pos, double c) {
        Eigen::VectorXd x(1 + 3 * n);
        x(0) = c;
        for (int col = 0; col < 3; ++col)
            for (int r = 0; r < n; ++r) x(1 + col * n + r) = pos(r, col);
        return nonlinRelativeDistanceFun(sys.mData, sys.dData, n, fs, x).squaredNorm();
    };

    const double c0 = 1485.0;
    const LocalizeResult res = localizeArrays(sys, init, c0, fs, /*maxIterations=*/25);

    // The bounded LM reduces the least-squares objective and keeps every
    // free coordinate inside its +/-2.3 mm box and c inside +/-20.
    EXPECT_LT(cost(res.positions, res.c), 0.25 * cost(init, c0));
    EXPECT_LE(std::abs(res.c - c0), 20.0 + 1e-9);
    for (int e = 0; e < n; ++e) {
        const int ob = e + 1;
        for (int col = 0; col < 3; ++col) {
            if (ob % 9 == 1 || ob % 9 == 0) {
                EXPECT_NEAR(res.positions(e, col), pTrue(e, col), 1e-12);  // pinned
            } else {
                EXPECT_LE(std::abs(res.positions(e, col) - init(e, col)), 0.0023 + 1e-9);
            }
        }
    }
}

TEST(GetLocalizeArraysSystemOfEquations, IdenticalWaveformsGiveZeroDelaySystem) {
    beam::array::ArrayData data;
    data.arrayTotal.element.resize(4);
    for (auto& el : data.arrayTotal.element) el.receiveElements = {1, 2, 3, 4};

    // Same 20x4 waveform matrix for all 4 transmit elements -> every pair
    // cross-correlates to lag 0.
    Eigen::MatrixXd wv = Eigen::MatrixXd::Zero(20, 4);
    for (int c = 0; c < 4; ++c) wv(c + 3, c) = 1.0;  // a delta per column
    const std::vector<Eigen::MatrixXd> wvData(4, wv);

    const LocalizeSystem sys =
        getLocalizeArraysSystemOfEquations(wvData, data, /*f=*/650000.0, /*fs=*/1e6);

    // Pairs (1,2) and (3,4), each over k = 1..4 -> 8 equations, all d12 = 0.
    ASSERT_EQ(sys.mData.rows(), 8);
    EXPECT_TRUE(sys.dData.isZero());
    EXPECT_EQ(sys.mData.row(0).transpose(), Eigen::Vector3i(1, 2, 1));
    EXPECT_EQ(sys.mData.row(4).transpose(), Eigen::Vector3i(3, 4, 1));
}

// --- getRecieveWaveformFromSerial.m: butter/filter + split/trim ---

TEST(ButterBandpass, MatchesMatlabReferenceCoefficients) {
    // Reference from a real MATLAB run: butter(2, [200000 400000]/(1316800/2)).
    constexpr double fs = 1316800.0;
    const IirCoefficients c = butterBandpass(2, 200000.0 / (fs / 2.0), 400000.0 / (fs / 2.0));
    ASSERT_EQ(c.b.size(), 5);
    ASSERT_EQ(c.a.size(), 5);
    Eigen::VectorXd bExpected(5);
    bExpected << 0.1337493090291613, 0.0, -0.26749861805832259, 0.0, 0.1337493090291613;
    Eigen::VectorXd aExpected(5);
    aExpected << 1.0, -0.42732599922470793, 0.78220583413198863, -0.19852912061148564, 0.26827944211633797;
    EXPECT_TRUE(c.b.isApprox(bExpected, 1e-9));
    EXPECT_TRUE(c.a.isApprox(aExpected, 1e-9));
}

TEST(FilterIir, PassThroughWithUnityCoefficients) {
    Eigen::VectorXd b(1);
    b << 1.0;
    Eigen::VectorXd a(1);
    a << 1.0;
    Eigen::VectorXd x(4);
    x << 3.0, -1.0, 2.5, 0.0;
    EXPECT_TRUE(filterIir(b, a, x).isApprox(x));
}

TEST(FilterIir, FirstOrderStepResponseConvergesToDcGain) {
    Eigen::VectorXd b(1);
    b << 1.0;
    Eigen::VectorXd a(2);
    a << 1.0, -0.5;
    const Eigen::VectorXd x = Eigen::VectorXd::Ones(200);
    const Eigen::VectorXd y = filterIir(b, a, x);
    EXPECT_NEAR(y(y.size() - 1), 1.0 / 0.5, 1e-6);  // steady-state gain = sum(b)/sum(a)
}

TEST(SplitAndFilterReceiveWaveform, ThrowsWhenFewerThanTwoSamplesPerChannel) {
    EXPECT_THROW(splitAndFilterReceiveWaveform({1.0, 2.0}), std::invalid_argument);  // N = 1
}

TEST(SplitAndFilterReceiveWaveform, SplitsFiltersAndTrimsFirstNinetyNineSamples) {
    std::vector<double> data(500);
    for (std::size_t i = 0; i < data.size(); ++i) data[i] = std::sin(2.0 * std::numbers::pi * i / 20.0);

    const ReceiveWaveformSplit split = splitAndFilterReceiveWaveform(data);
    const Eigen::Index n = static_cast<Eigen::Index>(data.size()) / 2;
    const Eigen::Index ch1RawLen = static_cast<Eigen::Index>(data.size()) - n + 1;  // includes the overlap
    EXPECT_EQ(split.ch0rcv.size(), n - 99);
    EXPECT_EQ(split.ch1rcv.size(), ch1RawLen - 99);
}

TEST(ParseCorrectionWaveformLine, ParsesCommaSeparatedFloatsAndDropsNonNumeric) {
    const std::vector<double> vals = parseCorrectionWaveformLine(" 1.5, abc, -2.25 , 3");
    ASSERT_EQ(vals.size(), 3u);
    EXPECT_DOUBLE_EQ(vals[0], 1.5);
    EXPECT_DOUBLE_EQ(vals[1], -2.25);
    EXPECT_DOUBLE_EQ(vals[2], 3.0);
}

TEST(ParseCorrectionWaveformLine, RejectsFieldsWithTrailingGarbage) {
    // MATLAB's str2double('3.4x') is NaN (the whole field fails), not 3.4.
    const std::vector<double> vals = parseCorrectionWaveformLine("3.4x,5.0");
    ASSERT_EQ(vals.size(), 1u);
    EXPECT_DOUBLE_EQ(vals[0], 5.0);
}
