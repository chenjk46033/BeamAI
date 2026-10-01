// make_mri_fixture -- converts a DICOM series into the de-identified NIfTI
// checked in under testdata/.
//
// The source series carries real identifiers in its DICOM headers (patient
// name, referring physician, study dates), which is why `.gitignore` refuses
// `*.dcm` wholesale. Round-tripping through beam::mri::writeNiftiVolume drops
// all of it structurally rather than by blacklisting tags: NiftiHeader holds
// only geometry and datatype -- it has no descrip, aux_file, db_name or
// intent_name field to carry text, so the writer emits those as zero bytes
// and there is nothing for a forgotten tag to hide in.
//
//   make_mri_fixture <dicom-dir-or-file> <out.nii>
//
// Run once to produce the fixture; the result is committed, so a normal build
// never needs this tool or the original series.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <string>
#include <vector>

#include "infra_dicom/load_mri_ras.hpp"
#include "mri/nifti_file.hpp"
#include "mri/ras_transform.hpp"


// --verify <dicom-dir> <nii>: does loading the fixture reproduce what
// loading the original series gives? Both volume and axes must match, or
// fiducials measured against one will not line up on the other.
int verify(const char* dicomPath, const char* niiPath) {
    const beam::mri::MriVolumeRas a = beam::infra::dicom::loadMriRas(dicomPath);
    const beam::mri::MriVolumeRas b = beam::infra::dicom::loadMriRas(niiPath);

    const auto report = [](const char* name, const Eigen::VectorXd& x, const Eigen::VectorXd& y) {
        const double d = (x.size() == y.size()) ? (x - y).cwiseAbs().maxCoeff() : -1.0;
        std::printf("  %-4s dicom[%9.4f .. %9.4f]  nii[%9.4f .. %9.4f]  maxdiff %.3e\n", name,
                    x(0), x(x.size() - 1), y(0), y(y.size() - 1), d);
        return d;
    };
    std::printf("axes:\n");
    double axisErr = std::max({report("LR", a.axes.dimLR, b.axes.dimLR),
                               report("AP", a.axes.dimAP, b.axes.dimAP),
                               report("IS", a.axes.dimIS, b.axes.dimIS)});

    double voxErr = -1.0;
    if (a.volume.voxels.size() == b.volume.voxels.size() && a.volume.dims == b.volume.dims) {
        voxErr = 0.0;
        for (std::size_t i = 0; i < a.volume.voxels.size(); ++i)
            voxErr = std::max(voxErr, std::abs(a.volume.voxels[i] - b.volume.voxels[i]));
    }
    std::printf("voxels: dims %dx%dx%d vs %dx%dx%d   maxdiff %.3e\n", a.volume.dims[0],
                a.volume.dims[1], a.volume.dims[2], b.volume.dims[0], b.volume.dims[1],
                b.volume.dims[2], voxErr);

    const bool ok = axisErr >= 0.0 && axisErr < 1e-3 && voxErr == 0.0;
    std::printf("%s\n", ok ? "MATCH -- the fixture loads as the series does"
                           : "MISMATCH -- fiducials will not line up");
    return ok ? 0 : 1;
}
int main(int argc, char** argv) {
    if (argc == 4 && std::string(argv[1]) == "--verify") {
        try {
            return verify(argv[2], argv[3]);
        } catch (const std::exception& error) {
            std::fprintf(stderr, "make_mri_fixture: %s\n", error.what());
            return 1;
        }
    }
    if (argc != 3) {
        std::fprintf(stderr, "usage: make_mri_fixture <dicom-dir-or-file> <out.nii>\n");
        return 2;
    }
    try {
        const beam::mri::MriVolumeRas loaded = beam::infra::dicom::loadMriRas(argv[1]);
        const beam::mri::RasAxisVectors axes = loaded.axes;

        // loaded.volume is already reordered into RAS voxel order, but
        // loaded.header still describes the file's ORIGINAL orientation --
        // writing one with the other would silently corrupt the geometry.
        // So emit a canonical RAS-aligned header instead: with i->LR,
        // j->AP, k->IS the affine is diagonal, and its rows come straight
        // from the axis vectors the loader already resolved. Reading this
        // file back must regenerate those same axes.
        const auto step = [](const Eigen::VectorXd& axis) {
            return axis.size() > 1 ? axis(1) - axis(0) : 1.0;
        };

        // The DICOM loader can hand back descending axes (here AP and IS run
        // anterior->posterior and superior->inferior). Storing that verbatim
        // would be faithful but ambiguous: a reader that normalises to RAS
        // flips the voxels back, so the file's array order would not match
        // what the loader produced. Flip those axes here instead, so the
        // fixture is canonical -- every axis ascending -- and any reader
        // yields the same voxels in the same order.
        // Faithful, NOT canonicalised. An earlier version flipped the
        // descending axes to make every axis ascending; that produced a
        // self-consistent file whose array landed in air (mean intensity
        // under the 160 registered elements fell to 0.08x the volume mean,
        // versus 2.98x for the loader's own orientation). The fiducials were
        // measured in BeamAI's convention, so the fixture has to keep it.
        //
        // Why flip the DATA while keeping the loader's own (descending) axes
        // in the header: reading a .nii back goes through
        // applyVoxelRasXform3D, which reorders any axis whose header step is
        // negative -- but the axis vectors still come from the header, not
        // from the reordered volume (the mismatch MriVolumeRas documents).
        // So storing the volume verbatim makes the reader flip AP and IS on
        // load, and the fixture comes back mirrored under fiducials that were
        // measured against the unflipped series. Pre-flipping here cancels
        // that: the reader's flip restores exactly what loadMriRas returns
        // for the DICOM series. `--verify` checks this.
        std::array<bool, 3> flip{step(axes.dimLR) < 0.0, step(axes.dimAP) < 0.0,
                                 step(axes.dimIS) < 0.0};
        const std::array<int, 3> n = loaded.volume.dims;
        std::vector<double> voxelsRas(loaded.volume.voxels.size());
        for (int k = 0; k < n[2]; ++k) {
            const int kk = flip[2] ? n[2] - 1 - k : k;
            for (int j = 0; j < n[1]; ++j) {
                const int jj = flip[1] ? n[1] - 1 - j : j;
                for (int i = 0; i < n[0]; ++i) {
                    const int ii = flip[0] ? n[0] - 1 - i : i;
                    voxelsRas[static_cast<std::size_t>((static_cast<long long>(k) * n[1] + j) * n[0] + i)] =
                        loaded.volume.voxels[static_cast<std::size_t>(
                            (static_cast<long long>(kk) * n[1] + jj) * n[0] + ii)];
                }
            }
        }
        std::printf("flipped  : LR=%d AP=%d IS=%d (to make every axis ascending)\n",
                    flip[0] ? 1 : 0, flip[1] ? 1 : 0, flip[2] ? 1 : 0);

        // Header axes must describe the voxels as stored. With no flip that
        // is simply what the loader reported, descending axes included.
        const beam::mri::RasAxisVectors rasAxes = axes;

        beam::mri::NiftiHeader header{};
        header.dim[0] = 3;
        header.dim[1] = loaded.volume.dims[0];
        header.dim[2] = loaded.volume.dims[1];
        header.dim[3] = loaded.volume.dims[2];
        header.pixdim[0] = 1.0;  // qfac
        header.pixdim[1] = std::abs(step(rasAxes.dimLR));
        header.pixdim[2] = std::abs(step(rasAxes.dimAP));
        header.pixdim[3] = std::abs(step(rasAxes.dimIS));
        // The DICOM path synthesises its header, so datatype is unset (0) --
        // pick one that stores these voxels exactly. DICOM pixel data is
        // integral, and stays so unless a RescaleSlope made it fractional;
        // int16 then costs half what float32 would.
        const auto& voxels = voxelsRas;
        double lo = voxels.empty() ? 0.0 : voxels.front();
        double hi = lo;
        bool integral = true;
        for (const double v : voxels) {
            lo = std::min(lo, v);
            hi = std::max(hi, v);
            if (integral && v != std::floor(v)) integral = false;
        }
        const bool fitsI16 = integral && lo >= -32768.0 && hi <= 32767.0;
        header.datatype = fitsI16 ? 4 : 16;  // DT_INT16 : DT_FLOAT32
        header.bitpix = fitsI16 ? 16 : 32;
        std::printf("values   : [%.4f .. %.4f]%s\n", lo, hi,
                    integral ? " (integral)" : " (fractional)");
        header.sformCode = 1;  // NIFTI_XFORM_SCANNER_ANAT
        header.qformCode = 0;  // sform alone defines the geometry
        header.srowX = Eigen::RowVector4d(step(rasAxes.dimLR), 0.0, 0.0, rasAxes.dimLR(0));
        header.srowY = Eigen::RowVector4d(0.0, step(rasAxes.dimAP), 0.0, rasAxes.dimAP(0));
        header.srowZ = Eigen::RowVector4d(0.0, 0.0, step(rasAxes.dimIS), rasAxes.dimIS(0));

        std::printf("source   : %s\n", argv[1]);
        std::printf("volume   : %d x %d x %d\n", header.dim[1], header.dim[2], header.dim[3]);
        std::printf("voxel mm : %.4f %.4f %.4f\n", header.pixdim[1], header.pixdim[2],
                    header.pixdim[3]);
        std::printf("datatype : %d (bitpix %d)\n", header.datatype, header.bitpix);
        std::printf("LR       : [%.4f .. %.4f]\n", rasAxes.dimLR(0), rasAxes.dimLR(rasAxes.dimLR.size() - 1));
        std::printf("AP       : [%.4f .. %.4f]\n", rasAxes.dimAP(0), rasAxes.dimAP(rasAxes.dimAP.size() - 1));
        std::printf("IS       : [%.4f .. %.4f]\n", rasAxes.dimIS(0), rasAxes.dimIS(rasAxes.dimIS.size() - 1));

        beam::mri::writeNiftiVolume(argv[2], header, voxelsRas);
        std::printf("wrote    : %s (%zu voxels)\n", argv[2], loaded.volume.voxels.size());

        // Read it back and confirm the axes survive the round trip -- a
        // mismatch here means the fixture would register differently from
        // the series it was made from.
        const beam::mri::RasAxisVectors back =
            beam::mri::getRasAxisVectors(beam::mri::readNiftiHeader(argv[2]));
        const double axisError = std::max({(back.dimLR - rasAxes.dimLR).cwiseAbs().maxCoeff(),
                                           (back.dimAP - rasAxes.dimAP).cwiseAbs().maxCoeff(),
                                           (back.dimIS - rasAxes.dimIS).cwiseAbs().maxCoeff()});
        // NIfTI-1 stores srow/pixdim as float32, so ~1e-5 mm of rounding on
        // coordinates this size is the format's floor, not a defect. A
        // micron is still four orders of magnitude finer than anything the
        // registration cares about, and would catch a real geometry error.
        std::printf("axis round-trip error: %.3e mm\n", axisError);
        if (axisError > 1e-3) {
            std::fprintf(stderr, "make_mri_fixture: axes did not survive the round trip\n");
            return 1;
        }
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "make_mri_fixture: %s\n", error.what());
        return 1;
    }
}
