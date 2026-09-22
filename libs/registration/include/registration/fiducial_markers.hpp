#pragma once

#include <string>
#include <vector>

#include <Eigen/Core>

#include "array/array_types.hpp"

// Ported from BeamV0/GUIMatlab/BEAM/Registration/:
//   getArrayFiducialMarkerNames.m, applyAffineMatrixToFrameData.m,
//   MRINeuroNav/TranslateArrayPosition/getTransducerFiducialMarkersPositionFromFrame.m,
//   MRINeuroNav/TranslateArrayPosition/getFiducialPositionFromName.m
//     -- byte-identical (first three) or trivial (last) vs DiademV0; copied
//        from Diadem's libs/registration where identical.
//   setArrayFiducialMarkers.m, .../getTransducerFiducialMarkersFromFiducials
//     -- BeamV0-specific (different marker set/geometry than DiademV0),
//        ported from BeamV0's own source.
//
// GUI/session state (registration-complete flags, live slider widgets) is
// out of scope; these are the pure per-point-list transforms.

namespace beam::registration {

struct FiducialMarker {
    std::string name;
    Eigen::Vector3d position = Eigen::Vector3d::Zero();
};

// Ported from Registration/getArrayFiducialMarkerNames.m: the 8 "EL*" names.
// NOTE: BeamV0's setArrayFiducialMarkers.m no longer uses these -- it builds
// its own 6 "LeftY1Z3"/"RightY1Z1"/... names inline. Kept for parity with
// the file, but it is effectively dead in the current Beam path.
std::vector<std::string> getArrayFiducialMarkerNames();

// Ported from BeamV0's setArrayFiducialMarkers.m. Computes the 6 fiducial
// marker ("donut") positions from the array geometry: each starts at a
// fixed offset (+/-19mm x, -22.5mm y, +20mm z) from its array half's rect
// centre, then is rotated about that half's Y1Z1 marker by the array's own
// x/y/z orientation basis (built from elements 1/81, 24/1, 31/40).
//
// Faithful-port note: BeamV0's defineArrayData makes array[0] and array[1]
// identical full copies, so the per-half `array(designation).rect` centre
// is the same for every marker -- ported as-is (indexed by designation),
// not "fixed".
std::vector<FiducialMarker> setArrayFiducialMarkers(const beam::array::ArrayData& arrayData);

// Ported from applyAffineMatrixToFrameData.m's per-list transform: applies
// a 4x4 homogeneous affine to every marker position.
std::vector<FiducialMarker> applyAffineMatrixToFiducialMarkers(
    const Eigen::Matrix4d& affineMatrix, const std::vector<FiducialMarker>& markers);

// Ported from getTransducerFiducialMarkersPositionFromFrame.m. Given
// marker positions recorded at a baseline slider position, returns where
// they'd be at a new slider position -- the slider delta becomes a
// physical (0, dY, dZ) displacement via the calibration deltas, rotated by
// regRotation (MATLAB's frame.reg.M(1:3,1:3)).
std::vector<FiducialMarker> getTransducerFiducialMarkersPositionFromFrame(
    const std::vector<FiducialMarker>& baselineMarkers, const Eigen::Matrix3d& regRotation,
    double baselineVerticalPosition, double baselineHorizontalPosition, double verticalDelta,
    double horizontalDelta, double currentVerticalPosition, double currentHorizontalPosition);

// Ported from MRINeuroNav/.../getFiducialPositionFromName.m: linear search
// by name. Last match wins (matching the MATLAB loop). Throws
// std::invalid_argument if the name isn't present (MATLAB would error on
// the then-undefined output).
Eigen::Vector3d getFiducialPositionFromName(const std::string& name,
                                             const std::vector<FiducialMarker>& markers);

// Ported from MRINeuroNav/.../getTranslationMatrixFromTransducerFiducials.m.
// Builds an (approximately orthonormal) X/Y/Z basis from the 6 named fiducials'
// pairwise differences (median of the per-pair unit vectors), for
// converting a slider displacement into the registration's frame. The
// MATLAB `app` argument is unused in the source body and dropped.
struct TransducerBasis {
    Eigen::Matrix3d m;  // rows are xVector, yVector, zVector
    Eigen::Vector3d xVector;
    Eigen::Vector3d yVector;
    Eigen::Vector3d zVector;
};
TransducerBasis getTranslationMatrixFromTransducerFiducials(const std::vector<FiducialMarker>& markers);

}  // namespace beam::registration
