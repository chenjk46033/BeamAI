#pragma once

#include <string>
#include <vector>

#include <Eigen/Core>

#include "mri/ras_transform.hpp"
#include "mri/slice.hpp"

// Automatic detection of the six fiducial donuts. Not a port; BeamV0 places
// them by hand. Sagittal slices are correlated against a bank of annulus
// templates, then the panel triangle picks among the responses.
//
// Geometry only selects candidates, never moves one: a rigid fit is worse
// than the raw detections (docs/known_gaps_mri.md).

namespace beam::mri {

struct FiducialDetection {
    std::string name;
    Eigen::Vector3d positionMm = Eigen::Vector3d::Zero();
    double response = 0.0;   // peak zero-mean normalised cross-correlation
    double stability = 0.0;  // how far neighbouring slices agree on the centre
    double confidence = 0.0;
    double radiusMm = 0.0;
    bool found = false;
};

struct FiducialDetectOptions {
    // Added to the prior's bounding box. IS is lopsided because the nominal
    // placement sits ~54mm inferior to the markers (docs/known_gaps_mri.md).
    // LR: nominal is off by 12mm on F017 and 0.4mm on F040; 16mm already
    // recovers both, so this carries headroom at no measurable cost.
    double lrMarginMm = 20.0;
    double apMarginMm = 35.0;
    double isMarginInferiorMm = 20.0;
    double isMarginSuperiorMm = 80.0;

    double minRadiusMm = 3.5;
    double maxRadiusMm = 6.0;
    double radiusStepMm = 0.5;

    double responseFloor = 0.45;
    double clusterRadiusMm = 7.0;
    int candidatesPerSide = 15;
};

// priorMm: six nominal marker positions in mm, in setArrayFiducialMarkers
// order. axes must be the ascending-normalised vectors the viewer uses.
// Throws std::invalid_argument on a wrong count or a volume/axes mismatch.
std::vector<FiducialDetection> detectFiducialDonuts(
    const Volume3D& volume, const RasAxisVectors& axes,
    const std::vector<Eigen::Vector3d>& priorMm,
    const std::vector<std::string>& names,
    const FiducialDetectOptions& options = {});

// One marker: every donut-like response within searchRadiusMm of aroundMm,
// strongest first, empty if none. No constellation constraint -- the caller
// has already said which marker this is.
//
// avoidMm holds the other markers' positions; a candidate nearer to one of
// those than to aroundMm is dropped, so a generous radius cannot snap onto
// the neighbouring marker ~20mm away.
std::vector<FiducialDetection> detectFiducialDonutsNear(
    const Volume3D& volume, const RasAxisVectors& axes, const Eigen::Vector3d& aroundMm,
    const std::vector<Eigen::Vector3d>& avoidMm, double searchRadiusMm, const std::string& name,
    const FiducialDetectOptions& options = {});

}  // namespace beam::mri
