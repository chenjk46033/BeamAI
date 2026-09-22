#pragma once

#include <vector>

#include <Eigen/Core>

#include "registration/fiducial_markers.hpp"

// Ported from BeamV0/GUIMatlab/BEAM/Registration/ImageBasedModel/ -- the
// pure-algorithm parts of the photo-based transducer-detection pipeline
// (perimeter walking, scalp-marker sampling, slot organisation). The image
// I/O and interactive picking (`getImageData.m`, `imread`/`imwarp`) stay
// deferred -- see docs/known_gaps_registration.md.

namespace beam::registration {

// Port of ImageBasedModel/getImgOrientationAngle.m's EXIF-Orientation ->
// rotation-angle mapping (1->0, 3->180, 6->90, 8->-90 degrees). The
// `imfinfo` read is the caller's job. Throws std::invalid_argument for a
// non-standard tag (the MATLAB source `disp`s and leaves the output unset).
double orientationAngleFromExif(int orientationTag);

// Port of ImageBasedModel/Lib/perimwalkImage2.m. Orders the scattered
// perimeter pixels `perimline` (N x 2, integer [row, col]) into a single
// connected path by an 8-connectivity DFS from `startpoint`, then splits
// at coordinate jumps > 10 and returns the best-scoring segment. Throws
// std::invalid_argument if startpoint isn't in perimline.
Eigen::MatrixX2i perimwalkImage2(const Eigen::MatrixX2i& perimline, const Eigen::Vector2i& startpoint);

// Port of ImageBasedModel/Lib/perimdistanceImage.m. Step distances along an
// ordered perimeter, each axis scaled by hRatio / wRatio. Returns a vector
// of length rows-1.
Eigen::VectorXd perimdistanceImage(const Eigen::MatrixX2i& perimlineSorted, double hRatio, double wRatio);

// Port of ImageBasedModel/getDiscreteFromImage.m. `contour`: a binary
// image (nonzero = perimeter pixel). `spacings`: mm/pixel (isotropic).
// nzImg / izImg: the nasion / inion pixel locations as [x, y] (0-based, as
// the source receives them -- it swaps and +1's internally). Returns up to
// 7 marker voxels ([row, col], 1-based) sampled at 0/10/30/50/70/90/100 %
// of the Nz->Iz scalp distance, or an empty matrix if the inion index
// falls past the end of the cumulative-distance array (the source's
// `if IzCntIdx <= length(sumdist)` guard).
Eigen::MatrixX2i getDiscreteFromImage(const Eigen::MatrixXi& contour, double spacings,
                                       const Eigen::Vector2i& nzImg, const Eigen::Vector2i& izImg);

// Port of ImageBasedModel/organizeTransducerSlots.m. transducerSlotsRasMm:
// N x 3 (columns x, Y, Z; N >= 4) detected slot positions in RAS mm.
// arrayFiducialMarkers: the 6 markers from setArrayFiducialMarkers (name +
// position in metres -- the x is used, *1000). Returns 6 FiducialMarkers
// whose positions are [x_mm, Y, Z] with Y/Z taken from the sorted slot
// rows (bottom row / left column) and x from the matching array marker.
std::vector<FiducialMarker> organizeTransducerSlots(const Eigen::MatrixX3d& transducerSlotsRasMm,
                                                     const std::vector<FiducialMarker>& arrayFiducialMarkers);

}  // namespace beam::registration
