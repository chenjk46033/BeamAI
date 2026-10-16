#include "gui/treatment_log.hpp"

#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <cmath>
#include <map>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace beam::gui {
namespace {

std::string trimmed(std::string text) {
    const auto notSpace = [](unsigned char c) { return std::isspace(c) == 0; };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), notSpace));
    text.erase(std::find_if(text.rbegin(), text.rend(), notSpace).base(), text.end());
    return text;
}

// Quoted fields matter for notes, which an operator can type a comma into.
std::vector<std::string> splitRow(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;
    for (const char c : line) {
        if (c == '"') {
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

std::string quote(const std::string& text) {
    if (text.find(',') == std::string::npos && text.find('"') == std::string::npos) return text;
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"') out += '"';
        out += c;
    }
    out += '"';
    return out;
}

double toDouble(const std::string& text, const std::string& path, const char* field) {
    try {
        return std::stod(text);
    } catch (const std::exception&) {
        throw std::runtime_error(path + ": " + field + " is not a number (\"" + text + "\")");
    }
}

std::optional<double> toOptional(const std::string& text, const std::string& path,
                                 const char* field) {
    if (text.empty() || text == "-") return std::nullopt;
    return toDouble(text, path, field);
}

constexpr std::size_t kColumns = 20;

}  // namespace

namespace {

bool sameSlot(const TreatmentLogRow& row, const std::string& protocol, int visit, int sequence) {
    return row.protocol == protocol && row.visit == visit && row.sequence == sequence;
}

}  // namespace

int TreatmentLog::append(TreatmentLogRow row) {
    int highest = 0;
    for (const TreatmentLogRow& existing : rows_)
        if (existing.visit == row.visit && existing.protocol == row.protocol)
            highest = std::max(highest, existing.sequence);
    row.sequence = highest + 1;
    const int sequence = row.sequence;
    rows_.push_back(std::move(row));
    return sequence;
}

void TreatmentLog::record(TreatmentLogRow row) {
    const auto found = std::find_if(rows_.begin(), rows_.end(), [&](const TreatmentLogRow& existing) {
        return sameSlot(existing, row.protocol, row.visit, row.sequence);
    });
    if (found == rows_.end()) {
        rows_.push_back(std::move(row));
        return;
    }
    // Keep what the operator wrote against this slot: a re-fire replaces what
    // was sent, not the response recorded for it.
    row.responsePain = found->responsePain;
    row.responseMood = found->responseMood;
    row.notes = found->notes;
    // Nor whether the row was the operator's addition. Losing that unfroze the
    // visit, and the next reseed then deleted every added row the schedule did
    // not account for.
    row.fromPlan = found->fromPlan;
    *found = std::move(row);
}

// Nothing may be moved once it has run: its sequence is where it happened. So
// an insert or a skip is only possible while every row below the one in
// question is still unfired.
bool TreatmentLog::canInsertBefore(const std::string& protocol, int visit, int sequence) const {
    if (find(protocol, visit, sequence) == nullptr) return false;
    // The row at this position moves down too, so it counts.
    return std::none_of(rows_.begin(), rows_.end(), [&](const TreatmentLogRow& row) {
        return row.protocol == protocol && row.visit == visit && row.sequence >= sequence &&
               delivered(row);
    });
}

bool TreatmentLog::insertBefore(const std::string& protocol, int visit, int sequence,
                                TreatmentLogRow row) {
    if (!canInsertBefore(protocol, visit, sequence)) return false;
    for (TreatmentLogRow& existing : rows_) {
        if (existing.protocol != protocol || existing.visit != visit) continue;
        if (existing.sequence >= sequence) ++existing.sequence;
    }
    row.protocol = protocol;
    row.visit = visit;
    row.sequence = sequence;
    row.fromPlan = false;
    row.startedAt.clear();
    row.endedAt.clear();
    rows_.push_back(std::move(row));
    return true;
}

bool TreatmentLog::canInsertAfter(const std::string& protocol, int visit, int sequence) const {
    if (find(protocol, visit, sequence) == nullptr) return false;
    return std::none_of(rows_.begin(), rows_.end(), [&](const TreatmentLogRow& row) {
        return row.protocol == protocol && row.visit == visit && row.sequence > sequence &&
               delivered(row);
    });
}

bool TreatmentLog::canRemoveAt(const std::string& protocol, int visit, int sequence) const {
    const TreatmentLogRow* target = find(protocol, visit, sequence);
    if (target == nullptr || delivered(*target)) return false;
    return canInsertAfter(protocol, visit, sequence);
}

bool TreatmentLog::insertAfter(const std::string& protocol, int visit, int sequence,
                               TreatmentLogRow row) {
    if (!canInsertAfter(protocol, visit, sequence)) return false;
    for (TreatmentLogRow& existing : rows_) {
        if (existing.protocol != protocol || existing.visit != visit) continue;
        if (existing.sequence > sequence) ++existing.sequence;
    }
    row.protocol = protocol;
    row.visit = visit;
    row.sequence = sequence + 1;
    row.fromPlan = false;
    row.startedAt.clear();
    row.endedAt.clear();
    rows_.push_back(std::move(row));
    return true;
}

bool TreatmentLog::removeAt(const std::string& protocol, int visit, int sequence) {
    if (!canRemoveAt(protocol, visit, sequence)) return false;
    rows_.erase(std::remove_if(rows_.begin(), rows_.end(),
                               [&](const TreatmentLogRow& row) {
                                   return sameSlot(row, protocol, visit, sequence);
                               }),
                rows_.end());
    for (TreatmentLogRow& existing : rows_) {
        if (existing.protocol != protocol || existing.visit != visit) continue;
        if (existing.sequence > sequence) --existing.sequence;
    }
    // The visit no longer follows the plan position for position.
    for (TreatmentLogRow& existing : rows_)
        if (existing.protocol == protocol && existing.visit == visit) existing.fromPlan = false;
    return true;
}

bool TreatmentLog::canRemoveVisit(const std::string& protocol, int visit) const {
    return std::none_of(rows_.begin(), rows_.end(), [&](const TreatmentLogRow& row) {
        return row.protocol == protocol && row.visit == visit && delivered(row);
    });
}

int TreatmentLog::removeVisit(const std::string& protocol, int visit) {
    if (!canRemoveVisit(protocol, visit)) return 0;
    const std::size_t before = rows_.size();
    rows_.erase(std::remove_if(rows_.begin(), rows_.end(),
                               [&](const TreatmentLogRow& row) {
                                   return row.protocol == protocol && row.visit == visit;
                               }),
                rows_.end());
    return static_cast<int>(before - rows_.size());
}

bool TreatmentLog::visitFrozen(const std::string& protocol, int visit) const {
    // Only a hand-edited sequence freezes a visit. A delivery does not: the
    // delivered row is protected on its own, and freezing the whole visit for
    // it meant a protocol edit could not reach the rows still ahead of the
    // operator without starting a new visit.
    return std::any_of(rows_.begin(), rows_.end(), [&](const TreatmentLogRow& row) {
        return row.protocol == protocol && row.visit == visit && !row.fromPlan;
    });
}

void TreatmentLog::seedVisit(int visit, const std::string& protocol,
                             const std::vector<TreatmentLogRow>& planned) {
    // A row that has been delivered is a record and is never rewritten. A row
    // that has not is still only this visit's copy of the plan, so an edited
    // protocol reaches it -- no new visit required.
    //
    // The exception is a visit whose sequence the operator has changed by
    // skipping or adding a sonication: its list is theirs and this leaves it
    // alone entirely. Adding back merely "missing" positions undid every skip,
    // because skipping closes the gap and so vacates the last position -- which
    // then looked missing and was refilled from the plan.
    if (visitFrozen(protocol, visit)) return;
    for (const TreatmentLogRow& row : planned) {
        TreatmentLogRow copy = row;
        copy.visit = visit;
        copy.protocol = protocol;
        const auto found = std::find_if(rows_.begin(), rows_.end(), [&](const TreatmentLogRow& e) {
            return sameSlot(e, protocol, visit, row.sequence);
        });
        if (found == rows_.end()) {
            rows_.push_back(std::move(copy));
            continue;
        }
        if (delivered(*found)) continue;  // a record, not a plan copy
        // Keep what the operator wrote against the row; take everything the
        // protocol and the Target List say.
        copy.responsePain = found->responsePain;
        copy.responseMood = found->responseMood;
        copy.notes = found->notes;
        *found = std::move(copy);
    }
    // The schedule may have got shorter. Unfired rows past its end are plan
    // copies of entries that no longer exist; fired ones stay, because they
    // happened.
    const int planned_count = static_cast<int>(planned.size());
    rows_.erase(std::remove_if(rows_.begin(), rows_.end(),
                               [&](const TreatmentLogRow& row) {
                                   return row.protocol == protocol && row.visit == visit &&
                                          row.sequence > planned_count && !delivered(row);
                               }),
                rows_.end());
}

std::vector<TreatmentLogRow> TreatmentLog::rowsFor(const std::string& protocol, int visit) const {
    std::vector<TreatmentLogRow> found;
    for (const TreatmentLogRow& row : rows_)
        if (row.visit == visit && row.protocol == protocol) found.push_back(row);
    std::sort(found.begin(), found.end(), [](const TreatmentLogRow& a, const TreatmentLogRow& b) {
        return a.sequence < b.sequence;
    });
    return found;
}

const TreatmentLogRow* TreatmentLog::find(const std::string& protocol, int visit,
                                          int sequence) const {
    const auto found = std::find_if(rows_.begin(), rows_.end(), [&](const TreatmentLogRow& row) {
        return sameSlot(row, protocol, visit, sequence);
    });
    return found == rows_.end() ? nullptr : &*found;
}

bool TreatmentLog::setResponse(const std::string& protocol, int visit, int sequence,
                               std::optional<double> pain, std::optional<double> mood) {
    const auto found = std::find_if(rows_.begin(), rows_.end(), [&](const TreatmentLogRow& row) {
        return sameSlot(row, protocol, visit, sequence);
    });
    if (found == rows_.end()) return false;
    found->responsePain = pain;
    found->responseMood = mood;
    return true;
}

bool TreatmentLog::setStatus(const std::string& protocol, int visit, int sequence,
                             int statusCode) {
    const auto found = std::find_if(rows_.begin(), rows_.end(), [&](const TreatmentLogRow& row) {
        return sameSlot(row, protocol, visit, sequence);
    });
    if (found == rows_.end()) return false;
    found->statusCode = statusCode;
    return true;
}

bool TreatmentLog::setNotes(const std::string& protocol, int visit, int sequence,
                            const std::string& notes) {
    const auto found = std::find_if(rows_.begin(), rows_.end(), [&](const TreatmentLogRow& row) {
        return sameSlot(row, protocol, visit, sequence);
    });
    if (found == rows_.end()) return false;
    found->notes = notes;
    return true;
}

bool TreatmentLog::close(const std::string& protocol, int visit, int sequence,
                         const std::string& endedAt) {
    const auto found = std::find_if(rows_.begin(), rows_.end(), [&](const TreatmentLogRow& row) {
        return sameSlot(row, protocol, visit, sequence);
    });
    if (found == rows_.end()) return false;
    found->endedAt = endedAt;
    return true;
}

// getResponseFromTreatmentProtocolData.m: each score is rounded and clamped to
// [-2, 2], a missing one counts as zero, and the total is their sum.
double TreatmentLog::responseTotal(const TreatmentLogRow& row) {
    const auto score = [](const std::optional<double>& value) {
        if (!value) return 0.0;
        return std::clamp(std::round(*value), -2.0, 2.0);
    };
    return score(row.responsePain) + score(row.responseMood);
}

// getTreatmentProtocolParamString.m's format, which is what BeamV0 stores in
// the Parameters column and setLogInfoToCSV.m parses back out.
std::string TreatmentLog::parameterString(const TreatmentLogRow& row) {
    std::ostringstream text;
    text << "X:" << row.xMm << ",Y:" << row.yMm << ",Z:" << row.zMm
         << ",BD:" << row.burstDurationSeconds << ",BI:" << row.burstIntervalSeconds
         << ",PD:" << row.pulseDurationSeconds << ",PI:" << row.pulseIntervalSeconds;
    return text.str();
}

void TreatmentLog::load(const std::string& path) {
    std::ifstream file(path);
    if (!file) return;  // no deliveries yet
    rows_.clear();

    // By header name, not position: BeamV0's own column set varies by protocol
    // (Response, or ResponsePain and ResponseMood), and a file written by one
    // must not be read as the other.
    std::map<std::string, std::size_t> column;
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string stripped = trimmed(line);
        if (stripped.empty() || stripped[0] == '#') continue;
        const std::vector<std::string> fields = splitRow(stripped);
        if (column.empty()) {
            for (std::size_t i = 0; i < fields.size(); ++i) column.emplace(fields[i], i);
            if (column.count("Number") == 0 || column.count("Name") == 0) {
                throw std::runtime_error(path + ": no Number/Name header; not a treatment log");
            }
            continue;
        }
        const auto text = [&](const char* name) -> std::string {
            const auto found = column.find(name);
            if (found == column.end() || found->second >= fields.size()) return {};
            return fields[found->second];
        };
        const auto number = [&](const char* name, double fallback) {
            const std::string value = text(name);
            return value.empty() ? fallback : toDouble(value, path, name);
        };

        TreatmentLogRow row;
        row.sequence = static_cast<int>(number("Number", 1));
        row.target = text("Name");
        row.protocol = text("Protocol");
        row.visit = static_cast<int>(number("VisitNumber", 1));
        row.xMm = number("X", 0.0);
        row.yMm = number("Y", 0.0);
        row.zMm = number("Z", 0.0);
        row.amplitude = number("Amplitude", 0.5);
        row.burstDurationSeconds = number("BD", 0.03);
        row.burstIntervalSeconds = number("BI", 0.70);
        row.pulseDurationSeconds = number("PD", 0.005);
        row.pulseIntervalSeconds = number("PI", 0.01);
        // Duration is what BeamV0 stores; start and end are how we hold it.
        row.startTimeSeconds = 0.0;
        row.endTimeSeconds = number("Duration", 30.0);
        const std::string sham = text("Sham");
        row.sham = sham == "1" || sham == "Y" || sham == "y" || sham == "true";
        row.startedAt = text("SonicationStartDTM");
        row.endedAt = text("SonicationEndDTM");
        row.responsePain = toOptional(text("ResponsePain"), path, "ResponsePain");
        row.responseMood = toOptional(text("ResponseMood"), path, "ResponseMood");
        row.notes = text("Notes");
        row.attenuation = toOptional(text("AttenuationVal"), path, "AttenuationVal");
        row.dutyCycle = toOptional(text("DC"), path, "DC");
        // Absent in logs written before the column existed: those rows ran to
        // completion as far as anything here knows, so an empty cell on a
        // delivered row reads as 200 rather than as "never ran".
        const std::string status = text("StatusCode");
        if (!status.empty())
            row.statusCode = std::atoi(status.c_str());
        else if (!row.startedAt.empty())
            row.statusCode = kCompleted;
        const std::string fromPlan = text("FromPlan");
        row.fromPlan = fromPlan.empty() || fromPlan == "1" || fromPlan == "true";
        rows_.push_back(std::move(row));
    }
}

