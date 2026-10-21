#include "gui/treatment_session.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <map>
#include <stdexcept>

#include <miniz.h>

namespace beam::gui {
namespace {

constexpr int kFormatVersion = 1;
constexpr const char* kManifest = "manifest.csv";
constexpr const char* kVolume = "mri.bin";
constexpr const char* kFiducials = "fiducials.csv";
constexpr const char* kLog = "treatment_log.csv";
constexpr const char* kVisitState = "treatment_visit_state.csv";
constexpr const char* kTargets = "treatment_targets.csv";

// "BEAMVOL1" then nx, ny, nz, then the three axis vectors as float64, then
// the voxels as float32 -- the scanner's own data is 16-bit, so float32 holds
// it exactly at half the size of the doubles it is kept in once loaded.
constexpr const char* kVolumeMagic = "BEAMVOL1";

std::string trimmed(std::string text) {
    const auto notSpace = [](unsigned char c) { return std::isspace(c) == 0; };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), notSpace));
    text.erase(std::find_if(text.rbegin(), text.rend(), notSpace).base(), text.end());
    return text;
}

std::string quote(const std::string& text) {
    if (text.find(',') == std::string::npos && text.find('"') == std::string::npos) return text;
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"') out += '"';
        out += c;
    }
    return out + "\"";
}

std::vector<std::string> splitRow(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '"') {
            if (quoted && i + 1 < line.size() && line[i + 1] == '"') {
                field += '"';
                ++i;
                continue;
            }
            quoted = !quoted;
        } else if (c == ',' && !quoted) {
            fields.push_back(trimmed(field));
            field.clear();
        } else {
            field += c;
        }
    }
    fields.push_back(trimmed(field));
    return fields;
}

std::string currentTimestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char buffer[32] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &local);
    return buffer;
}

template <typename T>
void put(std::string& out, const T& value) {
    const char* bytes = reinterpret_cast<const char*>(&value);
    out.append(bytes, sizeof(T));
}

template <typename T>
T take(const std::string& in, std::size_t& at, const std::string& path) {
    if (at + sizeof(T) > in.size())
        throw std::runtime_error(path + ": " + kVolume + " ends mid-value");
    T value{};
    std::memcpy(&value, in.data() + at, sizeof(T));
    at += sizeof(T);
    return value;
}

std::string encodeVolume(const SessionVolume& mri) {
    std::string out;
    const auto nx = static_cast<std::int64_t>(mri.volume.nx);
    const auto ny = static_cast<std::int64_t>(mri.volume.ny);
    const auto nz = static_cast<std::int64_t>(mri.volume.nz);
    out.reserve(static_cast<std::size_t>(nx * ny * nz) * sizeof(float) + 1024);
    out.append(kVolumeMagic, 8);
    put(out, nx);
    put(out, ny);
    put(out, nz);
    const auto axis = [&out](const Eigen::VectorXd& values) {
        put(out, static_cast<std::int64_t>(values.size()));
        for (Eigen::Index i = 0; i < values.size(); ++i) put(out, values(i));
    };
    axis(mri.axes.dimLR);
    axis(mri.axes.dimAP);
    axis(mri.axes.dimIS);
    for (std::int64_t k = 0; k < nz; ++k) {
        const Eigen::MatrixXd& slice = mri.volume.kSlices[static_cast<std::size_t>(k)];
        for (Eigen::Index j = 0; j < slice.cols(); ++j)
            for (Eigen::Index i = 0; i < slice.rows(); ++i)
                put(out, static_cast<float>(slice(i, j)));
    }
    return out;
}

SessionVolume decodeVolume(const std::string& in, const std::string& path) {
    SessionVolume mri;
    if (in.size() < 8 || std::memcmp(in.data(), kVolumeMagic, 8) != 0)
        throw std::runtime_error(path + ": " + kVolume + " is not a Beam volume");
    std::size_t at = 8;
    const auto nx = take<std::int64_t>(in, at, path);
    const auto ny = take<std::int64_t>(in, at, path);
    const auto nz = take<std::int64_t>(in, at, path);
    if (nx < 1 || ny < 1 || nz < 1 || nx > 4096 || ny > 4096 || nz > 4096)
        throw std::runtime_error(path + ": " + kVolume + " has implausible dimensions");

    const auto axis = [&](Eigen::VectorXd& values) {
        const auto n = take<std::int64_t>(in, at, path);
        if (n < 0 || n > 4096)
            throw std::runtime_error(path + ": " + kVolume + " has an implausible axis");
        values.resize(n);
        for (std::int64_t i = 0; i < n; ++i) values(i) = take<double>(in, at, path);
    };
    axis(mri.axes.dimLR);
    axis(mri.axes.dimAP);
    axis(mri.axes.dimIS);
    if (mri.axes.dimLR.size() != nx || mri.axes.dimAP.size() != ny || mri.axes.dimIS.size() != nz)
        throw std::runtime_error(path + ": " + kVolume + " axes do not match its dimensions");

    mri.volume.nx = nx;
    mri.volume.ny = ny;
    mri.volume.nz = nz;
    mri.volume.kSlices.assign(static_cast<std::size_t>(nz), Eigen::MatrixXd::Zero(nx, ny));
    for (std::int64_t k = 0; k < nz; ++k) {
        Eigen::MatrixXd& slice = mri.volume.kSlices[static_cast<std::size_t>(k)];
        for (Eigen::Index j = 0; j < ny; ++j)
            for (Eigen::Index i = 0; i < nx; ++i) slice(i, j) = take<float>(in, at, path);
    }
    mri.present = true;
    return mri;
}

