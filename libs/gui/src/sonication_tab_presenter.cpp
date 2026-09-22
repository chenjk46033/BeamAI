#include "gui/sonication_tab_presenter.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <numeric>

namespace beam::gui {

namespace {
bool ieq(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}
}  // namespace

namespace {

int clampResponse(double raw) {
    if (std::isnan(raw)) {
        return 0;
    }
    int r = static_cast<int>(std::lround(raw));
    r = std::clamp(r, -2, 2);
    return r;
}

}  // namespace

MoodPainResponse getResponseFromTreatmentProtocolData(double responseMoodRaw, double responsePainRaw) {
    MoodPainResponse r;
    r.mood = clampResponse(responseMoodRaw);
    r.pain = clampResponse(responsePainRaw);
    r.total = r.pain + r.mood;
    return r;
}

std::string getNewProtocolName(const std::vector<std::string>& existingProtocolNames) {
    const std::size_t n = existingProtocolNames.size();
    std::string newName = "Protocol " + std::to_string(n);

    for (int j = 1; j <= 5; ++j) {
        // MATLAB: for i = 1:length(protocolTables(1:end-1))
        for (std::size_t i = 0; i + 1 < n; ++i) {
            if (existingProtocolNames[i] == newName) {
                if (j == 1) {
                    newName += "_01";
                } else if (!newName.empty()) {
                    newName.back() = static_cast<char>('0' + j);
                }
            }
        }
    }
    return newName;
}

Eigen::MatrixX3d colorMapRgb() {
    static const double kFixed[13][3] = {
        {0, 1, 0},
        {1, 0, 0},
        {0, 0, 1},
        {0.9290, 0.6940, 0.1250},
        {0.4940, 0.1840, 0.5560},
        {0, 0.75, 0.75},
        {0.3010, 0.7450, 0.9330},
        {0.6350, 0.0780, 0.1840},
        {0, 0.4470, 0.7410},
        {0.8500, 0.3250, 0.0980},
        {0.75, 0.75, 0},
        {0.75, 0, 0.75},
        {0, 0.5, 0},
    };
    constexpr int kFixedCount = 13;
    constexpr int kLinspacePoints = 87;  // nLeft = N(100) - NN(13)
    constexpr int kTotalRows = 87;       // source bug: loop bound is nLeft, not N -- see header

    Eigen::MatrixX3d m(kTotalRows, 3);
    for (int r = 0; r < kFixedCount; ++r) {
        m.row(r) = Eigen::RowVector3d(kFixed[r][0], kFixed[r][1], kFixed[r][2]);
    }
    for (int r = kFixedCount; r < kTotalRows; ++r) {
        const int k = r - kFixedCount;  // 0-based index into the 87-point linspace
        const double scale = 0.1 + static_cast<double>(k) * 0.8 / static_cast<double>(kLinspacePoints - 1);
        m.row(r) = scale * Eigen::RowVector3d(0.1, 1.0, 0.0);
    }
    return m;
}

int getCurrentShownSonication(const std::vector<bool>& show) {
    for (int i = static_cast<int>(show.size()); i >= 1; --i) {
        if (show[static_cast<std::size_t>(i - 1)]) {
            return i;
        }
    }
    return 1;
}

std::vector<int> sortSonicationTableOrder(const std::vector<double>& order) {
    std::vector<int> idx(order.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) { return order[a] < order[b]; });
    return idx;
}

PulseWaveformPlot computePulseWaveformPlot(double pulseDuration, double pulseInterval,
                                            double amplitude) {
    constexpr double kFreqHz = 650000.0;  // hardcoded in updateSonicationPlots.m
    const double fs = std::round(kFreqHz);  // round((1/freq)^-1) == round(freq)
    const double step = 1.0 / fs;
    const int n = static_cast<int>(std::floor(pulseInterval / step + 1e-9)) + 1;

    PulseWaveformPlot p;
    p.x.resize(n);
    p.y.resize(n);
    for (int i = 0; i < n; ++i) {
        const double x = static_cast<double>(i) * step;
        p.x(i) = x;
        p.y(i) = (x <= pulseDuration) ? amplitude : 0.0;
    }
    return p;
}

PulseWaveformPlot computeBurstWaveformPlot(double burstDuration, double burstInterval, double pulseDuration,
                                            double pulseInterval, double amplitude) {
    const double fs = 100000.0 / burstInterval;
    const double step = 1.0 / fs;
    const int n = static_cast<int>(std::floor(burstInterval / step + 1e-9)) + 1;

    PulseWaveformPlot p;
    p.x.resize(n);
    p.y.resize(n);
    p.y.setZero();

    int piCounter = 1;
    for (int j = 0; j < n; ++j) {
        const double x = static_cast<double>(j) * step;
        p.x(j) = x;
        if (x > burstDuration) continue;
        if (x <= static_cast<double>(piCounter - 1) * pulseInterval + pulseDuration) p.y(j) = amplitude;
        if (x > static_cast<double>(piCounter) * pulseInterval) {
            p.y(j) = amplitude;
            ++piCounter;
        }
    }
    return p;
}

RowColor computeTreatmentRowColor(double currResponse) {
    if (currResponse < 0.0) {
        return {1.0, 1.0, 0.0};  // yellow
    }
    if (currResponse > 0.0) {
        const double clamped = std::min(currResponse, 4.0);
        return {0.0, 0.5 + 0.5 * (clamped / 4.0), 0.0};
    }
    return {1.0, 1.0, 1.0};  // white
}

AccFlag accFlagForProtocolName(const std::string& protocolName) {
    if (ieq(protocolName, "PainACC")) {
        return AccFlag::kOther;  // 'ACC' matches neither the SCC nor aMCC branch
    }
    if (ieq(protocolName, "PainSCCandAMCC")) {
        return AccFlag::kScc;  // ACCFlag{1} of {'SCC','aMCC'}
    }
    return AccFlag::kAmcc;  // ACCFlag{1} of {'aMCC','SCC'} -- the default branch
}

std::string newProtocolName(const std::vector<std::string>& existingNames) {
    const std::size_t n = existingNames.size();
    std::string newName = "Protocol " + std::to_string(n);
    const std::size_t compareCount = n > 0 ? n - 1 : 0;  // app.sys.protocolTables(1:end-1)
    for (int j = 1; j <= 5; ++j) {
        for (std::size_t i = 0; i < compareCount; ++i) {
            if (existingNames[i] == newName) {
                if (j == 1) {
                    newName += "_01";
                } else {
                    newName.back() = static_cast<char>('0' + j);
                }
            }
        }
    }
    return newName;
}

std::string exampleTargetHelpText(const std::string& exampleFlag) {
    std::string text =
        "Center targets on midline. Space targets by 4 mm in the Y-Z plane. Targets should be "
        "centered on white matter tracts in the ACC";
    if (exampleFlag == "VIM") {
        text =
            "Identify AC-PC points with fiducial markers. Use the table in the bottom left to move "
            "14 mm X, -6 mm Y, and 0 mm Z from the MC reference point";
    }
    return text;
}

}  // namespace beam::gui
