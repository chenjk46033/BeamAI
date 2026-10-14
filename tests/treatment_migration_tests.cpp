// Checks data/*.csv against what BeamV0 actually runs with: sys.protocolTables
// and sys.treatmentProtocolTables out of defaultSubjectMNIV1.mat, exported to
// testdata/beamv0_protocol_tables.csv and encoded in kVerifiedRows below.
//
// Not the BEAM/.../TreatmentProtocols/*.csv files: initTreatmentProtocolTables.m
// takes sys's own treatmentProtocolTables when the loaded session has them,
// which the default subject does, so BeamV0 reads no CSV at startup. Those
// files also carry three protocols -- MDD, PainACCTwoTargets, PainVPL -- that
// have no entry in sys and so never reach BeamV0's dropdown. They are still
// read here, by the two tests that exist to say how the CSVs and sys differ.
//
// See docs/known_gaps_treatment.md.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "gui/treatment_target_store.hpp"
#include "infra_mat/legacy_beam_session.hpp"

namespace {

struct SourceEntry {
    std::string protocol;
    int order = 0;  // sequential position, not BeamV0's Number column
    std::string target;
    std::string duration;
    std::string amplitude;
    std::string parameters;
};

std::string trim(std::string text) {
    const auto notSpace = [](unsigned char c) { return std::isspace(c) == 0; };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), notSpace));
    text.erase(std::find_if(text.rbegin(), text.rend(), notSpace).base(), text.end());
    return text;
}

// BeamV0's files are inconsistent: Addiction.csv is tab-separated, the rest
// comma-separated, and the Parameters column is a quoted field containing
// commas. Pick the delimiter per line from what appears outside quotes.
std::vector<std::string> splitRow(const std::string& line) {
    bool quoted = false;
    char delimiter = ',';
    for (const char c : line) {
        if (c == '"') quoted = !quoted;
        else if (c == '\t' && !quoted) { delimiter = '\t'; break; }
    }
    std::vector<std::string> fields;
    std::string field;
    quoted = false;
    for (const char c : line) {
        if (c == '"') {
            quoted = !quoted;
        } else if (c == delimiter && !quoted) {
            fields.push_back(trim(field));
            field.clear();
        } else {
            field += c;
        }
    }
    fields.push_back(trim(field));
    return fields;
}

std::vector<SourceEntry> readBeamV0Protocols() {
    std::vector<SourceEntry> entries;
    const std::filesystem::path directory(BEAM_V0_PROTOCOL_DIR);
    std::error_code ignored;
    if (!std::filesystem::is_directory(directory, ignored)) return entries;

    std::vector<std::filesystem::path> files;
    for (const auto& item : std::filesystem::directory_iterator(directory)) {
        if (item.path().extension() == ".csv") files.push_back(item.path());
    }
    std::sort(files.begin(), files.end());

    for (const std::filesystem::path& file : files) {
        std::ifstream stream(file);
        if (!stream) continue;
        const std::string protocol = file.stem().string();
        int order = 0;
        std::string line;
        while (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            // Most of these files carry a UTF-8 BOM, which ifstream keeps --
            // leaving it on makes the header row parse as data.
            if (line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);
            const std::string stripped = trim(line);
            if (stripped.empty()) continue;
            const std::vector<std::string> fields = splitRow(stripped);
            if (fields.size() < 5) continue;
            if (fields[0] == "Number") continue;      // header
            if (fields[1].empty()) continue;          // ",,,,,," block separator
            SourceEntry entry;
            entry.protocol = protocol;
            entry.order = ++order;
            entry.target = fields[1];
            entry.duration = fields[2];
            entry.amplitude = fields[3];
            entry.parameters = fields[4];
            entries.push_back(std::move(entry));
        }
    }
    return entries;
}

// The six protocols BeamV0 loads, from the checked-in export. Columns are
// protocol,order,target,amplitude,duration,parameters -- amplitude before
// duration, the opposite of the BEAM CSVs.
std::vector<SourceEntry> readBeamV0ProtocolTables() {
    std::vector<SourceEntry> entries;
    std::ifstream stream(BEAM_V0_PROTOCOL_TABLES_CSV);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string stripped = trim(line);
        if (stripped.empty() || stripped[0] == '#') continue;
        const std::vector<std::string> fields = splitRow(stripped);
        if (fields.size() < 6 || fields[0] == "protocol") continue;
        SourceEntry entry;
        entry.protocol = fields[0];
        entry.order = std::stoi(fields[1]);
        entry.target = fields[2];
        entry.amplitude = fields[3];
        entry.duration = fields[4];
        entry.parameters = fields[5];
        entries.push_back(std::move(entry));
    }
    return entries;
}

