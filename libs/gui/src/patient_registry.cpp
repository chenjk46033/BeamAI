#include "gui/patient_registry.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <map>
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

// An identifier an operator typed can hold a comma, so every field is quoted
// on the way out and unquoted on the way back.
std::string quote(const std::string& text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"') out += '"';
        out += c;
    }
    out += '"';
    return out;
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

// ISO-8601 local time to the second. libs/gui carries no Qt, so this is the
// standard library's own.
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

}  // namespace

void PatientRegistry::load(const std::string& path) {
    std::ifstream file(path);
    // No file is a device nobody has used yet, which is a legitimate state
    // and not something to stop for.
    if (!file) {
        patients_.clear();
        return;
    }

    // Built aside and swapped in only once the whole file has parsed. A throw
    // part-way through must not leave a registry holding the rows that came
    // before it -- that is the chooser quietly missing people.
    std::vector<Patient> loaded;
    // By header name, not position: demographics were added after the first
    // devices were in use, and a four-column file written before them has to
    // go on loading rather than being rejected as malformed.
    std::map<std::string, std::size_t> column;
    // The header's own field count, so a row that does not match it is still
    // refused. Reading by name must not turn a truncated row into one with
    // silently empty fields.
    std::size_t headerFields = 0;
    std::string line;
    int lineNumber = 0;
    int nextId = 1;
    while (std::getline(file, line)) {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string stripped = trimmed(line);
        if (stripped.empty() || stripped.front() == '#') {
            // The one comment that is data: how far the numbering has got.
            const std::string marker = "# next-id:";
            if (stripped.rfind(marker, 0) == 0) {
                try {
                    nextId = std::max(nextId, std::stoi(stripped.substr(marker.size())));
                } catch (const std::exception&) {
                    // An unreadable marker falls back to the rows below.
                }
            }
            continue;
        }
        const std::vector<std::string> fields = splitRow(stripped);
        if (column.empty()) {
            for (std::size_t i = 0; i < fields.size(); ++i) column.emplace(fields[i], i);
            if (column.count("id") == 0 || column.count("externalId") == 0) {
                throw std::runtime_error(path + ": no id/externalId header; not a patient list");
            }
            headerFields = fields.size();
            continue;
        }
        if (fields.size() != headerFields) {
            throw std::runtime_error(path + ":" + std::to_string(lineNumber) + ": expected " +
                                     std::to_string(headerFields) + " fields, got " +
                                     std::to_string(fields.size()));
        }
        const auto text = [&](const char* name) -> std::string {
            const auto found = column.find(name);
            if (found == column.end() || found->second >= fields.size()) return {};
            return fields[found->second];
        };

        Patient patient;
        const std::string id = text("id");
        try {
            patient.id = std::stoi(id);
        } catch (const std::exception&) {
            throw std::runtime_error(path + ":" + std::to_string(lineNumber) +
                                     ": id is not a number: \"" + id + "\"");
        }
        if (patient.id <= 0) {
            throw std::runtime_error(path + ":" + std::to_string(lineNumber) +
                                     ": id must be positive");
        }
        patient.externalId = text("externalId");
        patient.site = text("site");
        patient.lastName = text("lastName");
        patient.firstName = text("firstName");
        patient.dateOfBirth = text("dateOfBirth");
        patient.createdAt = text("createdAt");
        if (patient.externalId.empty()) {
            throw std::runtime_error(path + ":" + std::to_string(lineNumber) +
                                     ": a patient with no identifier cannot be told apart");
        }
        // Two rows claiming the same person, or the same id, means the file
        // has been edited into a state where lookups would answer differently
        // depending on which one they found first.
        for (const Patient& already : loaded) {
            if (already.id == patient.id) {
                throw std::runtime_error(path + ":" + std::to_string(lineNumber) +
                                         ": id " + std::to_string(patient.id) + " appears twice");
            }
            if (already.externalId == patient.externalId) {
                throw std::runtime_error(path + ":" + std::to_string(lineNumber) + ": \"" +
                                         patient.externalId + "\" appears twice");
            }
        }
        loaded.push_back(std::move(patient));
    }
    // A file written before the marker existed has none; the rows are then
    // the only evidence of how far numbering got.
    for (const Patient& patient : loaded) nextId = std::max(nextId, patient.id + 1);
    patients_ = std::move(loaded);
    nextId_ = nextId;
}

