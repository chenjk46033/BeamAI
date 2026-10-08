#include "gui/treatment_target_store.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace beam::gui {
namespace {

std::string trimmed(std::string text) {
    const auto notSpace = [](unsigned char c) { return std::isspace(c) == 0; };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), notSpace));
    text.erase(std::find_if(text.rbegin(), text.rend(), notSpace).base(), text.end());
    return text;
}

std::vector<std::vector<std::string>> readCsv(const std::string& path, std::size_t columns) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("cannot open " + path);

    std::vector<std::vector<std::string>> rows;
    std::string line;
    bool headerSeen = false;
    int lineNumber = 0;
    while (std::getline(file, line)) {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string stripped = trimmed(line);
        if (stripped.empty() || stripped.front() == '#') continue;

        // Split by hand rather than with getline, which drops a trailing
        // empty field -- and an absent optional column is exactly that.
        std::vector<std::string> fields;
        std::string field;
        for (const char c : stripped) {
            if (c == ',') {
                fields.push_back(trimmed(field));
                field.clear();
            } else {
                field += c;
            }
        }
        fields.push_back(trimmed(field));

        if (!headerSeen) {
            headerSeen = true;
            continue;
        }
        if (fields.size() != columns) {
            throw std::runtime_error(path + ":" + std::to_string(lineNumber) + ": expected " +
                                     std::to_string(columns) + " fields, got " +
                                     std::to_string(fields.size()));
        }
        rows.push_back(std::move(fields));
    }
    return rows;
}

double toDouble(const std::string& text, const std::string& path, const std::string& what) {
    try {
        return std::stod(text);
    } catch (const std::exception&) {
        throw std::runtime_error(path + ": " + what + " is not a number: \"" + text + "\"");
    }
}

}  // namespace

void TreatmentTargetStore::loadTargets(const std::string& path) {
    const std::vector<std::vector<std::string>> rows = readCsv(path, 10);
    std::vector<TreatmentTarget> loaded;
    std::map<std::string, std::size_t> index;
    // Rows of the same target are its points, in file order. A target's rows
    // need not be contiguous, but its point numbers must run 1..n -- a gap
    // would mean a row was lost somewhere upstream.
    for (const std::vector<std::string>& row : rows) {
        const std::string& name = row[0];
        if (name.empty()) throw std::runtime_error(path + ": a target has no name");

        const auto found = index.find(name);
        if (found == index.end()) {
            index.emplace(name, loaded.size());
            loaded.push_back(TreatmentTarget{name, {}});
        }
        TreatmentTarget& target = loaded[index.at(name)];

        TreatmentTargetPoint point;
        point.order = static_cast<int>(toDouble(row[1], path, "point"));
        if (point.order != static_cast<int>(target.points.size()) + 1)
            throw std::runtime_error(path + ": target \"" + name + "\" point numbers must run 1..n");
        const std::string& show = row[2];
        point.show = !(show == "0" || show == "N" || show == "n" || show == "false");
        point.amplitude = toDouble(row[3], path, "amplitude");
        point.startTimeSeconds = toDouble(row[4], path, "startTime");
        point.endTimeSeconds = toDouble(row[5], path, "endTime");
        point.burstDurationSeconds = toDouble(row[6], path, "burstDuration");
        point.burstIntervalSeconds = toDouble(row[7], path, "burstInterval");
        point.pulseDurationSeconds = toDouble(row[8], path, "pulseDuration");
        point.pulseIntervalSeconds = toDouble(row[9], path, "pulseInterval");
        target.points.push_back(point);
    }
    targets_ = std::move(loaded);
    targetIndex_ = std::move(index);
}

void TreatmentTargetStore::loadProtocols(const std::string& path) {
    const std::vector<std::vector<std::string>> rows = readCsv(path, 5);
    std::vector<TreatmentProtocolDefinition> loaded;
    for (const std::vector<std::string>& row : rows) {
        const std::string& protocolName = row[0];
        if (protocolName.empty()) throw std::runtime_error(path + ": a row has no protocol name");

        auto found = std::find_if(loaded.begin(), loaded.end(),
                                  [&](const TreatmentProtocolDefinition& definition) {
                                      return definition.name == protocolName;
                                  });
        if (found == loaded.end()) {
            loaded.push_back(TreatmentProtocolDefinition{protocolName, {}});
            found = std::prev(loaded.end());
        }

        TreatmentProtocolEntry entry;
        entry.order = static_cast<int>(toDouble(row[1], path, "order"));
        entry.target = row[2];
        if (entry.target.empty()) throw std::runtime_error(path + ": an entry has no target");
        if (!row[3].empty()) entry.amplitude = toDouble(row[3], path, "amplitude");
        if (!row[4].empty()) entry.durationSeconds = toDouble(row[4], path, "duration");
        found->entries.push_back(std::move(entry));
    }
    for (TreatmentProtocolDefinition& definition : loaded) {
        std::stable_sort(definition.entries.begin(), definition.entries.end(),
                         [](const TreatmentProtocolEntry& a, const TreatmentProtocolEntry& b) {
                             return a.order < b.order;
                         });
    }
    protocols_ = std::move(loaded);
}

