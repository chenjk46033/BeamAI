#pragma once

#include <string>
#include <vector>

#include "mri/ras_transform.hpp"
#include "mri/slice.hpp"

namespace beam::infra::mat {

// Read-only import of the MRI portion of a BeamV0/Diadem MATLAB session.
// The source file is never modified. Other session state is deliberately
// ignored until each field has a reviewed workflow mapping.
struct LegacyBeamMri {
    struct Fiducial { std::string name; Eigen::Vector3d positionMm; };
    beam::mri::Volume3D volume;
    beam::mri::RasAxisVectors axes;
    std::vector<Fiducial> fiducials;
};

LegacyBeamMri loadLegacyBeamMri(const std::string& path);

}  // namespace beam::infra::mat
