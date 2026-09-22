#pragma once

#include <Eigen/Core>

namespace beam::util {

// Port of BeamV0/GUIMatlab/BEAM/Util/vectorRange.m:
//   rangeVal = max(vec,[],'all') - min(vec,[],'all');
double vectorRange(const Eigen::VectorXd& vec);

// Port of BeamV0/GUIMatlab/BEAM/Util/distancePointToLine.m:
//   d = norm(cross((lineQ-P), lineV)) / norm(lineV);
// lineV: direction vector of the line. lineQ: a point on the line.
// point: the point to measure distance from.
double distancePointToLine(const Eigen::Vector3d& lineV, const Eigen::Vector3d& lineQ,
                            const Eigen::Vector3d& point);

// Port of BeamV0/GUIMatlab/BEAM/Util/addVectors.m:
//   v = reshape(u1,[],1) + reshape(u2,[],1);
// The MATLAB source reshapes both inputs to column vectors before adding so
// a row and a column of equal length still sum. Callers here pass Eigen
// column vectors, so Eigen's type system makes that reshape step
// unnecessary -- this is a plain elementwise sum. u1 and u2 must have the
// same length.
Eigen::VectorXd addVectors(const Eigen::VectorXd& u1, const Eigen::VectorXd& u2);

// Port of BeamV0/GUIMatlab/BEAM/Util/angleBetweenTwoVectors.m:
//   CosTheta = max(min(dot(u,v)/(norm(u)*norm(v)),1),-1);
//   ThetaInDegrees = real(acosd(CosTheta));
// Returns the angle between u and v in degrees. cos(theta) is clamped to
// [-1, 1] before acos (the MATLAB source's own guard against round-off
// pushing the ratio just outside that range).
double angleBetweenTwoVectors(const Eigen::Vector3d& u, const Eigen::Vector3d& v);

// Port of BeamV0/GUIMatlab/BEAM/Util/convertDelaysToCycles.m (byte-identical
// to BeamV0's Stimulation/convertDelaysToCycles.m -- one canonical
// implementation for both):
//   delaysCycles = delaysSeconds .* frequencyHz;
// frequencyHz is a single scalar (the transmit frequency), matching every
// real call site; the MATLAB `.*` would also broadcast against a same-length
// frequency vector, but no caller does that, so that generality is not
// carried over.
Eigen::VectorXd convertDelaysToCycles(const Eigen::VectorXd& delaysSeconds, double frequencyHz);

}  // namespace beam::util
