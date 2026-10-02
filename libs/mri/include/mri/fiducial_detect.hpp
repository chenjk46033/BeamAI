#pragma once

#include <string>
#include <vector>

#include <Eigen/Core>

#include "mri/ras_transform.hpp"
#include "mri/slice.hpp"

// Automatic detection of Beam's six fiducial "donuts" in a loaded MRI.
//
// Not a port: BeamV0 has no equivalent -- its operator places all six by
// hand. The markers image as bright annuli with dark centres, 10-12mm
// across, three per transducer panel, so each sagittal slice is correlated
// against a bank of zero-mean annulus templates and the strongest
// responses are assigned to markers by the panel's known triangle.
//
// Nothing here moves a detection to fit the model: geometry only chooses
// among candidates. An earlier rigid-fit variant pulled accurate points
// off by ~0.4mm because the hardware triangle and the measured one differ
// (see docs/known_gaps_mri.md).

namespace beam::mri {

struct FiducialDetection {
    std::string name;
    Eigen::Vector3d positionMm = Eigen::Vector3d::Zero();
    double response = 0.0;   // peak zero-mean normalised cross-correlation
    double stability = 0.0;  // how far neighbouring slices agree on the centre
    double confidence = 0.0; // == response; reported separately so the two
                             // signals stay distinguishable to a caller
    double radiusMm = 0.0;
    bool found = false;
};

struct FiducialDetectOptions {
    // Margins added to the prior's own bounding box, per axis. The prior
    // predicts laterality well and elevation badly -- it sits ~54mm inferior
    // to where the markers actually are -- so the IS window is deliberately
    // lopsided. See docs/known_gaps_mri.md.
    double lrMarginMm = 8.0;
    double apMarginMm = 35.0;
    double isMarginInferiorMm = 20.0;
    double isMarginSuperiorMm = 80.0;

    double minRadiusMm = 3.5;
    double maxRadiusMm = 6.0;
    double radiusStepMm = 0.5;

    double responseFloor = 0.45;   // below this a local maximum is not a candidate
    double clusterRadiusMm = 7.0;  // merges responses from neighbouring slices
    int candidatesPerSide = 15;    // how many survive into triangle assignment
};

// priorMm: the six nominal marker positions, in millimetres, in the same
// per-index order setArrayFiducialMarkers produces (three right, three
// left). Only their geometry is used -- the absolute placement supplies
// the search window, the within-panel triangle supplies the assignment.
//
// axes must be the ascending-normalised vectors the viewer uses, so a
// detection's millimetres mean the same thing as a coordinate typed into
// the registration table.
//
// Throws std::invalid_argument unless priorMm has exactly 6 entries, or if
// the volume and axes disagree on dimensions.
std::vector<FiducialDetection> detectFiducialDonuts(
    const Volume3D& volume, const RasAxisVectors& axes,
    const std::vector<Eigen::Vector3d>& priorMm,
    const std::vector<std::string>& names,
    const FiducialDetectOptions& options = {});

}  // namespace beam::mri
