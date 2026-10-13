#include "gui/treatment_target_store.hpp"

#include <algorithm>
#include <set>
#include <utility>
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
    snapshotMissingPlanPoints();
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

        // Placeholder row for a protocol with no entries.
        if (row[2].empty() && row[1] == "0") continue;
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
    snapshotMissingPlanPoints();
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

void TreatmentTargetStore::snapshotMissingPlanPoints() {
    for (TreatmentProtocolDefinition& definition : protocols_) {
        for (TreatmentProtocolEntry& entry : definition.entries) {
            if (!entry.points.empty()) continue;
            const TreatmentTarget* target = findTarget(entry.target);
            if (target == nullptr) continue;  // dangling; danglingTargetNames() reports it
            entry.points = target->points;
        }
    }
}

const std::vector<TreatmentTargetPoint>* TreatmentTargetStore::entryPoints(
    const std::string& protocolName, std::size_t entryIndex) const {
    const TreatmentProtocolDefinition* definition = findProtocol(protocolName);
    if (definition == nullptr || entryIndex >= definition->entries.size()) return nullptr;
    return &definition->entries[entryIndex].points;
}

int TreatmentTargetStore::firstEntryForTarget(const std::string& protocolName,
                                               const std::string& targetName) const {
    const TreatmentProtocolDefinition* definition = findProtocol(protocolName);
    if (definition == nullptr) return -1;
    for (std::size_t i = 0; i < definition->entries.size(); ++i)
        if (definition->entries[i].target == targetName) return static_cast<int>(i);
    return -1;
}

bool TreatmentTargetStore::setEntryPoints(const std::string& protocolName, std::size_t entryIndex,
                                           std::vector<TreatmentTargetPoint> points) {
    const auto found = std::find_if(protocols_.begin(), protocols_.end(),
                                    [&](const TreatmentProtocolDefinition& definition) {
                                        return definition.name == protocolName;
                                    });
    if (found == protocols_.end() || entryIndex >= found->entries.size() || points.empty())
        return false;
    for (std::size_t i = 0; i < points.size(); ++i) points[i].order = static_cast<int>(i) + 1;
    found->entries[entryIndex].points = std::move(points);
    return true;
}