void PatientRegistry::save(const std::string& path) const {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) throw std::runtime_error("cannot write " + path);
    file << "# Who this device has treated. id names the directory their visits\n"
            "# live in, so it is never reused or renumbered; externalId is the\n"
            "# operator's own Participant ID and is what appears on a CRF.\n";
    // Carried rather than derived from the rows below, so that removing a
    // patient does not release their number for the next one.
    int nextId = nextId_;
    for (const Patient& patient : patients_) nextId = std::max(nextId, patient.id + 1);
    file << "# next-id: " << nextId << "\n";
    file << "id,externalId,site,lastName,firstName,dateOfBirth,createdAt\n";
    for (const Patient& patient : patients_) {
        file << patient.id << ',' << quote(patient.externalId) << ',' << quote(patient.site) << ','
             << quote(patient.lastName) << ',' << quote(patient.firstName) << ','
             << quote(patient.dateOfBirth) << ',' << quote(patient.createdAt) << '\n';
    }
    if (!file) throw std::runtime_error("could not finish writing " + path);
}

// "Last, First" when there is a name, the Participant ID when there is not.
std::string patientDisplayName(const Patient& patient) {
    const std::string last = trimmed(patient.lastName);
    const std::string first = trimmed(patient.firstName);
    if (!last.empty() && !first.empty()) return last + ", " + first;
    if (!last.empty()) return last;
    if (!first.empty()) return first;
    return patient.externalId;
}

Patient PatientRegistry::add(const Patient& details) {
    if (trimmed(details.externalId).empty())
        throw std::runtime_error("a patient needs an identifier");

    for (const Patient& already : patients_) {
        if (already.externalId != details.externalId) continue;
        // Returning the existing record UNCHANGED is what makes "open the
        // patient I am about to treat" safe to call on every visit.
        //
        // It once took whatever was filled in, so details could be added on a
        // later visit. That let a half-finished form rewrite somebody else:
        // type a new patient's name, correct the Participant ID to one
        // already on file, accept, and the person who owned that ID silently
        // became the name left in the boxes. Changing who somebody is must be
        // something the operator asks for, which is what update() is.
        return already;
    }

    Patient added = details;
    // One past the highest EVER issued, which is not the same as one past the
    // highest still present: a removed patient's number must not be handed to
    // the next person, or they inherit the directory that was theirs.
    int highest = 0;
    for (const Patient& already : patients_) highest = std::max(highest, already.id);
    added.id = std::max(nextId_, highest + 1);
    nextId_ = added.id + 1;
    added.createdAt = currentTimestamp();
    patients_.push_back(added);
    return added;
}

Patient PatientRegistry::add(const std::string& externalId, const std::string& site) {
    Patient details;
    details.externalId = externalId;
    details.site = site;
    return add(details);
}

bool PatientRegistry::find(int id, Patient* out) const {
    const auto found = std::find_if(patients_.begin(), patients_.end(),
                                    [&](const Patient& p) { return p.id == id; });
    if (found == patients_.end()) return false;
    if (out != nullptr) *out = *found;
    return true;
}

bool PatientRegistry::findByExternalId(const std::string& externalId, Patient* out) const {
    const auto found = std::find_if(patients_.begin(), patients_.end(),
                                    [&](const Patient& p) { return p.externalId == externalId; });
    if (found == patients_.end()) return false;
    if (out != nullptr) *out = *found;
    return true;
}

bool PatientRegistry::remove(int id) {
    const auto found = std::find_if(patients_.begin(), patients_.end(),
                                    [&](const Patient& p) { return p.id == id; });
    if (found == patients_.end()) return false;
    patients_.erase(found);
    // add() takes one past the highest id ever seen, not one past the count,
    // so nothing here has to defend the gap this leaves.
    return true;
}

bool PatientRegistry::update(int id, const Patient& details) {
    if (trimmed(details.externalId).empty()) return false;
    const auto clash = std::find_if(patients_.begin(), patients_.end(), [&](const Patient& p) {
        return p.externalId == details.externalId && p.id != id;
    });
    if (clash != patients_.end()) return false;

    const auto found = std::find_if(patients_.begin(), patients_.end(),
                                    [&](const Patient& p) { return p.id == id; });
    if (found == patients_.end()) return false;
    found->externalId = details.externalId;
    found->site = details.site;
    // Taken as given, empty included: this is the editor, and a name typed by
    // mistake has to be removable.
    found->lastName = details.lastName;
    found->firstName = details.firstName;
    found->dateOfBirth = details.dateOfBirth;
    return true;
}

bool PatientRegistry::rename(int id, const std::string& externalId, const std::string& site) {
    Patient details;
    if (!find(id, &details)) return false;
    details.externalId = externalId;
    details.site = site;
    return update(id, details);
}

}  // namespace beam::gui
