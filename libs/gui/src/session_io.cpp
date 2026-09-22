#include "gui/session_io.hpp"

#include <sstream>
#include <stdexcept>

namespace beam::gui {

namespace {

constexpr const char* kVersionLine = "# Beam session v1";

std::vector<std::string> splitTab(const std::string& line) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (true) {
        const std::size_t tab = line.find('\t', start);
        if (tab == std::string::npos) {
            out.push_back(line.substr(start));
            break;
        }
        out.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
    return out;
}

std::string joinTab(const std::vector<std::string>& fields) {
    std::string out;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i != 0) out += '\t';
        out += fields[i];
    }
    return out;
}

// Enough significant digits to round-trip a double exactly.
std::string toStr(double v) {
    std::ostringstream ss;
    ss.precision(17);
    ss << v;
    return ss.str();
}
std::string toStr(int v) { return std::to_string(v); }
std::string toStr(bool v) { return v ? "1" : "0"; }

double toDouble(const std::string& s, const char* field) {
    try {
        return std::stod(s);
    } catch (const std::exception&) {
        throw std::runtime_error(std::string("session file: bad number for ") + field + ": \"" + s + "\"");
    }
}
int toInt(const std::string& s, const char* field) {
    try {
        return std::stoi(s);
    } catch (const std::exception&) {
        throw std::runtime_error(std::string("session file: bad integer for ") + field + ": \"" + s + "\"");
    }
}
bool toBool(const std::string& s) { return s == "1"; }

void expectColumns(const std::vector<std::string>& row, std::size_t n, const char* section) {
    if (row.size() != n) {
        throw std::runtime_error(std::string("session file: [") + section + "] row has " +
                                  std::to_string(row.size()) + " columns, expected " + std::to_string(n));
    }
}

}  // namespace

std::string serializeSession(const SessionData& data) {
    std::ostringstream out;
    out << kVersionLine << "\n";

    out << "[Fiducials]\n";
    out << "name\tx\ty\tz\n";
    for (const beam::registration::FiducialMarker& m : data.fiducials) {
        out << joinTab({m.name, toStr(m.position.x()), toStr(m.position.y()), toStr(m.position.z())}) << "\n";
    }

    out << "[StimParams]\n";
    out << "order\tshow\tx\ty\tz\tamplitude\tstartTime\tendTime\tbd\tbi\tpd\tpi\n";
    for (const StimParamRecord& r : data.stimParams) {
        out << joinTab({toStr(r.order), toStr(r.show), toStr(r.x), toStr(r.y), toStr(r.z), toStr(r.amplitude),
                        toStr(r.startTime), toStr(r.endTime), toStr(r.bd), toStr(r.bi), toStr(r.pd), toStr(r.pi)})
            << "\n";
    }

    out << "[TreatmentProtocol]\n";
    out << "number\ttarget\tduration\tamplitude\tparameters\tpain\tmood\tnotes\n";
    for (const TreatmentProtocolRecord& r : data.treatmentProtocol) {
        out << joinTab({toStr(r.number), r.target, toStr(r.duration), toStr(r.amplitude), r.parameters,
                        toStr(r.pain), toStr(r.mood), r.notes})
            << "\n";
    }

    return out.str();
}

SessionData deserializeSession(const std::string& text) {
    std::vector<std::string> lines;
    {
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();  // tolerate CRLF
            lines.push_back(line);
        }
    }
    if (lines.empty() || lines[0] != kVersionLine) {
        throw std::runtime_error("session file: missing or unrecognized version line");
    }

    SessionData data;
    std::string section;
    bool expectHeader = false;
    for (std::size_t i = 1; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        if (line.empty()) continue;
        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            expectHeader = true;
            continue;
        }
        if (expectHeader) {
            expectHeader = false;  // header line itself is not data -- skip it
            continue;
        }
        const std::vector<std::string> row = splitTab(line);
        if (section == "Fiducials") {
            expectColumns(row, 4, "Fiducials");
            beam::registration::FiducialMarker m;
            m.name = row[0];
            m.position = Eigen::Vector3d(toDouble(row[1], "Fiducials.x"), toDouble(row[2], "Fiducials.y"),
                                          toDouble(row[3], "Fiducials.z"));
            data.fiducials.push_back(std::move(m));
        } else if (section == "StimParams") {
            expectColumns(row, 12, "StimParams");
            StimParamRecord r;
            r.order = toInt(row[0], "StimParams.order");
            r.show = toBool(row[1]);
            r.x = toDouble(row[2], "StimParams.x");
            r.y = toDouble(row[3], "StimParams.y");
            r.z = toDouble(row[4], "StimParams.z");
            r.amplitude = toDouble(row[5], "StimParams.amplitude");
            r.startTime = toDouble(row[6], "StimParams.startTime");
            r.endTime = toDouble(row[7], "StimParams.endTime");
            r.bd = toDouble(row[8], "StimParams.bd");
            r.bi = toDouble(row[9], "StimParams.bi");
            r.pd = toDouble(row[10], "StimParams.pd");
            r.pi = toDouble(row[11], "StimParams.pi");
            data.stimParams.push_back(r);
        } else if (section == "TreatmentProtocol") {
            expectColumns(row, 8, "TreatmentProtocol");
            TreatmentProtocolRecord r;
            r.number = toInt(row[0], "TreatmentProtocol.number");
            r.target = row[1];
            r.duration = toDouble(row[2], "TreatmentProtocol.duration");
            r.amplitude = toDouble(row[3], "TreatmentProtocol.amplitude");
            r.parameters = row[4];
            r.pain = toDouble(row[5], "TreatmentProtocol.pain");
            r.mood = toDouble(row[6], "TreatmentProtocol.mood");
            r.notes = row[7];
            data.treatmentProtocol.push_back(r);
        }
        // Unknown section: skip its rows silently (forward compatibility).
    }
    return data;
}

}  // namespace beam::gui
