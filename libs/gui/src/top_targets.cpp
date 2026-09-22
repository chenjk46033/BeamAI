#include "gui/top_targets.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
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

std::vector<std::string> bestTargetsRanking(AccFlag flag) {
    switch (flag) {
        case AccFlag::kScc:
            return {"SCC1", "SCC2", "SCC3", "SCC4", "SCC5", "SCC6"};
        case AccFlag::kAmcc:
            return {"aMCC1", "aMCC2", "aMCC3", "aMCC4", "aMCC5", "aMCC6"};
        case AccFlag::kOther:
        default:
            return {"SCC1", "aMCC1", "SCC2", "aMCC2", "SCC3", "aMCC3",
                    "SCC4", "aMCC4", "SCC5", "aMCC5", "SCC6", "aMCC6"};
    }
}

void eraseValue(std::vector<int>& v, int value) {
    v.erase(std::remove(v.begin(), v.end(), value), v.end());
}

}  // namespace

BestTargets getTopTargetsFromTreatmentProtocolTable(const std::vector<TargetResponse>& responses,
                                                     AccFlag accFlag) {
    constexpr double kBlockADurationThresholdSec = 60.0;
    constexpr int kCarryForwardMin = 1;
    constexpr int kCarryForwardMax = 3;

    // --- 1. block A filter ---
    std::vector<TargetResponse> blockA;
    for (const TargetResponse& r : responses) {
        if (r.duration < kBlockADurationThresholdSec) {
            blockA.push_back(r);
        }
    }

    // --- 2. group by exact name (alphabetical, case-sensitive), keep the
    //     highest response per group (first occurrence on ties), drop Sham
    //     (case-insensitive) ---
    std::vector<std::string> uniqueNames;
    for (const TargetResponse& r : blockA) {
        if (std::find(uniqueNames.begin(), uniqueNames.end(), r.name) == uniqueNames.end()) {
            uniqueNames.push_back(r.name);
        }
    }
    std::sort(uniqueNames.begin(), uniqueNames.end());

    std::vector<TargetResponse> uniq;  // "blockAResponses.unique.*"
    for (const std::string& name : uniqueNames) {
        int bestIdx = -1;
        double bestVal = -std::numeric_limits<double>::infinity();
        for (std::size_t k = 0; k < blockA.size(); ++k) {
            if (blockA[k].name == name && blockA[k].numericResponse > bestVal) {
                bestVal = blockA[k].numericResponse;
                bestIdx = static_cast<int>(k);
            }
        }
        if (bestIdx < 0 || ieq(name, "Sham")) {
            continue;
        }
        uniq.push_back(blockA[static_cast<std::size_t>(bestIdx)]);
    }

    // --- 3/4. sort descending (stable), split into positive/negative ---
    std::vector<int> orderI(uniq.size());
    std::iota(orderI.begin(), orderI.end(), 0);
    std::stable_sort(orderI.begin(), orderI.end(),
                     [&](int a, int b) { return uniq[static_cast<std::size_t>(a)].numericResponse >
                                                 uniq[static_cast<std::size_t>(b)].numericResponse; });

    std::vector<int> positive;
    std::vector<int> negative;
    std::vector<double> negativeV;
    for (int idx : orderI) {
        const double v = uniq[static_cast<std::size_t>(idx)].numericResponse;
        if (v > 0.0) {
            positive.push_back(idx);
        } else if (v < 0.0) {
            negative.push_back(idx);
            negativeV.push_back(v);
        }
        // v == 0 -> the source's unused `neutralResponses` bucket; dropped.
    }

    const std::vector<std::string> ranking = bestTargetsRanking(accFlag);
    const std::vector<std::string> worstRanking(ranking.rbegin(), ranking.rend());

    // --- 5. trim excess positives, ties broken toward the worst-ranked
    //     name (so the better-ranked target of a tied pair survives) ---
    if (static_cast<int>(positive.size()) > kCarryForwardMax) {
        const int nPositive = static_cast<int>(positive.size());
        for (int k = 0; k < nPositive - kCarryForwardMax; ++k) {
            double vmin = std::numeric_limits<double>::infinity();
            for (int idx : positive) {
                vmin = std::min(vmin, uniq[static_cast<std::size_t>(idx)].numericResponse);
            }
            std::vector<int> tied;
            for (int idx : positive) {
                if (uniq[static_cast<std::size_t>(idx)].numericResponse == vmin) {
                    tied.push_back(idx);
                }
            }
            if (tied.size() == 1) {
                eraseValue(positive, tied.front());
            } else {
                for (const std::string& worstName : worstRanking) {
                    bool chosen = false;
                    for (int cand : tied) {
                        if (ieq(worstName, uniq[static_cast<std::size_t>(cand)].name)) {
                            eraseValue(positive, cand);
                            chosen = true;
                            break;
                        }
                    }
                    if (chosen) {
                        break;
                    }
                }
            }
        }
    } else if (static_cast<int>(positive.size()) < kCarryForwardMin) {
        // --- 6. backfill from the ranking list, response >= 0 ---
        for (const std::string& rankName : ranking) {
            if (static_cast<int>(positive.size()) >= kCarryForwardMin) {
                continue;  // matches the source's no-op `continue` guard
            }
            bool alreadyChosen = false;
            for (int idx : positive) {
                if (ieq(rankName, uniq[static_cast<std::size_t>(idx)].name)) {
                    alreadyChosen = true;
                }
            }
            if (alreadyChosen) {
                continue;
            }
            for (std::size_t j = 0; j < uniq.size(); ++j) {
                if (ieq(rankName, uniq[j].name) && uniq[j].numericResponse >= 0.0) {
                    positive.push_back(static_cast<int>(j));
                }
            }
        }
    }

    // --- 7. still short: pull in the least-negative remainder, ties
    //     broken toward the best-ranked name this time ---
    if (static_cast<int>(positive.size()) < kCarryForwardMin) {
        const int np = static_cast<int>(positive.size());
        for (int k = 0; k < kCarryForwardMin - np; ++k) {
            if (negativeV.empty()) {
                continue;  // source: max([]) is a silent no-op here too
            }
            const double vmax = *std::max_element(negativeV.begin(), negativeV.end());
            std::vector<int> tied;
            for (std::size_t m = 0; m < negative.size(); ++m) {
                if (negativeV[m] == vmax) {
                    tied.push_back(negative[m]);
                }
            }
            const auto removeFromNegative = [&](int value) {
                for (std::size_t m = 0; m < negative.size(); ++m) {
                    if (negative[m] == value) {
                        negative.erase(negative.begin() + static_cast<long>(m));
                        negativeV.erase(negativeV.begin() + static_cast<long>(m));
                        break;
                    }
                }
            };
            if (tied.size() == 1) {
                positive.push_back(tied.front());
                removeFromNegative(tied.front());
            } else {
                for (const std::string& rankName : ranking) {
                    if (static_cast<int>(positive.size()) >= kCarryForwardMin) {
                        break;
                    }
                    bool chosen = false;
                    for (int cand : tied) {
                        if (ieq(rankName, uniq[static_cast<std::size_t>(cand)].name) &&
                            std::find(positive.begin(), positive.end(), cand) == positive.end()) {
                            positive.push_back(cand);
                            removeFromNegative(cand);
                            chosen = true;
                            break;
                        }
                    }
                    if (chosen) {
                        break;
                    }
                }
            }
        }
    }

    // --- final selection ---
    BestTargets best;
    for (int idx : positive) {
        const TargetResponse& t = uniq[static_cast<std::size_t>(idx)];
        best.sonicationNumber.push_back(t.sonicationNumber);
        best.name.push_back(t.name);
        best.numericResponse.push_back(t.numericResponse);
        best.duration.push_back(t.duration);
    }

    // --- 8. nothing selected: pad `name` only to 3 "Sham" (source quirk:
    //     the other three arrays are left empty) ---
    if (best.name.empty()) {
        best.name = {"Sham", "Sham", "Sham"};
    }

    // --- 9. replace every remaining Sham with the best unused ranked name ---
    for (std::string& n : best.name) {
        if (!ieq(n, "Sham")) {
            continue;
        }
        for (const std::string& rankName : ranking) {
            bool alreadyUsed = false;
            for (const std::string& used : best.name) {
                if (ieq(rankName, used)) {
                    alreadyUsed = true;
                }
            }
            if (!alreadyUsed) {
                n = rankName;
                break;
            }
        }
    }

    return best;
}

}  // namespace beam::gui