std::vector<TreatmentTargetPoint> TreatmentTargetStore::pointsForSonication(
    const std::string& protocolName, std::size_t entryIndex,
    std::optional<double> durationSeconds) const {
    const std::vector<TreatmentTargetPoint>* owned = entryPoints(protocolName, entryIndex);
    if (owned == nullptr || owned->empty()) return {};
    std::vector<TreatmentTargetPoint> points = *owned;
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

void TreatmentTargetStore::loadPlanPoints(const std::string& path) {
    std::ifstream probe(path);
    if (!probe) return;  // nothing written yet; snapshots below supply the copies
    // Copies were keyed by target name before they moved onto the entry, and
    // that file cannot say which occurrence a row belonged to. Skip it rather
    // than fail the load or guess: every entry then takes a fresh copy of the
    // template below, which is what it held before anyone edited an occurrence.
    {
        std::string line;
        while (std::getline(probe, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const std::string stripped = trimmed(line);
            if (stripped.empty() || stripped.front() == '#') continue;
            if (stripped.rfind("protocol,target,", 0) == 0) return;  // the old header
            break;
        }
    }
    probe.close();

    const std::vector<std::vector<std::string>> rows = readCsv(path, 12);
    // A stored copy replaces whatever snapshotMissingPlanPoints() already put
    // there, rather than appending to it -- the loads can arrive in any order.
    std::set<std::pair<std::string, std::size_t>> replaced;
    for (const std::vector<std::string>& row : rows) {
        const std::string& protocolName = row[0];
        if (protocolName.empty()) throw std::runtime_error(path + ": a row has no protocol name");
        const int entryNumber = static_cast<int>(toDouble(row[1], path, "entry"));
        if (entryNumber < 1) throw std::runtime_error(path + ": entry numbers start at 1");
        const std::size_t entryIndex = static_cast<std::size_t>(entryNumber - 1);

        auto definition = std::find_if(protocols_.begin(), protocols_.end(),
                                       [&](const TreatmentProtocolDefinition& candidate) {
                                           return candidate.name == protocolName;
                                       });
        // A copy whose protocol or entry is gone is stale, not an error: neither
        // removeProtocol nor an edited schedule rewrites this file.
        if (definition == protocols_.end() || entryIndex >= definition->entries.size()) continue;

        TreatmentProtocolEntry& entry = definition->entries[entryIndex];
        std::vector<TreatmentTargetPoint>& points = entry.points;
        if (replaced.emplace(protocolName, entryIndex).second) points.clear();
        TreatmentTargetPoint point;
        point.order = static_cast<int>(toDouble(row[3], path, "point"));
        if (point.order != static_cast<int>(points.size()) + 1) {
            throw std::runtime_error(path + ": protocol \"" + protocolName + "\" entry " +
                                     std::to_string(entryNumber) +
                                     " point numbers must run 1..n");
        }
        const std::string& show = row[4];
        point.show = !(show == "0" || show == "N" || show == "n" || show == "false");
        point.amplitude = toDouble(row[5], path, "amplitude");
        point.startTimeSeconds = toDouble(row[6], path, "startTime");
        point.endTimeSeconds = toDouble(row[7], path, "endTime");
        point.burstDurationSeconds = toDouble(row[8], path, "burstDuration");
        point.burstIntervalSeconds = toDouble(row[9], path, "burstInterval");
        point.pulseDurationSeconds = toDouble(row[10], path, "pulseDuration");
        point.pulseIntervalSeconds = toDouble(row[11], path, "pulseInterval");
        points.push_back(point);
    }
    snapshotMissingPlanPoints();
}

void TreatmentTargetStore::savePlanPoints(const std::string& path) const {
    std::ofstream file(path);
    if (!file) throw std::runtime_error("cannot write " + path);
    file << "# BeamAI per-entry points: every scheduled occurrence carries its own copy,\n"
         << "# taken from the Target List when the entry was created. Two entries naming the\n"
         << "# same target are independent. The Target List is a template - editing it does\n"
         << "# not reach a copy already made here, and editing a copy reaches nothing else.\n"
         << "# `entry` is the 1-based position in the protocol's schedule; `target` is\n"
         << "# written for readability and is not what the row is matched on.\n"
         << "protocol,entry,target,point,show,amplitude,startTime,endTime,burstDuration,"
            "burstInterval,pulseDuration,pulseInterval\n";
    for (const TreatmentProtocolDefinition& definition : protocols_) {
        for (std::size_t index = 0; index < definition.entries.size(); ++index) {
            const TreatmentProtocolEntry& entry = definition.entries[index];
            for (const TreatmentTargetPoint& point : entry.points) {
                file << definition.name << ',' << (index + 1) << ',' << entry.target << ','
                     << point.order << ',' << (point.show ? 1 : 0) << ',' << point.amplitude << ','
                     << point.startTimeSeconds << ',' << point.endTimeSeconds << ','
                     << point.burstDurationSeconds << ',' << point.burstIntervalSeconds << ','
                     << point.pulseDurationSeconds << ',' << point.pulseIntervalSeconds << '\n';
            }
        }
    }
    if (!file) throw std::runtime_error("failed while writing " + path);
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
        // An empty protocol must still round-trip.
        if (definition.entries.empty()) {
            file << definition.name << ",0,,," << '\n';
            continue;
        }
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

bool TreatmentTargetStore::renameTarget(const std::string& from, const std::string& to) {
    if (to.empty() || from == to || targetIndex_.count(to) != 0) return false;
    const auto found = targetIndex_.find(from);
    if (found == targetIndex_.end()) return false;
    targets_[found->second].name = to;
    targetIndex_.clear();
    for (std::size_t i = 0; i < targets_.size(); ++i) targetIndex_.emplace(targets_[i].name, i);
    // The protocols are deliberately left alone. A target that has entered a
    // protocol was copied into it, name and points together, so the protocol
    // holds a complete target of its own and does not follow the template's
    // later edits -- renaming included.
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
    // An entry carries its own points, so removing it takes them with it and
    // adding one takes a fresh copy of the template below.
    snapshotMissingPlanPoints();
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
            // Its own points make it sonicable whatever the Target List holds.
            if (!entry.points.empty()) continue;
            if (std::find(missing.begin(), missing.end(), entry.target) == missing.end())
                missing.push_back(entry.target);
        }
    }
    return missing;
}

}  // namespace beam::gui
