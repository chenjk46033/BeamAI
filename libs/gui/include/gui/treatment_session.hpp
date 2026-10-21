#pragma once

#include <array>
#include <string>
#include <vector>

#include "mri/ras_transform.hpp"
#include "mri/slice.hpp"
#include "registration/fiducial_markers.hpp"

// One subject's record as a single portable file, so a visit can resume
// without repeating the MRI load and the fiducial picking.
//
// BeamV0 does this with a 118 MB MATLAB 7.3 `.mat` holding all 38 fields of
// `sys` -- including derived volumes (focusImage, MNI, anatomy) stored as
// doubles. This carries the inputs only and deflates them, which is the same
// scan in about 6 MB. Everything derived is recomputed on load, so a session
// cannot disagree with the code that reads it.
//
// Identity is the Participant ID and Site, as BeamV0's CRF has it. Names and
// dates of birth stay out: this is the file designed to travel.

namespace beam::gui {

struct SessionVolume {
    bool present = false;
    beam::mri::Volume3D volume;
    beam::mri::RasAxisVectors axes;
};

// The four transducer lock sliders, 1..4 each.
struct SessionLockPosition {
    int leftHorizontal = 1;
    int leftVertical = 1;
    int rightHorizontal = 1;
    int rightVertical = 1;
};

struct TreatmentSessionFile {
    std::string participantId;
    std::string siteId;
    std::string mriPath;  // provenance; the image itself is in `mri`
    std::string createdAt;
    std::string appVersion;

    SessionVolume mri;
    std::vector<beam::registration::FiducialMarker> fiducials;  // metres
    std::array<bool, 6> fiducialLocated{};
    std::array<bool, 6> fiducialConfirmed{};
    SessionLockPosition lock;
    bool imagingAccepted = false;
    bool registrationAccepted = false;

    // Carried verbatim. These are formats the application already writes and
    // reads; re-parsing them here would be a second implementation to keep in
    // step with the first.
    std::string treatmentLogCsv;
    std::string visitStateCsv;
    std::string targetsCsv;
};

// Writes a deflated zip. Throws std::runtime_error if the path is unwritable
// or the archive cannot be completed.
void writeTreatmentSession(const std::string& path, const TreatmentSessionFile& session);

// Throws std::runtime_error on a missing file, a member that will not inflate,
// an unreadable manifest, or a version this build does not know. Nothing is
// returned half-read.
TreatmentSessionFile readTreatmentSession(const std::string& path);

// The version this build writes. A file claiming a higher one is refused
// rather than guessed at.
int treatmentSessionFormatVersion();

}  // namespace beam::gui
