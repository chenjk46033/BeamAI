// Tests for the portable session file: one subject's record, plus the imaging
// and registration needed to resume, in a single deflated archive.

#include <chrono>
#include <fstream>
#include <iostream>
#include <string>

#include <gtest/gtest.h>

#include <miniz.h>

#include "gui/treatment_session.hpp"
#include "infra_dicom/load_mri_ras.hpp"
#include "mri/ras_transform.hpp"

namespace {

std::string scratch(const std::string& name) {
    return std::string(BEAM_TEST_SCRATCH) + "/" + name;
}

beam::gui::TreatmentSessionFile makeSession() {
    beam::gui::TreatmentSessionFile session;
    session.participantId = "p001";
    session.siteId = "site_100";
    session.mriPath = "C:/scans/F040.nii";
    session.createdAt = "2026-10-21T09:30:00";
    session.appVersion = "BeamAI test";
    session.lock = {2, 3, 4, 1};
    session.imagingAccepted = true;
    session.registrationAccepted = true;
    session.treatmentLogCsv = "Number,Name\n1,SCC1\n";
    session.visitStateCsv = "Protocol,Visit,CurrentSonication,Edited\n\"Default\",1,2,1\n";
    session.targetsCsv = "name,point\nSCC1,1\n";

    for (int i = 0; i < 6; ++i) {
        beam::registration::FiducialMarker marker;
        marker.name = "F" + std::to_string(i + 1);
        marker.position = Eigen::Vector3d(i * 1.5, -i * 2.5, i + 0.25) / 1000.0;
        session.fiducials.push_back(marker);
        session.fiducialLocated[static_cast<std::size_t>(i)] = true;
        session.fiducialConfirmed[static_cast<std::size_t>(i)] = i % 2 == 0;
    }
    return session;
}

// Small, but with distinct values in every voxel so a transposed or truncated
// read cannot pass.
beam::gui::SessionVolume makeVolume(int nx, int ny, int nz) {
    beam::gui::SessionVolume mri;
    mri.present = true;
    mri.volume.nx = nx;
    mri.volume.ny = ny;
    mri.volume.nz = nz;
    for (int k = 0; k < nz; ++k) {
        Eigen::MatrixXd slice(nx, ny);
        for (int i = 0; i < nx; ++i)
            for (int j = 0; j < ny; ++j) slice(i, j) = 100.0 * k + 10.0 * j + i;
        mri.volume.kSlices.push_back(slice);
    }
    mri.axes.dimLR = Eigen::VectorXd::LinSpaced(nx, -10.0, 10.0);
    mri.axes.dimAP = Eigen::VectorXd::LinSpaced(ny, -20.0, 5.0);
    mri.axes.dimIS = Eigen::VectorXd::LinSpaced(nz, -1.0, 30.0);
    return mri;
}

}  // namespace

TEST(TreatmentSession, RoundTripsEverythingItCarries) {
    const std::string path = scratch("session_round_trip.beamsession");
    const beam::gui::TreatmentSessionFile written = makeSession();
    ASSERT_NO_THROW(beam::gui::writeTreatmentSession(path, written));

    beam::gui::TreatmentSessionFile read;
    ASSERT_NO_THROW(read = beam::gui::readTreatmentSession(path));

    EXPECT_EQ(read.participantId, "p001");
    EXPECT_EQ(read.siteId, "site_100");
    EXPECT_EQ(read.mriPath, "C:/scans/F040.nii");
    EXPECT_EQ(read.createdAt, "2026-10-21T09:30:00");
    EXPECT_EQ(read.lock.leftHorizontal, 2);
    EXPECT_EQ(read.lock.leftVertical, 3);
    EXPECT_EQ(read.lock.rightHorizontal, 4);
    EXPECT_EQ(read.lock.rightVertical, 1);
    EXPECT_TRUE(read.imagingAccepted);
    EXPECT_TRUE(read.registrationAccepted);
    EXPECT_EQ(read.treatmentLogCsv, written.treatmentLogCsv);
    EXPECT_EQ(read.visitStateCsv, written.visitStateCsv);
    EXPECT_EQ(read.targetsCsv, written.targetsCsv);

    ASSERT_EQ(read.fiducials.size(), 6u);
    for (std::size_t i = 0; i < 6; ++i) {
        EXPECT_EQ(read.fiducials[i].name, written.fiducials[i].name);
        EXPECT_NEAR(read.fiducials[i].position.x(), written.fiducials[i].position.x(), 1e-12);
        EXPECT_NEAR(read.fiducials[i].position.y(), written.fiducials[i].position.y(), 1e-12);
        EXPECT_NEAR(read.fiducials[i].position.z(), written.fiducials[i].position.z(), 1e-12);
        EXPECT_EQ(read.fiducialLocated[i], written.fiducialLocated[i]);
        EXPECT_EQ(read.fiducialConfirmed[i], written.fiducialConfirmed[i]);
    }
}

