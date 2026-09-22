#pragma once

#include <string>
#include <vector>

#include <Eigen/Core>

namespace beam::mri {

// A 3D image volume, indexed (i, j, k) = (LR, AP, IS) to match how BeamV0's
// MRI code indexes `img`. Stored as one nx-by-ny matrix per k-slice.
struct Volume3D {
    Eigen::Index nx = 0;
    Eigen::Index ny = 0;
    Eigen::Index nz = 0;
    std::vector<Eigen::MatrixXd> kSlices;  // kSlices[k] is nx x ny

    double operator()(Eigen::Index i, Eigen::Index j, Eigen::Index k) const {
        return kSlices[static_cast<size_t>(k)](i, j);
    }
};

// Port of BeamV0/GUIMatlab/BEAM/MRI/getSliceImage.m.
// plane: "sagital", "coronal", or "axial" (the source's spellings).
// sliceIndex is 1-based (MATLAB). Extracts that plane and applies one
// counter-clockwise 90 deg rotation -- MATLAB's `imrotate(..., 90)`, which
// for an exact quarter turn is just rot90 (no interpolation, no resize).
// Throws std::invalid_argument on an unknown plane name.
Eigen::MatrixXd getSliceImage(const Volume3D& img, int sliceIndex, const std::string& plane);

// Ported from GUI/updateSysWithMRI.m: `app.sys.window = [0, max(aImg(:))]`
// -- the display intensity range a viewer should map to black/white.
struct DisplayWindow {
    double lo = 0.0;
    double hi = 0.0;
};
DisplayWindow computeDisplayWindow(const Volume3D& img);

}  // namespace beam::mri