const TreatmentTarget* TreatmentTargetStore::findTarget(const std::string& name) const {
    const auto found = targetIndex_.find(name);
    return found == targetIndex_.end() ? nullptr : &targets_[found->second];
}

bool TreatmentTargetStore::updateTarget(const TreatmentTarget& target) {
    const auto found = targetIndex_.find(target.name);
    if (found == targetIndex_.end()) return false;
    TreatmentTarget stored = target;
    for (std::size_t i = 0; i < stored.points.size(); ++i)
        stored.points[i].order = static_cast<int>(i) + 1;
    targets_[found->second] = std::move(stored);
    return true;
}

bool TreatmentTargetStore::setTargetPoints(const std::string& name,
                                            std::vector<TreatmentTargetPoint> points) {
    const auto found = targetIndex_.find(name);
    if (found == targetIndex_.end() || points.empty()) return false;
    for (std::size_t i = 0; i < points.size(); ++i) points[i].order = static_cast<int>(i) + 1;
    targets_[found->second].points = std::move(points);
    return true;
}

std::vector<TreatmentTargetPoint> TreatmentTargetStore::pointsForSonication(
    const std::string& targetName, std::optional<double> durationSeconds) const {
    const TreatmentTarget* target = findTarget(targetName);
    if (target == nullptr) return {};
    std::vector<TreatmentTargetPoint> points = target->points;
    if (durationSeconds) {
        // The scalar assignment in updateSonicateSettingsForCurrentSonication.m
        // writes one value down a whole table column, so every row moves.
        for (TreatmentTargetPoint& point : points) {
            point.startTimeSeconds = 0.0;
            point.endTimeSeconds = *durationSeconds;
        }
    }
    return points;
}

const TreatmentProtocolDefinition* TreatmentTargetStore::findProtocol(const std::string& name) const {
    const auto found = std::find_if(protocols_.begin(), protocols_.end(),
                                    [&](const TreatmentProtocolDefinition& definition) {
                                        return definition.name == name;
                                    });
    return found == protocols_.end() ? nullptr : &*found;
}

std::vector<std::string> TreatmentTargetStore::distinctTargetsOf(const std::string& protocolName) const {
    const TreatmentProtocolDefinition* definition = findProtocol(protocolName);
    if (definition == nullptr) return {};
    std::vector<std::string> names;
    for (const TreatmentProtocolEntry& entry : definition->entries) {
        if (std::find(names.begin(), names.end(), entry.target) == names.end())
            names.push_back(entry.target);
    }
    return names;
}

void TreatmentTargetStore::saveTargets(const std::string& path) const {
    std::ofstream file(path);
    if (!file) throw std::runtime_error("cannot write " + path);
    file << "# BeamAI Target List: the editable parameter definition for each target.\n"
         << "# One row per target point - a target's sonications are rows of its own\n"
         << "# stimParamTable in BeamV0, each carrying its own parameters.\n"
         << "# X/Y/Z are NOT here - placement is per-patient session data.\n"
         << "name,point,show,amplitude,startTime,endTime,burstDuration,burstInterval,"
            "pulseDuration,pulseInterval\n";
    for (const TreatmentTarget& target : targets_) {
        for (const TreatmentTargetPoint& point : target.points) {
            file << target.name << ',' << point.order << ',' << (point.show ? 1 : 0) << ','
                 << point.amplitude << ',' << point.startTimeSeconds << ','
                 << point.endTimeSeconds << ',' << point.burstDurationSeconds << ','
                 << point.burstIntervalSeconds << ',' << point.pulseDurationSeconds << ','
                 << point.pulseIntervalSeconds << '\n';
        }
    }
    if (!file) throw std::runtime_error("failed while writing " + path);
}

