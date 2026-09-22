#pragma once

#include <vector>

#include <Eigen/Core>

#include "array/array_types.hpp"
#include "mri/ras_transform.hpp"
#include "mri/slice.hpp"

// Ported from BeamV0/GUIMatlab/BEAM/GUI/imagePositionToIJK.m and the
// rasterization loop in GUI/drawTransducersOnMRI.m, plus new plumbing
// (rasterizeFiducialMarkersOntoMriGrid) that gives the Register tab's MRI
// viewer (libs/gui_qt/mri_slice_view) an array-footprint / fiducial-marker
// overlay without porting drawROIs.m / drawFocusOnMRI.m / setFiducialTemplate.m
// (interactive images.roi.* markers and the 3D ellipsoid template -- see
// docs/known_gaps_gui.md for what's still deferred there).

namespace beam::gui {

// Ported from GUI/imagePositionToIJK.m: for each axis, the index of the
// closest entry in that axis's physical-mm coordinate vector
// ([~,i] = min(abs(position(1)-sys.ax)), etc). Returns 0-based indices
// (this project's convention) rather than MATLAB's 1-based ones -- callers
// indexing into a beam::mri::Volume3D use these directly.
struct VoxelIndex {
    Eigen::Index i = 0;
    Eigen::Index j = 0;
    Eigen::Index k = 0;
};
VoxelIndex imagePositionToVoxelIndex(const Eigen::Vector3d& positionMm, const beam::mri::RasAxisVectors& axes);

// Ported from GUI/drawTransducersOnMRI.m: for every element of `arrayTotal`,
// spatially samples its face (beam::array::spatiallySampleElement, already
// ported/tested, at fs = min(axis spacing)/2, matching the source's
// `fs = min(sys.aRes)/2`) and marks each sample's nearest voxel as 1.0 in
// an (nx, ny, nz) mask volume shaped like the MRI grid. MATLAB dedupes
// samples with `unique(pointsIJK,'rows')` before marking; that's a
// performance step only (marking the same voxel twice is a no-op), so it's
// not replicated here. imagePositionToVoxelIndex always returns a valid
// index into the *axis vector* (it's a nearest-neighbor search), but that
// index can still land outside [0,nx)x[0,ny)x[0,nz) if the axis vectors
// are longer/shorter than the mask volume -- exactly the RAS-reordering/
// axis-length mismatch documented on beam::mri::MriVolumeRas. MATLAB's
// unchecked `arrayImage(i,j,k)=1` would error in that case; this drops
// such samples instead, a safety deviation (avoiding undefined behavior on
// an out-of-range Eigen access), not a behavior port.
beam::mri::Volume3D rasterizeArrayOntoMriGrid(const beam::array::ArrayStruct& arrayTotal,
                                               const beam::mri::RasAxisVectors& axes, Eigen::Index nx,
                                               Eigen::Index ny, Eigen::Index nz);

// New infrastructure -- no single BeamV0 `.m` counterpart. Rasterizes each
// fiducial marker's mm position (already-scaled the way setFiducialROIs.m
// scales them -- see FiducialTableModel) as a small
// (2*radiusVoxels+1)-cube blob into an (nx, ny, nz) mask volume, the same
// shape rasterizeArrayOntoMriGrid produces. Stands in for drawROIs.m's
// per-plane `images.roi.Point` markers with a static highlight instead --
// no draggable ROIs, no per-slice name label (both App-Designer-specific
// interaction; see docs/known_gaps_gui.md). Out-of-range positions are
// clipped the same way rasterizeArrayOntoMriGrid clips them.
beam::mri::Volume3D rasterizeFiducialMarkersOntoMriGrid(const std::vector<Eigen::Vector3d>& positionsMm,
                                                          const beam::mri::RasAxisVectors& axes, Eigen::Index nx,
                                                          Eigen::Index ny, Eigen::Index nz,
                                                          Eigen::Index radiusVoxels = 2);

// Ported from GUI/RegistrationTab/AutoReg/setFiducialTemplate.m, scoped to
// its GUI/drawFocusOnMRI.m call site only: `setFiducialTemplate(sys,
// 'focus', centerArrayMM, 1)` -- a single point, `fiducialRotation` = the
// scalar 1 (MATLAB broadcasts `xyz*1` unchanged), not a real rotation
// matrix. `fiducialShape` ("focus") is dead -- the source never branches
// on it. The *other* call site, RegistrationTab/AutoReg/drawFiducialsOnMRI.m,
// passes a real 3x3 rotation and a *list* of markers through the same
// hardcoded `for i = 1` (so it only ever stamps the first one anyway, via
// a linear-indexing hazard in imagePositionToIJK when given an Nx3 array)
// -- not ported here; still deferred (see docs/known_gaps_gui.md).
//
// The source computes the ellipsoid indicator
// `(x/30)^2 + (y/5)^2 + (z/5)^2 < 1` on a grid 4x finer than the voxel
// grid (`dec = 4`), then `interp3`s down to voxel resolution. Reproduced
// faithfully (trilinear blend of the 8 surrounding fine-lattice values),
// but without materializing that fine grid (it can be ~10s of millions of
// points for a ~1mm voxel size) -- the indicator is analytic, so each
// voxel's 8 fine-lattice neighbors are evaluated on demand instead. In
// practice this reduces to an exact 0/1 lookup for this unrotated call
// site: the coarse grid's spacing is exactly 4x the fine grid's and both
// start at the same origin, so every coarse voxel lands exactly on a fine
// lattice point (mod floating-point rounding). A real (non-identity)
// rotation would break that exact nesting and make the blend matter --
// but that only happens at the *other* call site (a real 3x3 rotation),
// which isn't ported here. `innerD` (declared, never read) is dropped.
//
// Placement deviation: the source centers the (already-computed) stamp on
// the focus voxel via a fixed-size index window, but if that window is
// clipped at a volume edge it still reads the stamp from its own index 1
// (not the correspondingly-shifted offset) -- silently misaligning the
// stamp. This reimplementation always reads the geometrically-correct
// stamp voxel and simply drops the ones that fall outside the volume, a
// safety/correctness deviation (avoiding undefined behavior on an
// out-of-range Eigen access), not a behavior port -- like
// rasterizeArrayOntoMriGrid's out-of-range handling above.
beam::mri::Volume3D rasterizeFocusEllipsoidOntoMriGrid(const Eigen::Vector3d& focusPositionMm,
                                                        const beam::mri::RasAxisVectors& axes, Eigen::Index nx,
                                                        Eigen::Index ny, Eigen::Index nz);

}  // namespace beam::gui
