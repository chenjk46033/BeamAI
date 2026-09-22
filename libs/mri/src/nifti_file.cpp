#include "mri/nifti_file.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>

// Copied from Diadem's libs/imaging/src/nifti_file.cpp (diadem::imaging).

namespace beam::mri {

namespace {

// Byte offsets into the fixed 348-byte NIfTI-1 header, per the published
// spec (unchanged since 2004). Read via memcpy at these explicit offsets
// rather than a packed C struct, to sidestep any compiler-padding
// mismatch with the on-disk layout.
constexpr int kOffSizeofHdr = 0;
constexpr int kOffDim = 40;         // int16[8]
constexpr int kOffDatatype = 70;    // int16
constexpr int kOffBitpix = 72;      // int16
constexpr int kOffPixdim = 76;      // float32[8]
constexpr int kOffVoxOffset = 108;  // float32
constexpr int kOffSclSlope = 112;   // float32
constexpr int kOffSclInter = 116;   // float32
constexpr int kOffQformCode = 252;  // int16
constexpr int kOffSformCode = 254;  // int16
constexpr int kOffQuaternB = 256;   // float32
constexpr int kOffQuaternC = 260;
constexpr int kOffQuaternD = 264;
constexpr int kOffQoffsetX = 268;
constexpr int kOffQoffsetY = 272;
constexpr int kOffQoffsetZ = 276;
constexpr int kOffSrowX = 280;  // float32[4]
constexpr int kOffSrowY = 296;
constexpr int kOffSrowZ = 312;
constexpr int kOffMagic = 344;  // char[4]
constexpr int kHeaderSize = 348;

int16_t readI16(const char* buf, int offset) {
    int16_t v;
    std::memcpy(&v, buf + offset, sizeof(v));
    return v;
}
int32_t readI32(const char* buf, int offset) {
    int32_t v;
    std::memcpy(&v, buf + offset, sizeof(v));
    return v;
}
float readF32(const char* buf, int offset) {
    float v;
    std::memcpy(&v, buf + offset, sizeof(v));
    return v;
}

// Reads a 348-byte NIfTI-1 header buffer into a structured header,
// rejecting anything that isn't a little-endian single-file NIfTI-1.
NiftiHeader parseHeader(const char* buf) {
    const int32_t sizeofHdr = readI32(buf, kOffSizeofHdr);
    if (sizeofHdr != kHeaderSize) {
        throw std::runtime_error(
            "readNiftiHeader: sizeof_hdr != 348 -- not a little-endian "
            "NIfTI-1 file (big-endian files are not supported)");
    }
    char magic[5] = {0};
    std::memcpy(magic, buf + kOffMagic, 4);
    if (std::strncmp(magic, "n+1", 3) != 0) {
        throw std::runtime_error(
            std::string("readNiftiHeader: unsupported magic '") + magic +
            "' -- only single-file \"n+1\" NIfTI-1 is supported, not "
            "two-file ni1/Analyze pairs");
    }

    NiftiHeader h;
    for (int i = 0; i < 8; ++i) h.dim[i] = readI16(buf, kOffDim + 2 * i);
    for (int i = 0; i < 8; ++i) h.pixdim[i] = readF32(buf, kOffPixdim + 4 * i);

    h.datatype = readI16(buf, kOffDatatype);
    h.bitpix = readI16(buf, kOffBitpix);
    h.voxOffset = readF32(buf, kOffVoxOffset);
    h.sclSlope = readF32(buf, kOffSclSlope);
    h.sclInter = readF32(buf, kOffSclInter);

    h.qformCode = readI16(buf, kOffQformCode);
    h.sformCode = readI16(buf, kOffSformCode);
    h.quaternB = readF32(buf, kOffQuaternB);
    h.quaternC = readF32(buf, kOffQuaternC);
    h.quaternD = readF32(buf, kOffQuaternD);
    h.qoffsetX = readF32(buf, kOffQoffsetX);
    h.qoffsetY = readF32(buf, kOffQoffsetY);
    h.qoffsetZ = readF32(buf, kOffQoffsetZ);

    for (int i = 0; i < 4; ++i) h.srowX(i) = readF32(buf, kOffSrowX + 4 * i);
    for (int i = 0; i < 4; ++i) h.srowY(i) = readF32(buf, kOffSrowY + 4 * i);
    for (int i = 0; i < 4; ++i) h.srowZ(i) = readF32(buf, kOffSrowZ + 4 * i);

    return h;
}

// NIfTI DT_* codes this reader supports, and their byte width.
int bytesPerVoxel(int datatype) {
    switch (datatype) {
        case 2: return 1;    // DT_UINT8
        case 256: return 1;  // DT_INT8
        case 4: return 2;    // DT_INT16
        case 512: return 2;  // DT_UINT16
        case 8: return 4;    // DT_INT32
        case 768: return 4;  // DT_UINT32
        case 16: return 4;   // DT_FLOAT32
        case 64: return 8;   // DT_FLOAT64
        default:
            throw std::runtime_error(
                "readNiftiVolumeScaled: unsupported NIfTI datatype code " +
                std::to_string(datatype));
    }
}

// Reads one voxel's raw bytes as a double, whatever its on-disk datatype.
double readVoxelAsDouble(const char* buf, int datatype) {
    switch (datatype) {
        case 2: return static_cast<double>(static_cast<uint8_t>(buf[0]));
        case 256: return static_cast<double>(static_cast<int8_t>(buf[0]));
        case 4: return static_cast<double>(readI16(buf, 0));
        case 512: {
            uint16_t v;
            std::memcpy(&v, buf, sizeof(v));
            return static_cast<double>(v);
        }
        case 8: return static_cast<double>(readI32(buf, 0));
        case 768: {
            uint32_t v;
            std::memcpy(&v, buf, sizeof(v));
            return static_cast<double>(v);
        }
        case 16: return static_cast<double>(readF32(buf, 0));
        case 64: {
            double v;
            std::memcpy(&v, buf, sizeof(v));
            return v;
        }
        default:
            throw std::runtime_error(
                "readVoxelAsDouble: unsupported NIfTI datatype code " +
                std::to_string(datatype));
    }
}

}  // namespace

// Reads just the header of a .nii file on disk, without its voxel data.
NiftiHeader readNiftiHeader(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("readNiftiHeader: could not open " + path);

    char buf[kHeaderSize];
    in.read(buf, kHeaderSize);
    if (in.gcount() != kHeaderSize) {
        throw std::runtime_error("readNiftiHeader: file shorter than a NIfTI-1 header: " + path);
    }
    return parseHeader(buf);
}

// Reads a .nii file's header and full voxel data, applying the file's own
// scale/intercept so the returned values are real-world units.
NiftiVolume readNiftiVolumeScaled(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("readNiftiVolumeScaled: could not open " + path);

    char headerBuf[kHeaderSize];
    in.read(headerBuf, kHeaderSize);
    if (in.gcount() != kHeaderSize) {
        throw std::runtime_error("readNiftiVolumeScaled: file shorter than a NIfTI-1 header: " + path);
    }

    NiftiVolume result;
    result.header = parseHeader(headerBuf);
    const NiftiHeader& h = result.header;

    const int ndims = h.dim[0] > 0 ? h.dim[0] : 3;
    long nVoxels = 1;
    for (int i = 1; i <= ndims; ++i) nVoxels *= h.dim[i];

    const int voxelBytes = bytesPerVoxel(h.datatype);

    in.seekg(static_cast<std::streamoff>(h.voxOffset));
    if (!in) throw std::runtime_error("readNiftiVolumeScaled: could not seek to vox_offset in " + path);

    std::vector<char> raw(static_cast<std::size_t>(nVoxels) * voxelBytes);
    in.read(raw.data(), static_cast<std::streamsize>(raw.size()));
    if (in.gcount() != static_cast<std::streamsize>(raw.size())) {
        throw std::runtime_error("readNiftiVolumeScaled: file shorter than dim[]*bitpix expects: " + path);
    }

    // scl_slope == 0 means "do not scale" per the NIfTI-1 spec -- not a
    // literal multiply-by-zero.
    const bool applyScaling = (h.sclSlope != 0.0);

    result.voxels.resize(nVoxels);
    for (long i = 0; i < nVoxels; ++i) {
        const double raw_i = readVoxelAsDouble(raw.data() + i * voxelBytes, h.datatype);
        result.voxels[i] = applyScaling ? (raw_i * h.sclSlope + h.sclInter) : raw_i;
    }

    return result;
}

namespace {

void writeI16(std::vector<char>& buf, int offset, int16_t v) { std::memcpy(buf.data() + offset, &v, 2); }
void writeI32(std::vector<char>& buf, int offset, int32_t v) { std::memcpy(buf.data() + offset, &v, 4); }
void writeF32(std::vector<char>& buf, int offset, float v) { std::memcpy(buf.data() + offset, &v, 4); }

// Appends one double, cast to the on-disk `datatype`, to `out`.
void appendVoxel(std::vector<char>& out, double value, int datatype) {
    switch (datatype) {
        case 2: { const auto v = static_cast<uint8_t>(value); out.push_back(static_cast<char>(v)); break; }
        case 256: { const auto v = static_cast<int8_t>(value); out.push_back(static_cast<char>(v)); break; }
        case 4: { const auto v = static_cast<int16_t>(value); const char* p = reinterpret_cast<const char*>(&v); out.insert(out.end(), p, p + 2); break; }
        case 512: { const auto v = static_cast<uint16_t>(value); const char* p = reinterpret_cast<const char*>(&v); out.insert(out.end(), p, p + 2); break; }
        case 8: { const auto v = static_cast<int32_t>(value); const char* p = reinterpret_cast<const char*>(&v); out.insert(out.end(), p, p + 4); break; }
        case 768: { const auto v = static_cast<uint32_t>(value); const char* p = reinterpret_cast<const char*>(&v); out.insert(out.end(), p, p + 4); break; }
        case 16: { const auto v = static_cast<float>(value); const char* p = reinterpret_cast<const char*>(&v); out.insert(out.end(), p, p + 4); break; }
        case 64: { const char* p = reinterpret_cast<const char*>(&value); out.insert(out.end(), p, p + 8); break; }
        default:
            throw std::runtime_error("writeNiftiVolume: unsupported NIfTI datatype code " +
                                      std::to_string(datatype));
    }
}

}  // namespace

void writeNiftiVolume(const std::string& path, const NiftiHeader& header,
                       const std::vector<double>& voxels) {
    constexpr int kHeaderSize = 348;
    constexpr float kVoxOffset = 352.0f;

    std::vector<char> buf(kHeaderSize, 0);
    writeI32(buf, kOffSizeofHdr, kHeaderSize);
    for (int i = 0; i < 8; ++i) writeI16(buf, kOffDim + 2 * i, static_cast<int16_t>(header.dim[i]));
    writeI16(buf, kOffDatatype, static_cast<int16_t>(header.datatype));
    writeI16(buf, kOffBitpix, static_cast<int16_t>(header.bitpix));
    for (int i = 0; i < 8; ++i) writeF32(buf, kOffPixdim + 4 * i, static_cast<float>(header.pixdim[i]));
    writeF32(buf, kOffVoxOffset, kVoxOffset);
    writeF32(buf, kOffSclSlope, 0.0f);
    writeF32(buf, kOffSclInter, 0.0f);
    writeI16(buf, kOffQformCode, static_cast<int16_t>(header.qformCode));
    writeI16(buf, kOffSformCode, static_cast<int16_t>(header.sformCode));
    writeF32(buf, kOffQuaternB, static_cast<float>(header.quaternB));
    writeF32(buf, kOffQuaternC, static_cast<float>(header.quaternC));
    writeF32(buf, kOffQuaternD, static_cast<float>(header.quaternD));
    writeF32(buf, kOffQoffsetX, static_cast<float>(header.qoffsetX));
    writeF32(buf, kOffQoffsetY, static_cast<float>(header.qoffsetY));
    writeF32(buf, kOffQoffsetZ, static_cast<float>(header.qoffsetZ));
    for (int i = 0; i < 4; ++i) writeF32(buf, kOffSrowX + 4 * i, static_cast<float>(header.srowX(i)));
    for (int i = 0; i < 4; ++i) writeF32(buf, kOffSrowY + 4 * i, static_cast<float>(header.srowY(i)));
    for (int i = 0; i < 4; ++i) writeF32(buf, kOffSrowZ + 4 * i, static_cast<float>(header.srowZ(i)));
    buf[kOffMagic + 0] = 'n';
    buf[kOffMagic + 1] = '+';
    buf[kOffMagic + 2] = '1';
    buf[kOffMagic + 3] = '\0';

    std::vector<char> body;
    body.reserve(voxels.size() * 8 + 4);
    body.insert(body.end(), 4, '\0');  // pad from 348 to vox_offset 352
    for (double v : voxels) {
        appendVoxel(body, v, header.datatype);
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("writeNiftiVolume: could not open " + path + " for writing");
    out.write(buf.data(), static_cast<std::streamsize>(buf.size()));
    out.write(body.data(), static_cast<std::streamsize>(body.size()));
}

}  // namespace beam::mri
