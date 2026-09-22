#include "gui/initial_placement_presenter.hpp"

namespace beam::gui {

beam::registration::AffineArrayResult centerArrayOnMri(const beam::array::ArrayData& arrayData,
                                                         const beam::mri::RasAxisVectors& axes) {
    // mean(arrayData.arrayTotal.rect(17:19,:), 2) -- MATLAB rows 17:19,
    // 1-based, are this project's 0-based rows 16:18 (kRectCenterStartRow).
    const Eigen::Vector3d centerArrayM =
        arrayData.arrayTotal.rect.block(beam::array::kRectCenterStartRow, 0, 3, arrayData.arrayTotal.rect.cols())
            .rowwise()
            .mean() -
        Eigen::Vector3d(0.0, 50.0, -25.0) / 1000.0;

    const Eigen::Vector3d centerMriM =
        Eigen::Vector3d(axes.dimLR.mean(), axes.dimAP.mean(), axes.dimIS.mean()) / 1000.0;

    Eigen::Matrix4d affineMatrix = Eigen::Matrix4d::Identity();
    affineMatrix.block<3, 1>(0, 3) = centerMriM - centerArrayM;

    return beam::registration::applyAffineToArrayData(affineMatrix, arrayData);
}

ArrayFramePosition defaultArrayFramePosition() { return ArrayFramePosition{}; }

}  // namespace beam::gui