// The image is the one thing a session carries that nothing else on the device
// holds, so a voxel landing in the wrong place has to fail here.
TEST(TreatmentSession, TheVolumeSurvivesVoxelForVoxel) {
    const std::string path = scratch("session_volume.beamsession");
    beam::gui::TreatmentSessionFile written = makeSession();
    written.mri = makeVolume(5, 4, 3);
    ASSERT_NO_THROW(beam::gui::writeTreatmentSession(path, written));

    const beam::gui::TreatmentSessionFile read = beam::gui::readTreatmentSession(path);
    ASSERT_TRUE(read.mri.present);
    EXPECT_EQ(read.mri.volume.nx, 5);
    EXPECT_EQ(read.mri.volume.ny, 4);
    EXPECT_EQ(read.mri.volume.nz, 3);
    for (int k = 0; k < 3; ++k)
        for (int i = 0; i < 5; ++i)
            for (int j = 0; j < 4; ++j)
                EXPECT_DOUBLE_EQ(read.mri.volume(i, j, k), 100.0 * k + 10.0 * j + i)
                    << "at " << i << "," << j << "," << k;

    ASSERT_EQ(read.mri.axes.dimLR.size(), 5);
    ASSERT_EQ(read.mri.axes.dimAP.size(), 4);
    ASSERT_EQ(read.mri.axes.dimIS.size(), 3);
    EXPECT_DOUBLE_EQ(read.mri.axes.dimLR(0), -10.0);
    EXPECT_DOUBLE_EQ(read.mri.axes.dimLR(4), 10.0);
    EXPECT_DOUBLE_EQ(read.mri.axes.dimAP(3), 5.0);
    EXPECT_DOUBLE_EQ(read.mri.axes.dimIS(2), 30.0);
}

// A session saved before an MRI was loaded is legitimate -- the registration
// half simply is not there yet.
TEST(TreatmentSession, AnAbsentVolumeIsNotAnError) {
    const std::string path = scratch("session_no_volume.beamsession");
    beam::gui::TreatmentSessionFile written = makeSession();
    written.mri.present = false;
    ASSERT_NO_THROW(beam::gui::writeTreatmentSession(path, written));

    const beam::gui::TreatmentSessionFile read = beam::gui::readTreatmentSession(path);
    EXPECT_FALSE(read.mri.present);
    EXPECT_EQ(read.participantId, "p001");
}

TEST(TreatmentSession, RefusesAFileThatIsNotASession) {
    const std::string path = scratch("session_not_a_zip.beamsession");
    std::ofstream(path, std::ios::binary) << "this is not an archive";
    EXPECT_THROW(beam::gui::readTreatmentSession(path), std::runtime_error);
    EXPECT_THROW(beam::gui::readTreatmentSession(scratch("session_absent.beamsession")),
                 std::runtime_error);
}

// Guessing at a format a later build wrote is how a session half-loads, so
// the archive here is forged directly rather than through the writer.
TEST(TreatmentSession, RefusesANewerFormat) {
    const std::string path = scratch("session_future.beamsession");
    const std::string manifest =
        "key,value\nformatVersion," +
        std::to_string(beam::gui::treatmentSessionFormatVersion() + 1) +
        "\nparticipantId,p001\n";
    mz_zip_archive zip{};
    ASSERT_TRUE(mz_zip_writer_init_file(&zip, path.c_str(), 0));
    ASSERT_TRUE(mz_zip_writer_add_mem(&zip, "manifest.csv", manifest.data(), manifest.size(), 1));
    ASSERT_TRUE(mz_zip_writer_finalize_archive(&zip));
    mz_zip_writer_end(&zip);

    EXPECT_THROW(beam::gui::readTreatmentSession(path), std::runtime_error);
}