void TreatmentTargetStore::saveProtocols(const std::string& path) const {
    std::ofstream file(path);
    if (!file) throw std::runtime_error("cannot write " + path);
    file << "# BeamAI treatment protocols: an ordered list of references into the Target List.\n"
         << "# amplitude and duration are what this entry starts from; blank means use the "
            "target's own.\n"
         << "protocol,order,target,amplitude,duration\n";
    for (const TreatmentProtocolDefinition& definition : protocols_) {
        for (const TreatmentProtocolEntry& entry : definition.entries) {
            file << definition.name << ',' << entry.order << ',' << entry.target << ',';
            if (entry.amplitude) file << *entry.amplitude;
            file << ',';
            if (entry.durationSeconds) file << *entry.durationSeconds;
            file << '\n';
        }
    }
    if (!file) throw std::runtime_error("failed while writing " + path);
}

bool TreatmentTargetStore::addTarget(const TreatmentTarget& target) {
    if (target.name.empty() || targetIndex_.count(target.name) != 0) return false;
    TreatmentTarget stored = target;
    // createStimParamTable.m's N = 1: a new target starts with one point
    // rather than none, so it is sonicable the moment it exists.
    if (stored.points.empty()) stored.points.push_back(TreatmentTargetPoint{});
    for (std::size_t i = 0; i < stored.points.size(); ++i)
        stored.points[i].order = static_cast<int>(i) + 1;
    targetIndex_.emplace(stored.name, targets_.size());
    targets_.push_back(std::move(stored));
    return true;
}

bool TreatmentTargetStore::removeTarget(const std::string& name) {
    const auto found = targetIndex_.find(name);
    if (found == targetIndex_.end()) return false;
    targets_.erase(targets_.begin() + static_cast<std::ptrdiff_t>(found->second));
    targetIndex_.clear();
    for (std::size_t i = 0; i < targets_.size(); ++i) targetIndex_.emplace(targets_[i].name, i);
    return true;
}

bool TreatmentTargetStore::addProtocol(const std::string& name) {
    if (name.empty() || findProtocol(name) != nullptr) return false;
    protocols_.push_back(TreatmentProtocolDefinition{name, {}});
    return true;
}

bool TreatmentTargetStore::renameProtocol(const std::string& from, const std::string& to) {
    if (to.empty() || findProtocol(to) != nullptr) return false;
    const auto found = std::find_if(protocols_.begin(), protocols_.end(),
                                    [&](const TreatmentProtocolDefinition& definition) {
                                        return definition.name == from;
                                    });
    if (found == protocols_.end()) return false;
    found->name = to;
    return true;
}

bool TreatmentTargetStore::removeProtocol(const std::string& name) {
    const auto found = std::find_if(protocols_.begin(), protocols_.end(),
                                    [&](const TreatmentProtocolDefinition& definition) {
                                        return definition.name == name;
                                    });
    if (found == protocols_.end()) return false;
    protocols_.erase(found);
    return true;
}

bool TreatmentTargetStore::setProtocolEntries(const std::string& name,
                                               std::vector<TreatmentProtocolEntry> entries) {
    const auto found = std::find_if(protocols_.begin(), protocols_.end(),
                                    [&](const TreatmentProtocolDefinition& definition) {
                                        return definition.name == name;
                                    });
    if (found == protocols_.end()) return false;
    for (std::size_t i = 0; i < entries.size(); ++i) entries[i].order = static_cast<int>(i) + 1;
    found->entries = std::move(entries);
    return true;
}

bool TreatmentTargetStore::appendProtocolEntry(const std::string& protocolName,
                                                const TreatmentProtocolEntry& entry) {
    const TreatmentProtocolDefinition* definition = findProtocol(protocolName);
    if (definition == nullptr || entry.target.empty()) return false;
    std::vector<TreatmentProtocolEntry> entries = definition->entries;
    entries.push_back(entry);
    return setProtocolEntries(protocolName, std::move(entries));
}

std::vector<std::string> TreatmentTargetStore::danglingTargetNames() const {
    std::vector<std::string> missing;
    for (const TreatmentProtocolDefinition& definition : protocols_) {
        for (const TreatmentProtocolEntry& entry : definition.entries) {
            if (targetIndex_.count(entry.target) != 0) continue;
            if (std::find(missing.begin(), missing.end(), entry.target) == missing.end())
                missing.push_back(entry.target);
        }
    }
    return missing;
}

}  // namespace beam::gui