bool sameNumber(const std::string& text, double value) {

    try {
        return std::abs(std::stod(text) - value) < 1e-9;
    } catch (const std::exception&) {
        return false;
    }
}

beam::gui::TreatmentTargetStore shippedStore() {
    beam::gui::TreatmentTargetStore store;
    store.loadTargets(BEAM_TREATMENT_TARGETS_CSV);
    store.loadProtocols(BEAM_TREATMENT_PROTOCOLS_CSV);
    return store;
}

}  // namespace

TEST(TreatmentMigration, ProtocolsMatchBeamV0EntryForEntry) {
    const std::vector<SourceEntry> source = readBeamV0ProtocolTables();
    ASSERT_EQ(source.size(), 146u) << "testdata/beamv0_protocol_tables.csv is the export";

    const beam::gui::TreatmentTargetStore store = shippedStore();

    // Same protocols, same count.
    std::set<std::string> sourceProtocols;
    for (const SourceEntry& entry : source) sourceProtocols.insert(entry.protocol);
    std::set<std::string> ourProtocols;
    for (const beam::gui::TreatmentProtocolDefinition& definition : store.protocols())
        ourProtocols.insert(definition.name);
    EXPECT_EQ(ourProtocols, sourceProtocols);

    std::size_t ourEntryCount = 0;
    for (const beam::gui::TreatmentProtocolDefinition& definition : store.protocols())
        ourEntryCount += definition.entries.size();
    EXPECT_EQ(ourEntryCount, source.size());

    // Then every entry, in order.
    for (const SourceEntry& expected : source) {
        SCOPED_TRACE(expected.protocol + " entry " + std::to_string(expected.order) + " (" +
                     expected.target + ")");
        const beam::gui::TreatmentProtocolDefinition* definition =
            store.findProtocol(expected.protocol);
        ASSERT_NE(definition, nullptr);
        ASSERT_GE(static_cast<int>(definition->entries.size()), expected.order);
        const beam::gui::TreatmentProtocolEntry& actual =
            definition->entries[static_cast<std::size_t>(expected.order - 1)];
        EXPECT_EQ(actual.order, expected.order);
        EXPECT_EQ(actual.target, expected.target);
        ASSERT_TRUE(actual.amplitude.has_value());
        ASSERT_TRUE(actual.durationSeconds.has_value());
        EXPECT_TRUE(sameNumber(expected.amplitude, *actual.amplitude))
            << "amplitude " << expected.amplitude << " vs " << *actual.amplitude;
        EXPECT_TRUE(sameNumber(expected.duration, *actual.durationSeconds))
            << "duration " << expected.duration << " vs " << *actual.durationSeconds;
    }
}

// The BEAM protocol CSVs carry one parameter string for all 208 entries. That
// is now evidence that they are NOT the source of the per-target parameters,
// because BeamV0's own grid shows those varying per target -- SCC2's burst and
// pulse values differ from SCC1's. Kept for exactly that reason.
TEST(TreatmentMigration, TheProtocolCsvsCarryOneParameterStringForEveryEntry) {
    const std::vector<SourceEntry> source = readBeamV0Protocols();
    if (source.empty()) GTEST_SKIP() << "needs the BeamV0 checkout at " << BEAM_V0_PROTOCOL_DIR;

    std::set<std::string> distinct;
    for (const SourceEntry& entry : source) distinct.insert(entry.parameters);
    ASSERT_EQ(distinct.size(), 1u) << "the BEAM CSVs now have more than one parameter string";
    EXPECT_EQ(*distinct.begin(), "X:0,Y:0,Z:0,0.03,0.7,0.005,0.010");
}

