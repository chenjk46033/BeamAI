#include "registration/array_transform.hpp"

#include <stdexcept>

#include "array/affine.hpp"
#include "array/array_struct.hpp"
#include "registration/affine_registration.hpp"

namespace beam::registration {

using beam::array::ArrayStruct;

namespace {

// applyAffineToArrayData.m's local updateArray helper: overwrite only
// .rect and .element, leave the rest untouched.
void updateArray(ArrayStruct& target, const ArrayStruct& src) {
    target.rect = src.rect;
    target.element = src.element;
}

}  // namespace

AffineArrayResult applyAffineToArrayData(const Eigen::Matrix4d& affineMatrix,
                                          beam::array::ArrayData arrayData) {
    const double frequency = arrayData.arrayTotal.frequency;
    const std::array<double, 2> elementDimensions = arrayData.arrayTotal.elementDimensions;

    {
        const Eigen::MatrixXd rect = beam::array::applyAffineToRect(affineMatrix, arrayData.arrayTotal.rect);
        const ArrayStruct s = beam::array::defineArrayStruct(rect, frequency, elementDimensions);
        updateArray(arrayData.arrayTotal, s);
    }
    for (ArrayStruct& half : arrayData.array) {
        const Eigen::MatrixXd rect = beam::array::applyAffineToRect(affineMatrix, half.rect);
        const ArrayStruct s = beam::array::defineArrayStruct(rect, frequency, elementDimensions);
        updateArray(half, s);
    }

    AffineArrayResult result;
    result.fiducialMarkers = setArrayFiducialMarkers(arrayData);
    result.arrayData = std::move(arrayData);
    return result;
}

AffineArrayResult registerArrayToFiducials(const beam::array::ArrayData& originArrayData,
                                            const std::vector<Eigen::Vector3d>& mriFiducialsMm) {
    const std::vector<FiducialMarker> originFiducials = setArrayFiducialMarkers(originArrayData);
    if (originFiducials.size() != 6 || mriFiducialsMm.size() != 6) {
        throw std::invalid_argument(
            "registerArrayToFiducials: BeamV0's fixed registration weights require exactly 6 fiducials on "
            "each side");
    }

    Eigen::Matrix3Xd arrayFiducialsMm(3, 6);
    Eigen::Matrix3Xd mriFiducials(3, 6);
    for (int i = 0; i < 6; ++i) {
        arrayFiducialsMm.col(i) = originFiducials[static_cast<size_t>(i)].position * 1000.0;  // m -> mm
        mriFiducials.col(i) = mriFiducialsMm[static_cast<size_t>(i)];
    }

    const Eigen::Matrix4d affineMatrix = getAffineMatrixFromRegistration(arrayFiducialsMm, mriFiducials,
                                                                          /*metersToMm=*/true);
    return applyAffineToArrayData(affineMatrix, originArrayData);
}

AffineArrayResult registerCurrentTransducerPosition(const beam::array::ArrayData& originArrayData,
                                                     const std::vector<Eigen::Vector3d>& mriFiducialsMm,
                                                     double horizontalSliderValue, double verticalSliderValue) {
    constexpr double kHorizontalDeltaMm = 7.5;  // app.sys.frame.horizontalDelta
    constexpr double kVerticalDeltaMm = 10.0;   // app.sys.frame.verticalDelta

    const AffineArrayResult fiducialFit = registerArrayToFiducials(originArrayData, mriFiducialsMm);

    // The source builds the basis from `app.FiducialROIs` -- the operator's
    // measured MRI fiducials -- not from the fitted array's own markers:
    //
    //     registerArrayToFiducials(app);
    //     FiducialROIs = app.FiducialROIs;
    //     [M, Xvector,Yvector,Zvector] = getTranslationMatrixFromTransducerFiducials(app,FiducialROIs);
    //
    // The two differ by the fit residual. Rebuild the measured set here,
    // borrowing the marker names (and their order) from the nominal array so
    // getFiducialPositionFromName can still resolve them.
    std::vector<FiducialMarker> measuredFiducials = setArrayFiducialMarkers(originArrayData);
    for (std::size_t i = 0; i < measuredFiducials.size(); ++i) {
        measuredFiducials[i].position = mriFiducialsMm[i] / 1000.0;  // mm -> m
    }

    TransducerBasis basis = getTranslationMatrixFromTransducerFiducials(measuredFiducials);
    // `if Zvector(3)<0, Zvector = -1*Zvector; end` -- the basis comes from
    // differences between fiducials, so its vertical axis can come out
    // pointing inferior depending on the array's pose. The source forces it
    // superior before assembling M; without this the vertical lock-position
    // offset is applied in the wrong direction whenever that happens.
    Eigen::Vector3d zVector = basis.zVector;
    if (zVector.z() < 0.0) zVector = -zVector;

    // M's columns are the basis vectors (the source's trailing transpose),
    // mapping transducer-frame displacement into world RAS.
    Eigen::Matrix3d m;
    m.col(0) = basis.xVector;
    m.col(1) = basis.yVector;
    m.col(2) = zVector;

    const double dH = horizontalSliderValue - 1.0;
    const double dV = verticalSliderValue - 1.0;
    const double dY = -dH * kHorizontalDeltaMm / 1000.0;  // mm -> m
    const double dZ = dV * kVerticalDeltaMm / 1000.0;
    const Eigen::Vector3d regdXYZ = m * Eigen::Vector3d(0.0, dY, dZ);

    Eigen::Matrix4d translation = Eigen::Matrix4d::Identity();
    translation.block<3, 1>(0, 3) = regdXYZ;

    return applyAffineToArrayData(translation, fiducialFit.arrayData);
}

}  // namespace beam::registration
