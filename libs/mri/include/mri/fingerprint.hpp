#pragma once

#include <string>

#include "mri/ras_transform.hpp"
#include "mri/slice.hpp"

namespace beam::mri {

// Identifies the scan a visit was planned on, so a file found later can be
// shown to be the same image.
//
// Geometry alone is not enough: two patients scanned on one machine with one
// protocol share their dimensions exactly, so a geometry-only check would call
// them the same scan. The digest below samples the voxels as well, which is
// what makes this an identity check rather than an integrity check.
//
// Not a security hash. It answers "is this the same image", not "has anyone
// tampered with it".
std::string volumeFingerprint(const Volume3D& volume, const RasAxisVectors& axes);

// The geometry half on its own: voxel counts and axis bounds. Two scans can
// match here and still be different patients -- say so if you report it.
std::string geometryFingerprint(const RasAxisVectors& axes);

// True when both name the same image. Empty never matches, so an unrecorded
// scan is not silently taken as a match.
bool fingerprintsMatch(const std::string& a, const std::string& b);

}  // namespace beam::mri
