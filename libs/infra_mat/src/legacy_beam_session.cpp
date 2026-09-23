#include "infra_mat/legacy_beam_session.hpp"

#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>

#include <matio.h>

namespace beam::infra::mat {
namespace {

struct MatCloser { void operator()(mat_t* p) const { if (p) Mat_Close(p); } };
struct VarCloser { void operator()(matvar_t* p) const { if (p) Mat_VarFree(p); } };

double numberAt(const matvar_t& value, std::size_t index) {
    if (!value.data) throw std::runtime_error("MAT variable has no numeric data");
    switch (value.data_type) {
        case MAT_T_DOUBLE: return static_cast<const double*>(value.data)[index];
        case MAT_T_SINGLE: return static_cast<const float*>(value.data)[index];
        case MAT_T_INT16: return static_cast<const std::int16_t*>(value.data)[index];
        case MAT_T_UINT16: return static_cast<const std::uint16_t*>(value.data)[index];
        case MAT_T_INT32: return static_cast<const std::int32_t*>(value.data)[index];
        case MAT_T_UINT32: return static_cast<const std::uint32_t*>(value.data)[index];
        case MAT_T_INT8: return static_cast<const std::int8_t*>(value.data)[index];
        case MAT_T_UINT8: return static_cast<const std::uint8_t*>(value.data)[index];
        default: throw std::runtime_error("Unsupported numeric type in legacy MAT session");
    }
}

matvar_t& field(matvar_t& structure, const char* name) {
    matvar_t* result = Mat_VarGetStructFieldByName(&structure, name, 0);
    if (!result) throw std::runtime_error(std::string("Legacy Beam session is missing sys.") + name);
    return *result;
}

matvar_t* optionalField(matvar_t& structure, const char* name, std::size_t index = 0) {
    return Mat_VarGetStructFieldByName(&structure, name, index);
}

std::string charValue(mat_t* file, matvar_t& value) {
    if (!value.data && Mat_VarReadDataAll(file, &value) != 0) return {};
    const std::size_t count = value.data_size ? value.nbytes / value.data_size : 0;
    std::string out;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        unsigned int ch = 0;
        switch (value.data_type) {
            case MAT_T_UINT16: ch = static_cast<const std::uint16_t*>(value.data)[i]; break;
            case MAT_T_UINT8: ch = static_cast<const std::uint8_t*>(value.data)[i]; break;
            case MAT_T_UTF8: ch = static_cast<const std::uint8_t*>(value.data)[i]; break;
            case MAT_T_UTF16: ch = static_cast<const std::uint16_t*>(value.data)[i]; break;
            default: return {};
        }
        if (ch && ch < 128) out.push_back(static_cast<char>(ch));
    }
    return out;
}

Eigen::VectorXd vectorField(mat_t* file, matvar_t& sys, const char* name, std::size_t expected) {
    matvar_t& source = field(sys, name);
    if (!source.data && Mat_VarReadDataAll(file, &source) != 0)
        throw std::runtime_error(std::string("Could not read sys.") + name);
    const std::size_t count = source.nbytes / source.data_size;
    if (count != expected) throw std::runtime_error(std::string("sys.") + name + " length does not match MRI");
    Eigen::VectorXd out(static_cast<Eigen::Index>(count));
    for (std::size_t i = 0; i < count; ++i) out(static_cast<Eigen::Index>(i)) = numberAt(source, i);
    return out;
}

beam::mri::Volume3D volumeValue(mat_t* file, matvar_t& source, const char* fieldName) {
    if (source.rank != 3 || !source.dims)
        throw std::runtime_error(std::string("sys.") + fieldName + " is not a 3-D volume");
    if (!source.data && Mat_VarReadDataAll(file, &source) != 0)
        throw std::runtime_error(std::string("Could not read sys.") + fieldName);
    const std::size_t nx = source.dims[0], ny = source.dims[1], nz = source.dims[2];
    beam::mri::Volume3D volume;
    volume.nx = static_cast<Eigen::Index>(nx);
    volume.ny = static_cast<Eigen::Index>(ny);
    volume.nz = static_cast<Eigen::Index>(nz);
    volume.kSlices.resize(nz);
    for (std::size_t k = 0; k < nz; ++k) {
        Eigen::MatrixXd slice(static_cast<Eigen::Index>(nx), static_cast<Eigen::Index>(ny));
        for (std::size_t j = 0; j < ny; ++j)
            for (std::size_t i = 0; i < nx; ++i)
                slice(static_cast<Eigen::Index>(i), static_cast<Eigen::Index>(j)) =
                    numberAt(source, i + nx * (j + ny * k));
        volume.kSlices[k] = std::move(slice);
    }
    return volume;
}

}  // namespace

LegacyBeamMri loadLegacyBeamMri(const std::string& path) {
    std::unique_ptr<mat_t, MatCloser> file(Mat_Open(path.c_str(), MAT_ACC_RDONLY));
    if (!file) throw std::runtime_error("Cannot open legacy Beam MAT session");
    std::unique_ptr<matvar_t, VarCloser> sys(Mat_VarReadInfo(file.get(), "sys"));
    if (!sys || sys->class_type != MAT_C_STRUCT) throw std::runtime_error("MAT file does not contain a Beam sys structure");
    matvar_t& image = field(*sys, "aImg");
    if (image.rank != 3 || !image.dims) throw std::runtime_error("sys.aImg is not a 3-D MRI volume");
    const std::size_t nx = image.dims[0], ny = image.dims[1], nz = image.dims[2];
    if (!nx || !ny || !nz) throw std::runtime_error("sys.aImg is empty");

    LegacyBeamMri result;
    result.volume = volumeValue(file.get(), image, "aImg");
    result.axes.dimLR = vectorField(file.get(), *sys, "ax", nx);
    result.axes.dimAP = vectorField(file.get(), *sys, "ay", ny);
    result.axes.dimIS = vectorField(file.get(), *sys, "az", nz);

    if (matvar_t* frame = optionalField(*sys, "frame")) {
        if (matvar_t* rois = optionalField(*frame, "FiducialROIs")) {
            const std::size_t count = rois->rank > 0 && rois->dims ? rois->dims[0] * rois->dims[1] : 0;
            for (std::size_t i = 0; i < count; ++i) {
                matvar_t* name = optionalField(*rois, "name", i);
                matvar_t* position = optionalField(*rois, "position", i);
                if (!name || !position) continue;
                if (!position->data && Mat_VarReadDataAll(file.get(), position) != 0) continue;
                if (position->nbytes / position->data_size != 3) continue;
                LegacyBeamMri::Fiducial marker;
                marker.name = charValue(file.get(), *name);
                marker.positionMm = Eigen::Vector3d(numberAt(*position, 0), numberAt(*position, 1), numberAt(*position, 2));
                result.fiducials.push_back(std::move(marker));
            }
        }
    }
    if (result.fiducials.size() == 6) {
        constexpr const char* canonicalNames[] = {
            "LeftY1Z3", "LeftY1Z1", "LeftY4Z1", "RightY1Z3", "RightY1Z1", "RightY4Z1"};
        for (std::size_t i = 0; i < result.fiducials.size(); ++i)
            if (result.fiducials[i].name.empty()) result.fiducials[i].name = canonicalNames[i];
    }
    return result;
}

}  // namespace beam::infra::mat
