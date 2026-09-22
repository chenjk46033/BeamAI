#include "gui/treatment_session_store.hpp"

#include <algorithm>

namespace beam::gui {

std::vector<TreatmentProtocol> initTreatmentProtocols(const std::vector<std::string>& protocolNames,
                                                       const std::vector<TreatmentProtocolRecord>& blankRows) {
    std::vector<TreatmentProtocol> protocols;
    protocols.reserve(protocolNames.size());
    for (const std::string& name : protocolNames) {
        TreatmentProtocol protocol;
        protocol.name = name;
        protocol.sessions.push_back(TreatmentSession{1, blankRows});
        protocols.push_back(std::move(protocol));
    }
    return protocols;
}

void addTreatmentProtocolSession(TreatmentProtocol& protocol, const std::vector<TreatmentProtocolRecord>& blankRows,
                                  int maxSessions) {
    if (static_cast<int>(protocol.sessions.size()) > maxSessions) {
        return;
    }
    int highestVisit = 0;
    for (const TreatmentSession& s : protocol.sessions) {
        highestVisit = std::max(highestVisit, s.visitNumber);
    }
    protocol.sessions.push_back(TreatmentSession{highestVisit + 1, blankRows});
}

TreatmentProtocol& findProtocol(std::vector<TreatmentProtocol>& protocols, const std::string& protocolName) {
    for (TreatmentProtocol& p : protocols) {
        if (p.name == protocolName) {
            return p;
        }
    }
    throw std::out_of_range("findProtocol: no protocol named '" + protocolName + "'");
}

TreatmentSession& findSession(TreatmentProtocol& protocol, int visitNumber) {
    for (TreatmentSession& s : protocol.sessions) {
        if (s.visitNumber == visitNumber) {
            return s;
        }
    }
    throw std::out_of_range("findSession: protocol '" + protocol.name + "' has no visit " +
                             std::to_string(visitNumber));
}

const std::vector<TreatmentProtocolRecord>& currentSessionRows(std::vector<TreatmentProtocol>& protocols,
                                                                 const std::string& protocolName, int visitNumber) {
    return findSession(findProtocol(protocols, protocolName), visitNumber).rows;
}

void setCurrentSessionRows(std::vector<TreatmentProtocol>& protocols, const std::string& protocolName,
                            int visitNumber, std::vector<TreatmentProtocolRecord> rows) {
    findSession(findProtocol(protocols, protocolName), visitNumber).rows = std::move(rows);
}

}  // namespace beam::gui
