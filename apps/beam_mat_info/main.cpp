#include <cstdio>
#include <exception>
#include <fstream>
#include <string>

#include "infra_mat/legacy_beam_session.hpp"

namespace {

void printSummary(const beam::infra::mat::LegacyBeamMri& mri) {
    std::printf("volume=%lldx%lldx%lld\nLR=%.3f..%.3f\nAP=%.3f..%.3f\nIS=%.3f..%.3f\n",
                static_cast<long long>(mri.volume.nx), static_cast<long long>(mri.volume.ny),
                static_cast<long long>(mri.volume.nz), mri.axes.dimLR(0),
                mri.axes.dimLR(mri.axes.dimLR.size() - 1), mri.axes.dimAP(0),
                mri.axes.dimAP(mri.axes.dimAP.size() - 1), mri.axes.dimIS(0),
                mri.axes.dimIS(mri.axes.dimIS.size() - 1));
    std::printf("fiducials=%zu\n", mri.fiducials.size());
    for (const auto& marker : mri.fiducials)
        std::printf("  %s %.3f %.3f %.3f\n", marker.name.c_str(), marker.positionMm.x(),
                    marker.positionMm.y(), marker.positionMm.z());
}

// Writes the fiducials in the format the application's Import reads, so a
// BeamV0 session's measurements survive without the application having to keep
// a .mat reader in its UI.
int writeCsv(const beam::infra::mat::LegacyBeamMri& mri, const char* source, const char* path) {
    if (mri.fiducials.size() != 6) {
        std::fprintf(stderr, "expected 6 fiducials, found %zu\n", mri.fiducials.size());
        return 1;
    }
    std::ofstream out(path);
    if (!out) {
        std::fprintf(stderr, "cannot write %s\n", path);
        return 1;
    }
    const auto ends = [](const Eigen::VectorXd& axis) {
        return std::make_pair(axis(0), axis(axis.size() - 1));
    };
    const auto [lrFirst, lrLast] = ends(mri.axes.dimLR);
    const auto [apFirst, apLast] = ends(mri.axes.dimAP);
    const auto [isFirst, isLast] = ends(mri.axes.dimIS);

    out.precision(17);
    out << "# BeamAI measured fiducials, millimetres, RAS (+x right, +y anterior, +z superior)\n";
    out << "# MRI: " << source << "\n";
    out.precision(10);
    out << "# geometry: " << mri.volume.nx << ',' << mri.volume.ny << ',' << mri.volume.nz << ','
        << lrFirst << ',' << lrLast << ',' << apFirst << ',' << apLast << ',' << isFirst << ','
        << isLast << "\n";
    out << "name,x,y,z\n";
    out.precision(17);
    for (const auto& marker : mri.fiducials)
        out << marker.name << ',' << marker.positionMm.x() << ',' << marker.positionMm.y() << ','
            << marker.positionMm.z() << "\n";
    std::printf("wrote %s\n", path);
    return 0;
}

// Prints the plan index a session carries. The tables themselves are MATLAB
// `table` objects and do not survive the read -- the printed note says so,
// because an index that looks like a parity export would be worse than none.
void printPlan(const beam::infra::mat::LegacyBeamPlan& plan) {
    std::printf("targets=%zu\n", plan.targetNames.size());
    for (std::size_t i = 0; i < plan.targetNames.size(); ++i)
        std::printf("  %zu %s\n", i + 1, plan.targetNames[i].c_str());
    std::printf("protocols=%zu\n", plan.protocols.size());
    for (const auto& protocol : plan.protocols)
        std::printf("  %s sessions=%zu\n", protocol.name.c_str(), protocol.sessionCount);
    if (plan.tablePayloadsUnreadable)
        std::printf("note: the stim/protocol table payloads are MATLAB table objects and were not"
                    " readable; names and counts only\n");
}

}  // namespace

int main(int argc, char** argv) {
    const std::string mode = argc >= 3 ? argv[2] : std::string();
    if (argc < 2 || argc > 4 || (argc == 3 && mode != "--plan") ||
        (argc == 4 && mode != "--csv")) {
        std::fprintf(stderr,
                     "usage: beam_mat_info <Beam-session.mat>\n"
                     "       beam_mat_info <Beam-session.mat> --csv <out.csv>\n"
                     "       beam_mat_info <Beam-session.mat> --plan\n");
        return 2;
    }
    try {
        // --plan skips the MRI deliberately: the volume is most of the file.
        if (mode == "--plan") {
            printPlan(beam::infra::mat::loadLegacyBeamPlan(argv[1]));
            return 0;
        }
        const auto mri = beam::infra::mat::loadLegacyBeamMri(argv[1]);
        if (mode == "--csv") return writeCsv(mri, argv[1], argv[3]);
        printSummary(mri);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "import failed: %s\n", error.what());
        return 1;
    }
}
