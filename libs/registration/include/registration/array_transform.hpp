#pragma once

#include <vector>

#include <Eigen/Core>

#include "array/array_types.hpp"
#include "registration/fiducial_markers.hpp"

// Ported from BeamV0/GUIMatlab/BEAM/Arrays/applyAffineToArrayData.m. Lives
// in libs/registration (not libs/array) because its final step is
// setArrayFiducialMarkers, which needs the fiducial-marker geometry -- and
// libs/array can't depend on libs/registration.

namespace beam::registration {

struct AffineArrayResult {
    beam::array::ArrayData arrayData;
    std::vector<FiducialMarker> fiducialMarkers;
};

// Applies a 4x4 affine to arrayData.arrayTotal and both halves, rebuilds
// each via defineArrayStruct (so normals / opposing elements are
// recomputed), then recomputes the fiducial markers via
// setArrayFiducialMarkers.
//
// The MATLAB source's `varargin` (an optional affineMatrix passed on to
// setArrayFiducialMarkers) is not exposed -- BeamV0's setArrayFiducialMarkers.m
// declares `varargin` but never reads it, so both call paths are identical.
AffineArrayResult applyAffineToArrayData(const Eigen::Matrix4d& affineMatrix,
                                          beam::array::ArrayData arrayData);

// Ported from BeamV0/GUIMatlab/BEAM/Registration/registerArrayToFiducials.m:
// the "Register To MRI Fiducials" button (see the operator manual's
// Device to Subject Registration section) -- computes the best-fit
// affine mapping the array's own nominal fiducial positions onto the
// operator-edited MRI fiducial positions, then applies it to the array.
// `getAffineMatrixFromRegistration`'s fixed BeamV0 weights require
// exactly 6 points on each side (the source's implicit assumption, since
// `app.FiducialROIs`/`arrayData.fiducialMarkers` both always have 6
// entries); this checks that explicitly and throws std::invalid_argument
// rather than failing inside affineRegistration with a less obvious
// dimension-mismatch error. Both position lists are mm, in matching
// per-index order (the same order setArrayFiducialMarkers produces).
//
// Dropped (all commented-out or GUI-state in the source): the slider
// edit-field updates, and the trailing `drawTransducersOnMRI(app,
// arrayData)` call -- re-rasterizing the overlay is the caller's job,
// using the already-ported beam::gui::rasterizeArrayOntoMriGrid.
AffineArrayResult registerArrayToFiducials(const beam::array::ArrayData& originArrayData,
                                            const std::vector<Eigen::Vector3d>& mriFiducialsMm);

// Ported from Registration/MRINeuroNav/TranslateArrayPosition/
// registerCurrentTransducerPostion.m, scoped to its `MRIFiducialsButton`
// branch only -- the manual's "Outside the MRI" registration step
// ("Register Arrays to Current Position" button). The `MRIFree`/
// `PhotoBased` branches need state this port doesn't model
// (hardcoded MNI landmarks / an image-registration struct) and stay
// deferred -- see docs/known_gaps_registration.md.
//
// First calls registerArrayToFiducials (fits the array's nominal
// fiducials onto `mriFiducialsMm`, same as the "Register To MRI
// Fiducials" button), then converts the horizontal/vertical physical
// slider position into a world-frame translation via the already-ported
// getTranslationMatrixFromTransducerFiducials basis (with the source's
// own Z-vector sign-flip-if-negative reapplied here, since that ported
// function doesn't do it internally), and applies that translation on
// top. `horizontalSliderValue`/`verticalSliderValue`: the physical lock-
// position slider readings (both Left/Right sides must agree in the
// source, checked by the GUI itself, not here -- see the same doc for
// why the source's own alignment-mismatch warning isn't reproduced).
AffineArrayResult registerCurrentTransducerPosition(const beam::array::ArrayData& originArrayData,
                                                     const std::vector<Eigen::Vector3d>& mriFiducialsMm,
                                                     double horizontalSliderValue, double verticalSliderValue);

}  // namespace beam::registration
