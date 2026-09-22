#pragma once

#include "array/array_types.hpp"
#include "mri/ras_transform.hpp"
#include "registration/array_transform.hpp"

// Ported from BeamV0/GUIMatlab/BEAM/GUI/initTransducers.m's real
// computation: centers the array's own centroid on a newly-loaded MRI
// volume's physical center, so the demo array/fiducial/focus overlay
// starts out somewhere inside the volume rather than wherever the
// array's own nominal rect coordinates happen to place it. Called once,
// right after loading an MRI file, before any fiducial-based
// registration.

namespace beam::gui {

// Dropped: `transformArrayDataToHFSRAS.m` -- the source calls it first,
// but its entire body is commented out beneath a `% TODO` (only the
// function signature is live code), so `arrayData =
// transformArrayDataToHFSRAS(arrayData)` is the identity function.
// Disclosed, not silently skipped. `defineTranslateAffineMatrix.m`'s own
// 4x4 construction is just `Eigen::Matrix4d::Identity()` plus a
// translation block -- not worth a separate named port either.
//
// The `[0, 50, -25]` mm offset (`centerArrayMM - [0,50,-25]/1000` in the
// source) is a bare literal in `initTransducers.m` with no comment
// explaining it -- presumably a coarse head-origin-vs-array-origin
// correction; reproduced as-is, not derived.
beam::registration::AffineArrayResult centerArrayOnMri(const beam::array::ArrayData& arrayData,
                                                         const beam::mri::RasAxisVectors& axes);

// Port of GUI/initArrayFramePosition.m: the "Array Lock Position Subject
// Left/Right" physical sliders' startup default (both horizontal and
// vertical = 1, the sliders' own minimum -- "no shift" from the
// fiducial-based registration in registerCurrentTransducerPosition).
struct ArrayFramePosition {
    double horizontal = 1.0;
    double vertical = 1.0;
};
ArrayFramePosition defaultArrayFramePosition();

}  // namespace beam::gui