// All twelve, read out of sys.protocolTables(i).stimParamTableData in
// defaultSubjectMNIV1.mat -- each target's own stored grid, which is what
// BeamV0 displays when the target is selected. This replaces reading three of
// them off the running GUI, which is also where SCC2's PD was misread as 0.05
// (a 500% duty cycle against PI 0.01); the stored table says 0.005.
//
// The per-target parameters are NOT the protocol CSVs' -- those carry one
// parameter string for all 208 entries, while BD and BI here cycle
// 0.03/0.7, 0.06/1.1, 0.09/1.4 every three targets. They are not uniformly
// createStimParamTable.m's defaults either: that is the 0.03/0.7/0.005/0.01
// row, which only four of the twelve carry.
//
// amplitude, startTime and endTime are uniform across the twelve and equal
// both the defaults and what PainACC schedules, so these rows cannot show
// whether a schedule writes amplitude. The source code can: its one call site,
// setStimulationAmplitudeFromResponses, is commented out.
namespace {

struct VerifiedRow {
    const char* name;
    double amplitude, startTime, endTime, bd, bi;
    double pd;
    double pi;
};

// SCC3 is the one target whose pulse values differ from every other: PD 0.001
// against PI 0.005, where the rest carry 0.005 against 0.01.
const VerifiedRow kVerifiedRows[] = {
    {"SCC1", 0.5, 0.0, 30.0, 0.03, 0.70, 0.005, 0.01},
    {"SCC2", 0.5, 0.0, 30.0, 0.06, 1.10, 0.005, 0.01},
    {"SCC3", 0.5, 0.0, 30.0, 0.09, 1.40, 0.001, 0.005},
    {"SCC4", 0.5, 0.0, 30.0, 0.03, 0.70, 0.005, 0.01},
    {"SCC5", 0.5, 0.0, 30.0, 0.06, 1.10, 0.005, 0.01},
    {"SCC6", 0.5, 0.0, 30.0, 0.09, 1.40, 0.005, 0.01},
    {"aMCC1", 0.5, 0.0, 30.0, 0.03, 0.70, 0.005, 0.01},
    {"aMCC2", 0.5, 0.0, 30.0, 0.06, 1.10, 0.005, 0.01},
    {"aMCC3", 0.5, 0.0, 30.0, 0.09, 1.40, 0.005, 0.01},
    {"aMCC4", 0.5, 0.0, 30.0, 0.03, 0.70, 0.005, 0.01},
    {"aMCC5", 0.5, 0.0, 30.0, 0.06, 1.10, 0.005, 0.01},
    {"aMCC6", 0.5, 0.0, 30.0, 0.09, 1.40, 0.005, 0.01},
};

}  // namespace

TEST(TreatmentMigration, EveryTargetMatchesBeamV0sStoredStimParamGrid) {
    const beam::gui::TreatmentTargetStore store = shippedStore();
    ASSERT_EQ(store.targets().size(), 12u);

    for (const VerifiedRow& expected : kVerifiedRows) {
        SCOPED_TRACE(expected.name);
        const beam::gui::TreatmentTarget* target = store.findTarget(expected.name);
        ASSERT_NE(target, nullptr);
        ASSERT_EQ(target->points.size(), 1u) << "the seed is createStimParamTable.m's N = 1";
        const beam::gui::TreatmentTargetPoint& point = target->points.front();
        EXPECT_EQ(point.order, 1);
        EXPECT_TRUE(point.show);
        EXPECT_DOUBLE_EQ(point.amplitude, expected.amplitude);
        EXPECT_DOUBLE_EQ(point.startTimeSeconds, expected.startTime);
        EXPECT_DOUBLE_EQ(point.endTimeSeconds, expected.endTime);
        EXPECT_DOUBLE_EQ(point.burstDurationSeconds, expected.bd);
        EXPECT_DOUBLE_EQ(point.burstIntervalSeconds, expected.bi);
        EXPECT_DOUBLE_EQ(point.pulseDurationSeconds, expected.pd);
        EXPECT_DOUBLE_EQ(point.pulseIntervalSeconds, expected.pi);
    }

    EXPECT_EQ(std::size(kVerifiedRows), store.targets().size())
        << "a target has no row above; see docs/known_gaps_treatment.md";
}