void TreatmentLog::save(const std::string& path) const {
    std::ofstream file(path);
    if (!file) throw std::runtime_error("cannot write " + path);
    file << "# BeamAI treatment log, in setLogInfoToCSV.m's column shape: the protocol\n"
         << "# table's own columns, the Parameters string parsed out beside it, and the\n"
         << "# visit/site metadata that function appends.\n"
         << "#\n"
         << "# SonicationStartDTM and SonicationEndDTM are the names BeamV0's own rename\n"
         << "# list gives them; it never fills them, and these are stamped at fire time.\n"
         << "# Sham and Protocol are ours: BeamV0 logs one protocol per file and does not\n"
         << "# record which deliveries were masked.\n"
         << "#\n"
         << "# Blank CRF columns are fields BeamAI does not collect yet. They are written\n"
         << "# so the column set matches rather than silently differing.\n"
         << "Number,Name,Duration,Amplitude,Parameters,ResponsePain,ResponseMood,ResponseTotal,"
            "Notes,X,Y,Z,BD,BI,PD,PI,VisitNumber,SubjectID,SiteID,SessionDate,BEAMSerialNumber,"
            "Transducer1SerialNumber,Transducer2SerialNumber,SoftwareVersion,Region,"
            "HardwareSerialNumber,AttenuationVal,DC,SonicationStartDTM,SonicationEndDTM,Sham,"
            "Protocol,FromPlan,StatusCode\n";
    for (const TreatmentLogRow& row : rows_) {
        file << row.sequence << ',' << quote(row.target) << ','
             << (row.endTimeSeconds - row.startTimeSeconds) << ',' << row.amplitude << ','
             << quote(parameterString(row)) << ',';
        if (row.responsePain) file << *row.responsePain;
        file << ',';
        if (row.responseMood) file << *row.responseMood;
        file << ',' << responseTotal(row) << ',' << quote(row.notes) << ',' << row.xMm << ','
             << row.yMm << ',' << row.zMm << ',' << row.burstDurationSeconds << ','
             << row.burstIntervalSeconds << ',' << row.pulseDurationSeconds << ','
             << row.pulseIntervalSeconds << ',' << row.visit
             // SubjectID, SiteID, SessionDate, three serials, SoftwareVersion
             << ",,,,,,,"
             // Region is BeamV0's own copy of Name.
             << ',' << quote(row.target)
             // HardwareSerialNumber, which nothing here knows.
             << ',';
        // setLogInfoToCSV.m fills these from app.sys.log{i}.att and .DC -- the
        // correction values the manual says are logged for every sonication.
        file << ',';
        if (row.attenuation) file << *row.attenuation;
        file << ',';
        if (row.dutyCycle) file << *row.dutyCycle;
        file << ',' << quote(row.startedAt) << ',' << quote(row.endedAt) << ','
             << (row.sham ? 1 : 0) << ',' << quote(row.protocol) << ','
             << (row.fromPlan ? 1 : 0) << ',' << row.statusCode << '\n';
    }
    if (!file) throw std::runtime_error("failed while writing " + path);
}

}  // namespace beam::gui