std::string encodeFiducials(const TreatmentSessionFile& session) {
    std::string out = "# BeamAI session fiducials, millimetres, RAS\nname,x,y,z,located,confirmed,confidence,stability\n";
    for (std::size_t i = 0; i < session.fiducials.size(); ++i) {
        const Eigen::Vector3d mm = session.fiducials[i].position * 1000.0;
        char row[256] = {};
        std::snprintf(row, sizeof(row), "%s,%.17g,%.17g,%.17g,%d,%d,%.17g,%.17g\n",
                      quote(session.fiducials[i].name).c_str(), mm.x(), mm.y(), mm.z(),
                      i < session.fiducialLocated.size() && session.fiducialLocated[i] ? 1 : 0,
                      i < session.fiducialConfirmed.size() && session.fiducialConfirmed[i] ? 1 : 0,
                      i < session.fiducialConfidence.size() ? session.fiducialConfidence[i] : 0.0,
                      i < session.fiducialStability.size() ? session.fiducialStability[i] : 0.0);
        out += row;
    }
    return out;
}

// A detection score, or 0 if the column is unreadable.
double score(const std::string& text) {
    try {
        return std::stod(text);
    } catch (const std::exception&) {
        return 0.0;
    }
}

void decodeFiducials(const std::string& text, TreatmentSessionFile& session,
                     const std::string& path) {
    session.fiducials.clear();
    session.fiducialLocated.fill(false);
    session.fiducialConfirmed.fill(false);
    session.fiducialConfidence.fill(0.0);
    session.fiducialStability.fill(0.0);
    std::size_t at = 0;
    bool headerSeen = false;
    while (at < text.size()) {
        const std::size_t end = text.find('\n', at);
        std::string line = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
        at = end == std::string::npos ? text.size() : end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string stripped = trimmed(line);
        if (stripped.empty() || stripped.front() == '#') continue;
        if (!headerSeen) {
            headerSeen = true;
            continue;
        }
        const std::vector<std::string> fields = splitRow(stripped);
        if (fields.size() < 4)
            throw std::runtime_error(path + ": a fiducial row has " +
                                     std::to_string(fields.size()) + " fields, expected 6");
        beam::registration::FiducialMarker marker;
        marker.name = fields[0];
        try {
            marker.position = Eigen::Vector3d(std::stod(fields[1]), std::stod(fields[2]),
                                              std::stod(fields[3])) /
                              1000.0;
        } catch (const std::exception&) {
            throw std::runtime_error(path + ": a fiducial row has a non-numeric coordinate");
        }
        const std::size_t index = session.fiducials.size();
        if (index < session.fiducialLocated.size()) {
            session.fiducialLocated[index] = fields.size() > 4 && fields[4] == "1";
            session.fiducialConfirmed[index] = fields.size() > 5 && fields[5] == "1";
            // Absent in a file written before these columns existed.
            if (fields.size() > 6) session.fiducialConfidence[index] = score(fields[6]);
            if (fields.size() > 7) session.fiducialStability[index] = score(fields[7]);
        }
        session.fiducials.push_back(std::move(marker));
    }
}

std::string encodeManifest(const TreatmentSessionFile& session) {
    std::string out =
        "# BeamAI treatment session. One subject, portable. Derived volumes are\n"
        "# recomputed on load and are deliberately not stored.\n"
        "key,value\n";
    const auto row = [&out](const char* key, const std::string& value) {
        out += key;
        out += ',';
        out += quote(value);
        out += '\n';
    };
    row("formatVersion", std::to_string(kFormatVersion));
    row("participantId", session.participantId);
    row("siteId", session.siteId);
    row("mriPath", session.mriPath);
    row("createdAt", session.createdAt.empty() ? currentTimestamp() : session.createdAt);
    row("appVersion", session.appVersion);
    row("lockLeftHorizontal", std::to_string(session.lock.leftHorizontal));
    row("lockLeftVertical", std::to_string(session.lock.leftVertical));
    row("lockRightHorizontal", std::to_string(session.lock.rightHorizontal));
    row("lockRightVertical", std::to_string(session.lock.rightVertical));
    row("imagingAccepted", session.imagingAccepted ? "1" : "0");
    row("registrationAccepted", session.registrationAccepted ? "1" : "0");
    return out;
}

