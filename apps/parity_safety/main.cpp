// parity_safety -- runs the Beam Safety ports on synthetic sonication
// params, writing results for matlab_verify/verify_safety.m. See
// matlab_verify/README.md.
//
// Unlike the other phases, BeamV0's Safety .m functions
// (checkSonicationSafety, getISPTAFromStimParams, getMechanicalIndex) are
// GUI callbacks that take `app` and call Verasonics/GUI code, so they can't
// be invoked directly. verify_safety.m instead evaluates the acoustic
// formulae copied verbatim from those source files (with line references),
// and calls the two functions that *are* directly callable
// (getMaxSteeringRange, getMaxSonicationAmplitude).

#include <cstdio>
#include <exception>
#include <fstream>
#include <iomanip>
#include <string>

#include "safety/intensity.hpp"
#include "safety/limits.hpp"

using namespace beam::safety;

namespace {

struct Results {
    std::ofstream out;
    explicit Results(const std::string& p) : out(p) { out << std::setprecision(17); }
    void v(const std::string& k, double x) { out << k << "," << x << "\n"; }
    void i(const std::string& k, long x) { out << k << "," << x << "\n"; }
};

int run() {
    Results r("parity_safety_cpp.csv");

    SonicationSafetyParams s1;
    s1.amplitudeMPa = 2.5;
    s1.centerFrequencyMHz = 0.65;
    s1.pd = 0.02; s1.pi = 0.05; s1.bd = 0.3; s1.bi = 1.0;
    s1.startTime = 0.0; s1.endTime = 10.0;

    SonicationSafetyParams s2;
    s2.amplitudeMPa = 1.8;
    s2.centerFrequencyMHz = 0.70;
    s2.pd = 0.10; s2.pi = 0.08; s2.bd = 0.5; s2.bi = 0.4;
    s2.startTime = 0.0; s2.endTime = 2.0;

    SonicationSafetyParams s3;
    s3.amplitudeMPa = 3.0;
    s3.centerFrequencyMHz = 0.65;
    s3.pd = 0.04; s3.pi = 0.05; s3.bd = 0.4; s3.bi = 0.5;
    s3.startTime = 0.0; s3.endTime = 10.0;

    // --- isppa (both source densities) ---
    r.v("isppa_1040_s1", isppa(s1.amplitudeMPa));           // checkSonicationSafety.m: rho 1040
    r.v("isppa_1040_s2", isppa(s2.amplitudeMPa));
    r.v("isppa_1046_s3", isppa(s3.amplitudeMPa, 1046.0));   // getISPTAFromStimParams.m: rho 1046

    // --- mechanical index ---
    r.v("mi_s1", mechanicalIndex(s1.amplitudeMPa, s1.centerFrequencyMHz));
    r.v("mi_s2", mechanicalIndex(s2.amplitudeMPa, s2.centerFrequencyMHz));

    // --- ISPTA (burst-duration / burst-interval) ---
    const IsptaResult ispta = isptaFromParams(s3);
    r.v("isptaBurstDuration_s3", ispta.isptaBurstDuration);
    r.v("isptaBurstInterval_s3", ispta.isptaBurstInterval);

    // --- limits ---
    const Eigen::Matrix<double, 3, 2> steer = maxSteeringRangeDegrees();
    for (int a = 0; a < 3; ++a)
        for (int b = 0; b < 2; ++b) r.v("maxSteer_" + std::to_string(a) + std::to_string(b), steer(a, b));
    r.v("maxAmp", kMaxSonicationAmplitudeMPa);

    return 0;
}

}  // namespace

int main() {
    try {
        return run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "parity_safety: %s\n", e.what());
        return 1;
    }
}
