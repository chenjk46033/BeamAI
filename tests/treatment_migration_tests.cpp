// Checks data/*.csv against BeamV0's BEAM/.../TreatmentProtocols/*.csv.
//
// IMPORTANT, AND THE REASON THIS IS NOT YET A PARITY TEST: those CSVs are not
// what BeamV0 runs with. initTreatmentProtocolTables.m takes sys's own
// treatmentProtocolTables when the loaded session has them, which
// defaultSubjectMNIV1.mat does -- so BeamV0 reads no CSV at startup. Its
// fallback, getTreatmentProtocolTable.m, resolves what('Diadem\...') to the
// sibling DiademV0 checkout, whose files differ from these and cover only 6
// of the 9 protocols.
//
// So this proves the migration transcribed the BEAM CSVs faithfully, and
// nothing more. The authoritative target parameters live in
// sys.protocolTables / sys.treatmentProtocolTables and are NOT yet compared.
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
    const std::vector<SourceEntry> source = readBeamV0Protocols();
    if (source.empty()) GTEST_SKIP() << "needs the BeamV0 checkout at " << BEAM_V0_PROTOCOL_DIR;

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

// What has actually been checked against BeamV0, row by row, by selecting the
// target in BeamV0's Target List (protocol PainACC) and reading its
// stimParamTable grid. Everything else in data/treatment_targets.csv is still
// a guess, and kUnverifiedTargets below is what says so.
//
// The per-target parameters are NOT the protocol CSVs' -- those carry one
// parameter string for all 208 entries, while SCC2's burst and pulse values
// differ from SCC1's. They are also not uniformly
// createStimParamTable.m's defaults: SCC1 and SCC4 happen to match it and SCC2
// does not.
//
// BD, BI, PD and PI are the trustworthy columns here: no write path touches
// them, so they can only be the stored table's own. amplitude and endTime are
// both confounded under PainACC, which schedules every target at 0.5 / 30 --
// the same values observed. endTime is genuinely written from the schedule
// (updateSonicateSettingsForCurrentSonication.m), and while no live code
// writes amplitude from it, these readings cannot show that on their own. A
// protocol that schedules a different amplitude (Default has SCC1 at 0.75)
// would separate the two.
namespace {

struct VerifiedRow {
    const char* name;
    double amplitude, startTime, endTime, bd, bi;
    // Nullopt where the reading still needs confirming.
    std::optional<double> pd;
    double pi;
};

// SCC2's PD read as 0.05 against PI 0.01 -- a 500% pulse duty cycle, which
// getISPTAFromStimParams.m would turn into a nonsense intensity. Held back
// until re-read rather than encoded.
const VerifiedRow kVerifiedRows[] = {
    {"SCC1", 0.5, 0.0, 30.0, 0.03, 0.70, 0.005, 0.01},
    {"SCC2", 0.5, 0.0, 30.0, 0.06, 1.10, std::nullopt, 0.01},
    {"SCC4", 0.5, 0.0, 30.0, 0.03, 0.70, 0.005, 0.01},
};

// Nine of the twelve have never been read off BeamV0. Shrinking this list is
// the work; it must not grow silently.
const char* const kUnverifiedTargets[] = {"SCC3",  "SCC5",  "SCC6",  "aMCC1", "aMCC2",
                                          "aMCC3", "aMCC4", "aMCC5", "aMCC6"};

}  // namespace

TEST(TreatmentMigration, VerifiedTargetsMatchBeamV0sStimParamGrid) {
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
        if (expected.pd) EXPECT_DOUBLE_EQ(point.pulseDurationSeconds, *expected.pd);
        EXPECT_DOUBLE_EQ(point.pulseIntervalSeconds, expected.pi);
    }

    // The rest exist and are sonicable, but their values are unchecked.
    for (const char* name : kUnverifiedTargets) {
        SCOPED_TRACE(name);
        const beam::gui::TreatmentTarget* target = store.findTarget(name);
        ASSERT_NE(target, nullptr);
        EXPECT_FALSE(target->points.empty());
    }
    EXPECT_EQ(std::size(kVerifiedRows) + std::size(kUnverifiedTargets), store.targets().size())
        << "a target is in neither list; see docs/known_gaps_treatment.md";
}

// The split the Target List creates: the BEAM CSVs schedule 28 distinct names,
// the Target List defines 12, and the other 16 are reachable through a protocol
// but not placeable.
TEST(TreatmentMigration, TheProtocolCsvsNameSixteenTargetsNoTargetListDefines) {
    const std::vector<SourceEntry> source = readBeamV0Protocols();
    if (source.empty()) GTEST_SKIP() << "needs the BeamV0 checkout at " << BEAM_V0_PROTOCOL_DIR;

    std::set<std::string> scheduled;
    for (const SourceEntry& entry : source) scheduled.insert(entry.target);
    EXPECT_EQ(scheduled.size(), 28u);

    const beam::gui::TreatmentTargetStore store = shippedStore();
    std::vector<std::string> notInTheTargetList;
    for (const std::string& name : scheduled)
        if (store.findTarget(name) == nullptr) notInTheTargetList.push_back(name);

    EXPECT_EQ(notInTheTargetList.size(), 16u)
        << "first is " << (notInTheTargetList.empty() ? std::string("none") : notInTheTargetList.front());
    EXPECT_EQ(store.danglingTargetNames().size(), notInTheTargetList.size());
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
