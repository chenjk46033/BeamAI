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

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2 && argc != 4) {
        std::fprintf(stderr,
                     "usage: beam_mat_info <Beam-session.mat>\n"
                     "       beam_mat_info <Beam-session.mat> --csv <out.csv>\n");
        return 2;
    }
    try {
        const auto mri = beam::infra::mat::loadLegacyBeamMri(argv[1]);
        if (argc == 4 && std::string(argv[2]) == "--csv") return writeCsv(mri, argv[1], argv[3]);
        printSummary(mri);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "import failed: %s\n", error.what());
        return 1;
    }
}