std::map<std::string, std::string> decodeManifest(const std::string& text,
                                                  const std::string& path) {
    std::map<std::string, std::string> values;
    std::size_t at = 0;
    bool headerSeen = false;
    while (at < text.size()) {
        const std::size_t end = text.find('\n', at);
        std::string line = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
        at = end == std::string::npos ? text.size() : end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string stripped = trimmed(line);
        if (stripped.empty() || stripped.front() == '#') continue;
        if (!headerSeen) {
            headerSeen = true;
            continue;
        }
        const std::vector<std::string> fields = splitRow(stripped);
        if (fields.size() != 2)
            throw std::runtime_error(path + ": a manifest row has " +
                                     std::to_string(fields.size()) + " fields, expected 2");
        values[fields[0]] = fields[1];
    }
    if (values.count("formatVersion") == 0)
        throw std::runtime_error(path + ": no formatVersion; not a Beam session");
    return values;
}

int intOr(const std::map<std::string, std::string>& values, const char* key, int fallback) {
    const auto found = values.find(key);
    if (found == values.end() || found->second.empty()) return fallback;
    try {
        return std::stoi(found->second);
    } catch (const std::exception&) {
        return fallback;
    }
}

std::string textOr(const std::map<std::string, std::string>& values, const char* key) {
    const auto found = values.find(key);
    return found == values.end() ? std::string() : found->second;
}

void addMember(mz_zip_archive& zip, const char* name, const std::string& bytes,
               const std::string& path) {
    // Level 1: deflate at 6 costs six times as long for 7% off an MRI.
    if (!mz_zip_writer_add_mem(&zip, name, bytes.data(), bytes.size(), 1))
        throw std::runtime_error(path + ": could not add " + name);
}

std::string readMember(mz_zip_archive& zip, const char* name, const std::string& path,
                       bool required) {
    const int index = mz_zip_reader_locate_file(&zip, name, nullptr, 0);
    if (index < 0) {
        if (!required) return {};
        throw std::runtime_error(path + ": " + name + " is missing");
    }
    std::size_t size = 0;
    void* raw = mz_zip_reader_extract_to_heap(&zip, static_cast<mz_uint>(index), &size, 0);
    if (raw == nullptr) throw std::runtime_error(path + ": " + name + " could not be read");
    std::string out(static_cast<const char*>(raw), size);
    mz_free(raw);
    return out;
}

}  // namespace

int treatmentSessionFormatVersion() { return kFormatVersion; }

void writeTreatmentSession(const std::string& path, const TreatmentSessionFile& session) {
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_file(&zip, path.c_str(), 0))
        throw std::runtime_error("cannot write " + path);
    try {
        addMember(zip, kManifest, encodeManifest(session), path);
        addMember(zip, kFiducials, encodeFiducials(session), path);
        addMember(zip, kLog, session.treatmentLogCsv, path);
        addMember(zip, kVisitState, session.visitStateCsv, path);
        addMember(zip, kTargets, session.targetsCsv, path);
        if (session.mri.present) addMember(zip, kVolume, encodeVolume(session.mri), path);
    } catch (...) {
        mz_zip_writer_end(&zip);
        throw;
    }
    const bool finalised = mz_zip_writer_finalize_archive(&zip) != 0;
    mz_zip_writer_end(&zip);
    if (!finalised) throw std::runtime_error("could not finish writing " + path);
}

TreatmentSessionFile readTreatmentSession(const std::string& path) {
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, path.c_str(), 0))
        throw std::runtime_error(path + ": not readable as a Beam session");

    TreatmentSessionFile session;
    try {
        const std::map<std::string, std::string> manifest =
            decodeManifest(readMember(zip, kManifest, path, true), path);
        const int version = intOr(manifest, "formatVersion", 0);
        if (version > kFormatVersion) {
            throw std::runtime_error(path + ": written by a newer BeamAI (session format " +
                                     std::to_string(version) + ", this build reads " +
                                     std::to_string(kFormatVersion) + ")");
        }
        session.participantId = textOr(manifest, "participantId");
        session.siteId = textOr(manifest, "siteId");
        session.mriPath = textOr(manifest, "mriPath");
        session.createdAt = textOr(manifest, "createdAt");
        session.appVersion = textOr(manifest, "appVersion");
        session.lock.leftHorizontal = intOr(manifest, "lockLeftHorizontal", 1);
        session.lock.leftVertical = intOr(manifest, "lockLeftVertical", 1);
        session.lock.rightHorizontal = intOr(manifest, "lockRightHorizontal", 1);
        session.lock.rightVertical = intOr(manifest, "lockRightVertical", 1);
        session.imagingAccepted = textOr(manifest, "imagingAccepted") == "1";
        session.registrationAccepted = textOr(manifest, "registrationAccepted") == "1";
        if (session.participantId.empty())
            throw std::runtime_error(path + ": the session names no participant");

        decodeFiducials(readMember(zip, kFiducials, path, false), session, path);
        session.treatmentLogCsv = readMember(zip, kLog, path, false);
        session.visitStateCsv = readMember(zip, kVisitState, path, false);
        session.targetsCsv = readMember(zip, kTargets, path, false);
        const std::string volume = readMember(zip, kVolume, path, false);
        if (!volume.empty()) session.mri = decodeVolume(volume, path);
    } catch (...) {
        mz_zip_reader_end(&zip);
        throw;
    }
    mz_zip_reader_end(&zip);
    return session;
}

}  // namespace beam::gui
