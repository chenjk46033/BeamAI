#include <cstdio>
#include <exception>

#include "infra_mat/legacy_beam_session.hpp"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: beam_mat_info <Beam-session.mat>\n");
        return 2;
    }
    try {
        const auto mri = beam::infra::mat::loadLegacyBeamMri(argv[1]);
        std::printf("volume=%lldx%lldx%lld\nLR=%.3f..%.3f\nAP=%.3f..%.3f\nIS=%.3f..%.3f\n",
                    static_cast<long long>(mri.volume.nx), static_cast<long long>(mri.volume.ny),
                    static_cast<long long>(mri.volume.nz), mri.axes.dimLR(0), mri.axes.dimLR(mri.axes.dimLR.size()-1),
                    mri.axes.dimAP(0), mri.axes.dimAP(mri.axes.dimAP.size()-1),
                    mri.axes.dimIS(0), mri.axes.dimIS(mri.axes.dimIS.size()-1));
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "import failed: %s\n", error.what());
        return 1;
    }
}
