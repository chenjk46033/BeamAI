#pragma once

#include <vector>

#include <Eigen/Core>
#include <Eigen/Geometry>

#include "registration/fiducial_markers.hpp"

// GUI phase, RegistrationTab slice: the pure coordinate math behind
// BeamV0/GUIMatlab/BEAM/GUI/RegistrationTab/{Targeting,AutoReg}/*.m,
// decoupled from the `app`/widget reads and the external `acpcDetect`
// (WSL) tool invocation. No Qt dependency.
//
// Everything else in RegistrationTab/ is `app`-coupled UI (fiducial ROI
// tables, transducer-position edit fields, MRI display) or drives the
// external acpcDetect tool via WSL (getACPCFromSys.m) -- deferred, see
// docs/known_gaps_gui.md.

namespace beam::gui {

// Port of Targeting/getACPCTransform.m: builds the 4x4 transform from
// anterior/posterior-commissure landmarks and the mid-sagittal-plane
// normal into the reference coordinate frame.
//
// Faithful-port notes (both preserved, not "fixed"):
//   - The X-axis orthogonalization step *adds* the Y-projection to X
//     (`X = X + dot(X,Y)*Y`) rather than subtracting it, which is what
//     Gram-Schmidt orthogonalization would do. That is what the source
//     literally computes.
//   - `disp(...)` debug prints in the source are dropped (no I/O here).
// Throws std::runtime_error if the resulting Z axis points the wrong way
// (Z(3) < 0), matching the source's `error('Coordinates in wrong
// orientation')`.
Eigen::Matrix4d getAcpcTransform(const Eigen::Vector3d& acXyz, const Eigen::Vector3d& pcXyz,
                                  const Eigen::Vector3d& midSagittalPlaneNormal,
                                  const Eigen::Vector3d& referenceCoordsXyz);

// Port of Targeting/applyReferenceCoordinateTransform.m. `transform` is the
// 4x4 from getAcpcTransform (MATLAB: app.sys.referenceCoordinate.Transform).
//
// Faithful-port note: the two directions are not a true forward/inverse
// pair in the usual affine sense -- MriToReference adds the translation
// *then* rotates; ReferenceToMri un-rotates *then* subtracts the
// translation. Ported exactly as the source computes it.
enum class ReferenceTransformDirection { kMriToReference, kReferenceToMri };
Eigen::Vector3d applyReferenceCoordinateTransform(const Eigen::Matrix4d& transform,
                                                   const Eigen::Vector3d& xyz,
                                                   ReferenceTransformDirection direction);

// Port of AutoReg/getArrayFiducialNormals.m: an axis-permuted (x,y,z) ->
// (z,x,y) plane normal and edge vector from the first three fiducial
// markers. The source's `i` parameter is unused in its live body (the code
// that would have used it is commented out) -- dropped here.
struct ArrayFiducialNormals {
    Eigen::Vector3d v1;  // fiducialMarkers[0].position - fiducialMarkers[1].position, permuted
    Eigen::Vector3d v2;  // normal through fiducialMarkers[0..2], permuted
};
ArrayFiducialNormals getArrayFiducialNormals(
    const std::vector<beam::registration::FiducialMarker>& fiducialMarkers);

}  // namespace beam::gui
