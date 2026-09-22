// parity_stimulation -- runs the Beam Stimulation ports on a synthetic
// array + parameters, writes inputs + results for
// matlab_verify/verify_stimulation.m. See matlab_verify/README.md.

#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>

#include <Eigen/Core>

#include "array/array_struct.hpp"
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

constexpr int kNel = 60;

Eigen::MatrixXd makeRect() {
    constexpr double spacing = 0.002, h = 0.0003;
    Eigen::MatrixXd rect = Eigen::MatrixXd::Zero(19, kNel);
    for (int i = 0; i < kNel; ++i) {
        const Eigen::Vector3d c(static_cast<double>(i + 1) * spacing, 0.0, 0.0);
        rect(0, i) = i + 1;
        rect.block<3, 1>(1, i) = c + Eigen::Vector3d(-h, -h, 0);
        rect.block<3, 1>(4, i) = c + Eigen::Vector3d(h, -h, 0);
        rect.block<3, 1>(7, i) = c + Eigen::Vector3d(h, h, 0);
        rect.block<3, 1>(10, i) = c + Eigen::Vector3d(-h, h, 0);
        rect.block<3, 1>(16, i) = c;
    }
    return rect;
}

void writeMatrixCsv(const Eigen::MatrixXd& m, const std::string& path) {
    std::ofstream out(path);
    out << std::setprecision(17);
    for (Eigen::Index r = 0; r < m.rows(); ++r)
        for (Eigen::Index c = 0; c < m.cols(); ++c) out << m(r, c) << (c + 1 < m.cols() ? "," : "\n");
}

struct Results {
    std::ofstream out;
    explicit Results(const std::string& p) : out(p) { out << std::setprecision(17); }
    void v(const std::string& k, double x) { out << k << "," << x << "\n"; }
    void i(const std::string& k, long x) { out << k << "," << x << "\n"; }
};

int run() {
    const Eigen::MatrixXd rect = makeRect();
    writeMatrixCsv(rect, "parity_stim_rect.csv");
    Results r("parity_stimulation_cpp.csv");

    const beam::array::ArrayStruct array = beam::array::defineArrayStruct(rect, 150000.0, {0.06, 0.06});

    Eigen::VectorXd freqsMHz(kNel);
    for (int e = 0; e < kNel; ++e) freqsMHz(e) = 0.6 + 0.001 * e;

    // --- focusArrayAtPoint ---
    const FocusResult f = focusArrayAtPoint(array, Eigen::Vector3d(0.06, 0.01, 0.15), 1500.0);
    r.v("focus_delaysT_0", f.delaysT(0));
    r.v("focus_delaysT_last", f.delaysT(kNel - 1));
    r.v("focus_delaysTRaw_0", f.delaysTRaw(0));
    r.i("focus_mi", f.mi);

    // --- calculateMultifrequencySuperpositionDelays ---
    const Eigen::VectorXd wd = calculateMultifrequencySuperpositionDelays((freqsMHz.array() * 1e6).matrix(), 0.0);
    r.v("mfsdelays_0", wd(0));
    r.v("mfsdelays_last", wd(kNel - 1));

    // --- getApodFromAtt ---
    Eigen::VectorXd att(6);
    att << 1.0, 2.0, 0.5, 3.0, 1.2, 5.0;
    const ApodResult ap = getApodFromAtt(10.0, att, 1.5);
    r.v("apod_V", ap.v);
    r.v("apod_0", ap.apods(0));
    r.v("apod_5", ap.apods(5));

    // --- defineStimFreqs ---
    r.v("stimfreqs_650_0", defineStimFreqs("650", 8)(0));
    r.v("stimfreqs_high_7", defineStimFreqs("high", 8)(7));

    // --- interp1 ---
    Eigen::VectorXd ix(4), iy(4);
    ix << 0, 1, 2, 3;
    iy << 0, 10, 30, 60;
    r.v("interp1_1p5", interp1(ix, iy, 1.5));
    r.v("interp1_2p5", interp1(ix, iy, 2.5));

    // --- pressureToDutyCycleGivenTransmission ---
    Eigen::VectorXd calDuty(7), calPressure(7);
    calDuty << 40, 45, 56, 75, 80, 90, 97.5;
    calPressure << 0.75, 1.12, 1.72, 2.95, 3.1, 3.72, 3.92;
    r.v("duty_0p65_0p2", pressureToDutyCycleGivenTransmission(0.65, 0.2, calDuty, calPressure));

    // --- getPauseIntervals ---
    const std::vector<double> pi = getPauseIntervals(1.0, 0.3);
    r.i("pause_count", static_cast<long>(pi.size()));
    r.v("pause_last", pi.back());

    // --- defineStimParams ---
    const std::vector<std::vector<int>> txElements{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                                                   16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30}};
    const std::vector<std::vector<int>> txElementsArray = txElements;
    const std::vector<int> rxElements{1, 2, 3};
    Eigen::MatrixX3d positions(1, 3);
    positions << 10, 0, 150;
    Eigen::VectorXd apods = Eigen::VectorXd::Ones(kNel);
    Eigen::VectorXd correctionDelays = Eigen::VectorXd::Zero(kNel);
    const std::vector<StimParams> sp =
        defineStimParams(array, 1500.0, txElements, txElementsArray, rxElements, positions, freqsMHz, apods,
                         correctionDelays);
    r.i("stimparams_count", static_cast<long>(sp.size()));
    r.v("stimparams_centerFreqMHz", sp[0].centerFrequencyMHz);
    r.v("stimparams_delaysCycle_0", sp[0].delaysCycle(0));
    r.v("stimparams_delaysCycle_last", sp[0].delaysCycle(sp[0].delaysCycle.size() - 1));
    r.v("stimparams_delaysSeconds_0", sp[0].delaysSeconds(0));
    r.v("stimparams_delaysSteering_0", sp[0].delaysSteering(0));

    // --- getTxAndBurstEvents ---
    SonicationSchedule s;
    s.startTime = 0.5;
    s.bi = 1.0;
    s.bd = 0.4;
    s.pi = 0.1;
    s.pd = 0.05;
    const TxAndBurstEvents ev = getTxAndBurstEvents({s}, {2}, {{0.1, 0.2}}, {3}, {2});
    r.i("events_burst_count", static_cast<long>(ev.burstEvents.size()));
    r.i("events_tx_count", static_cast<long>(ev.txEvents.size()));
    r.v("events_burst1_timeOn", ev.burstEvents[1].timeOn);
    r.v("events_burst1_timeOff", ev.burstEvents[1].timeOff);
    r.v("events_txlast_timeOn", ev.txEvents.back().timeOn);
    r.v("events_txlast_timeOff", ev.txEvents.back().timeOff);

    return 0;
}

}  // namespace

int main() {
    try {
        return run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "parity_stimulation: %s\n", e.what());
        return 1;
    }
}