// BeamV0 ships protocols that schedule targets its own Target List does not
// define: T1-T6 and four NACC/Nacc spellings, ten of the 22 names the six
// protocols between them name. Default is one of them -- its rows 15-24 are
// T1/T2/T3. So the dangling warning BeamAI raises is reporting BeamV0's own
// inconsistency, not a migration error; BeamV0 just does not warn.
TEST(TreatmentMigration, BeamV0sOwnProtocolsScheduleTenTargetsItNeverDefines) {
    const std::vector<SourceEntry> source = readBeamV0ProtocolTables();
    std::set<std::string> scheduled;
    for (const SourceEntry& entry : source) scheduled.insert(entry.target);
    EXPECT_EQ(scheduled.size(), 22u);

    const beam::gui::TreatmentTargetStore store = shippedStore();
    std::vector<std::string> notInTheTargetList;
    for (const std::string& name : scheduled)
        if (store.findTarget(name) == nullptr) notInTheTargetList.push_back(name);

    EXPECT_EQ(notInTheTargetList.size(), 10u)
        << "first is " << (notInTheTargetList.empty() ? std::string("none") : notInTheTargetList.front());
    EXPECT_EQ(store.danglingTargetNames().size(), notInTheTargetList.size());
}

// The three the CSVs carry and sys does not. They are deliberately absent from
// data/treatment_protocols.csv: BeamV0 cannot reach them, so neither should we.
TEST(TreatmentMigration, TheCsvOnlyProtocolsAreNotMirrored) {
    const std::vector<SourceEntry> csvs = readBeamV0Protocols();
    if (csvs.empty()) GTEST_SKIP() << "needs the BeamV0 checkout at " << BEAM_V0_PROTOCOL_DIR;

    std::set<std::string> csvProtocols;
    for (const SourceEntry& entry : csvs) csvProtocols.insert(entry.protocol);
    std::set<std::string> loaded;
    for (const SourceEntry& entry : readBeamV0ProtocolTables()) loaded.insert(entry.protocol);

    std::vector<std::string> csvOnly;
    std::set_difference(csvProtocols.begin(), csvProtocols.end(), loaded.begin(), loaded.end(),
                        std::back_inserter(csvOnly));
    EXPECT_EQ(csvOnly, (std::vector<std::string>{"MDD", "PainACCTwoTargets", "PainVPL"}));

    const beam::gui::TreatmentTargetStore store = shippedStore();
    for (const std::string& name : csvOnly) EXPECT_EQ(store.findProtocol(name), nullptr) << name;
    EXPECT_EQ(store.protocols().size(), loaded.size());
}

// The Target List BeamV0 actually carries, read from a session's own sys
// rather than from the CSVs. protocolTables is keyed by Target List position
// (setProtocolTableWithStimParamTable.m), so these names in this order ARE
// the list -- and they are the same twelve in the shipped default subject, in
// a real BeamV0 session, and in a Diadem one.
//
// data/treatment_targets.csv carries 29 names because it was built from the
// BEAM protocol CSVs, which schedule targets the Target List does not define.
// This test is what says which of the two is the reference.
TEST(TreatmentMigration, TheAuthoritativeTargetListIsBeamV0sTwelve) {
    if (!std::filesystem::exists(BEAM_DEFAULT_SUBJECT_MAT))
        GTEST_SKIP() << "needs the default subject at " << BEAM_DEFAULT_SUBJECT_MAT;

    const beam::infra::mat::LegacyBeamPlan plan =
        beam::infra::mat::loadLegacyBeamPlan(BEAM_DEFAULT_SUBJECT_MAT);

    const std::vector<std::string> expected = {"SCC1",  "SCC2",  "SCC3",  "SCC4",
                                               "SCC5",  "SCC6",  "aMCC1", "aMCC2",
                                               "aMCC3", "aMCC4", "aMCC5", "aMCC6"};
    EXPECT_EQ(plan.targetNames, expected);

    // Six protocols, not the nine CSV filenames: MDD, PainACCTwoTargets and
    // PainVPL have a CSV but no entry in the session BeamV0 loads.
    std::vector<std::string> protocolNames;
    for (const beam::infra::mat::LegacyBeamPlan::Protocol& protocol : plan.protocols)
        protocolNames.push_back(protocol.name);
    EXPECT_EQ(protocolNames,
              (std::vector<std::string>{"PainSCCandAMCC", "PainAMCCandSCC", "PainACC", "PTSD",
                                        "Addiction", "Default"}));

    // app.maxTreatmentSessions, corroborating TreatmentSessionStore's cap.
    for (const beam::infra::mat::LegacyBeamPlan::Protocol& protocol : plan.protocols)
        EXPECT_EQ(protocol.sessionCount, 15u) << protocol.name;

    // And the reason the values still need a MATLAB export: the payloads are
    // table objects, so this file gives the index and nothing more.
    EXPECT_TRUE(plan.tablePayloadsUnreadable);
}