// A session whose volume member is truncated must not load a partial image.
TEST(TreatmentSession, RefusesATruncatedVolume) {
    const std::string path = scratch("session_torn.beamsession");
    const std::string manifest = "key,value\nformatVersion,1\nparticipantId,p001\n";
    const std::string stub = std::string("BEAMVOL1") + std::string(8, char(0));
    mz_zip_archive zip{};
    ASSERT_TRUE(mz_zip_writer_init_file(&zip, path.c_str(), 0));
    ASSERT_TRUE(mz_zip_writer_add_mem(&zip, "manifest.csv", manifest.data(), manifest.size(), 1));
    ASSERT_TRUE(mz_zip_writer_add_mem(&zip, "mri.bin", stub.data(), stub.size(), 1));
    ASSERT_TRUE(mz_zip_writer_finalize_archive(&zip));
    mz_zip_writer_end(&zip);

    EXPECT_THROW(beam::gui::readTreatmentSession(path), std::runtime_error);
}

TEST(TreatmentSession, RefusesASessionThatNamesNobody) {
    const std::string path = scratch("session_anonymous.beamsession");
    beam::gui::TreatmentSessionFile written = makeSession();
    written.participantId.clear();
    ASSERT_NO_THROW(beam::gui::writeTreatmentSession(path, written));
    EXPECT_THROW(beam::gui::readTreatmentSession(path), std::runtime_error);
}

// An identifier an operator typed can hold the separator, and a Windows path
// holds backslashes; neither may break the manifest.
TEST(TreatmentSession, AwkwardTextSurvivesTheManifest) {
    const std::string path = scratch("session_awkward.beamsession");
    beam::gui::TreatmentSessionFile written = makeSession();
    written.participantId = "Smith, John";
    written.siteId = "site \"A\"";
    written.mriPath = "C:\\scans\\F040, copy.nii";
    ASSERT_NO_THROW(beam::gui::writeTreatmentSession(path, written));

    const beam::gui::TreatmentSessionFile read = beam::gui::readTreatmentSession(path);
    EXPECT_EQ(read.participantId, "Smith, John");
    EXPECT_EQ(read.siteId, "site \"A\"");
    EXPECT_EQ(read.mriPath, "C:\\scans\\F040, copy.nii");
}

// The archive has to be a real zip, or none of the tooling an operator has
// will open it when something goes wrong.
TEST(TreatmentSession, WritesARecognisableZip) {
    const std::string path = scratch("session_magic.beamsession");
    ASSERT_NO_THROW(beam::gui::writeTreatmentSession(path, makeSession()));
    std::ifstream file(path, std::ios::binary);
    char magic[4] = {};
    file.read(magic, 4);
    EXPECT_EQ(magic[0], 'P');
    EXPECT_EQ(magic[1], 'K');
}

// Not part of the suite: it writes a 192^3 session and reports what that
// costs. Run with --gtest_also_run_disabled_tests when the number matters.
TEST(TreatmentSession, DISABLED_RealScanSizeAndTime) {
    const beam::mri::MriVolumeRas ras = beam::infra::dicom::loadMriRas(BEAM_MRI_FIXTURE_NII);
    beam::gui::TreatmentSessionFile session = makeSession();
    session.mri.present = true;
    session.mri.volume = beam::mri::reorientedVolumeToVolume3D(ras.volume);
    session.mri.axes = ras.axes;

    const std::string path = scratch("session_real_scan.beamsession");
    const auto start = std::chrono::steady_clock::now();
    ASSERT_NO_THROW(beam::gui::writeTreatmentSession(path, session));
    const auto written = std::chrono::steady_clock::now();
    const beam::gui::TreatmentSessionFile back = beam::gui::readTreatmentSession(path);
    const auto readBack = std::chrono::steady_clock::now();

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto bytes = static_cast<double>(file.tellg());
    const auto ms = [](auto a, auto b) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count();
    };
    std::cout << "\n  volume      " << session.mri.volume.nx << "x" << session.mri.volume.ny
              << "x" << session.mri.volume.nz
              << "\n  session     " << (bytes / 1048576.0) << " MB"
              << "\n  write       " << ms(start, written) << " ms"
              << "\n  read        " << ms(written, readBack) << " ms\n";
    ASSERT_TRUE(back.mri.present);
    EXPECT_EQ(back.mri.volume.nx, session.mri.volume.nx);
    EXPECT_EQ(back.mri.volume.nz, session.mri.volume.nz);
}
