// Tests for beam::gui::TreatmentTargetStore -- BeamAI's own Target List and
// protocol data, loaded from data/*.csv rather than BeamV0's .mat.

#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "gui/treatment_target_store.hpp"

namespace {

std::string writeTemp(const std::string& name, const std::string& body) {
    const std::string path = std::string(BEAM_TEST_SCRATCH) + "/" + name;
    std::ofstream file(path);
    file << body;
    return path;
}

}  // namespace

TEST(TreatmentTargetStore, LoadsTheShippedTargetsAndProtocols) {
    beam::gui::TreatmentTargetStore store;
    ASSERT_NO_THROW(store.loadTargets(BEAM_TREATMENT_TARGETS_CSV));
    ASSERT_NO_THROW(store.loadProtocols(BEAM_TREATMENT_PROTOCOLS_CSV));

    // BeamV0's Target List, confirmed against three sessions -- see
    // TreatmentMigration.TheAuthoritativeTargetListIsBeamV0sTwelve.
    EXPECT_EQ(store.targets().size(), 12u);
    EXPECT_EQ(store.targets().front().name, "SCC1");
    EXPECT_EQ(store.targets().back().name, "aMCC6");
    EXPECT_EQ(store.protocols().size(), 9u);

    // The nine BEAM protocol CSVs schedule targets no Target List defines, so
    // those names dangle by design; the count pins which.
    EXPECT_EQ(store.danglingTargetNames().size(), 16u);

    const beam::gui::TreatmentTarget* scc1 = store.findTarget("SCC1");
    ASSERT_NE(scc1, nullptr);
    // createStimParamTable.m's N = 1: the seed gives each target one point.
    ASSERT_EQ(scc1->points.size(), 1u);
    EXPECT_EQ(scc1->points.front().order, 1);
    EXPECT_TRUE(scc1->points.front().show);
    // The values themselves are BeamV0's business, asserted target by target
    // by TreatmentMigration.VerifiedTargetsMatchBeamV0sStimParamGrid -- they
    // vary per target, so this file only checks the shape.
    EXPECT_DOUBLE_EQ(scc1->points.front().burstDurationSeconds, 0.03);
    EXPECT_DOUBLE_EQ(scc1->points.front().pulseIntervalSeconds, 0.01);

    for (const beam::gui::TreatmentTarget& target : store.targets())
        EXPECT_FALSE(target.points.empty()) << target.name << " has no point to sonicate";

    EXPECT_EQ(store.findTarget("NoSuchTarget"), nullptr);
}

// The reason TreatmentTarget owns a list: BeamV0's stimParamTable is one table
// per Target List entry, and createSonicationUpdateTable.m appends a row to it
// (removeSonicationUpdateTable.m deletes one, sortSonicationTable.m sorts by
// order). A target with three points must keep three sets of parameters.
TEST(TreatmentTargetStore, ATargetKeepsEveryPointsOwnParameters) {
    beam::gui::TreatmentTargetStore store;
    store.loadTargets(BEAM_TREATMENT_TARGETS_CSV);

    beam::gui::TreatmentTarget scc1 = *store.findTarget("SCC1");
    ASSERT_EQ(scc1.points.size(), 1u);

    // createSonicationUpdateTable.m copies the previous row, then renumbers.
    beam::gui::TreatmentTargetPoint second = scc1.points.front();
    second.amplitude = 0.4;
    second.endTimeSeconds = 45.0;
    beam::gui::TreatmentTargetPoint third = scc1.points.front();
    third.amplitude = 0.2;
    third.show = false;
    ASSERT_TRUE(store.setTargetPoints("SCC1", {scc1.points.front(), second, third}));

    const beam::gui::TreatmentTarget* stored = store.findTarget("SCC1");
    ASSERT_EQ(stored->points.size(), 3u);
    EXPECT_EQ(stored->points[0].order, 1);
    EXPECT_EQ(stored->points[1].order, 2);
    EXPECT_EQ(stored->points[2].order, 3);
    EXPECT_DOUBLE_EQ(stored->points[0].amplitude, 0.5);
    EXPECT_DOUBLE_EQ(stored->points[1].amplitude, 0.4);
    EXPECT_DOUBLE_EQ(stored->points[2].amplitude, 0.2);
    EXPECT_DOUBLE_EQ(stored->points[1].endTimeSeconds, 45.0);
    EXPECT_FALSE(stored->points[2].show);

    // Other targets are untouched.
    EXPECT_EQ(store.findTarget("SCC2")->points.size(), 1u);

    EXPECT_FALSE(store.setTargetPoints("NoSuchTarget", {second}));
    // A target with no row could not be sonicated.
    EXPECT_FALSE(store.setTargetPoints("SCC1", {}));
    EXPECT_EQ(store.findTarget("SCC1")->points.size(), 3u);
}

// updateSonicateSettingsForCurrentSonication.m assigns the protocol's Duration
// to stimParamTableData.endTime -- a scalar into a whole table column, so every
// point moves -- and leaves Amplitude alone.
TEST(TreatmentTargetStore, SonicationDurationAppliesToEveryPointButNotAmplitude) {
    beam::gui::TreatmentTargetStore store;
    store.loadTargets(BEAM_TREATMENT_TARGETS_CSV);

    beam::gui::TreatmentTargetPoint second = store.findTarget("SCC1")->points.front();
    second.amplitude = 0.4;
    second.startTimeSeconds = 5.0;
    ASSERT_TRUE(store.setTargetPoints("SCC1", {store.findTarget("SCC1")->points.front(), second}));

    const std::vector<beam::gui::TreatmentTargetPoint> scheduled =
        store.pointsForSonication("SCC1", 90.0);
    ASSERT_EQ(scheduled.size(), 2u);
    for (const beam::gui::TreatmentTargetPoint& point : scheduled) {
        EXPECT_DOUBLE_EQ(point.startTimeSeconds, 0.0);
        EXPECT_DOUBLE_EQ(point.endTimeSeconds, 90.0);
    }
    // Amplitude is the point's own, not the schedule's.
    EXPECT_DOUBLE_EQ(scheduled[0].amplitude, 0.5);
    EXPECT_DOUBLE_EQ(scheduled[1].amplitude, 0.4);

    // No duration from the schedule leaves the points as they are.
    const std::vector<beam::gui::TreatmentTargetPoint> unscheduled =
        store.pointsForSonication("SCC1", std::nullopt);
    ASSERT_EQ(unscheduled.size(), 2u);
    EXPECT_DOUBLE_EQ(unscheduled[1].startTimeSeconds, 5.0);
    EXPECT_DOUBLE_EQ(unscheduled[1].endTimeSeconds, 30.0);

    EXPECT_TRUE(store.pointsForSonication("NoSuchTarget", 30.0).empty());
}

TEST(TreatmentTargetStore, ProtocolsKeepScheduleOrderAndRepeatTargets) {
    beam::gui::TreatmentTargetStore store;
    store.loadTargets(BEAM_TREATMENT_TARGETS_CSV);
    store.loadProtocols(BEAM_TREATMENT_PROTOCOLS_CSV);

    const beam::gui::TreatmentProtocolDefinition* def = store.findProtocol("Default");
    ASSERT_NE(def, nullptr);
    ASSERT_FALSE(def->entries.empty());
    EXPECT_EQ(def->entries.front().order, 1);
    for (std::size_t i = 1; i < def->entries.size(); ++i)
        EXPECT_LE(def->entries[i - 1].order, def->entries[i].order);

    // A protocol schedules the same target more than once -- that is the
    // reason entries carry their own amplitude and duration.
    EXPECT_LT(store.distinctTargetsOf("Default").size(), def->entries.size());
    EXPECT_TRUE(store.distinctTargetsOf("NoSuchProtocol").empty());
}

TEST(TreatmentTargetStore, TargetParametersAreEditable) {
    beam::gui::TreatmentTargetStore store;
    store.loadTargets(BEAM_TREATMENT_TARGETS_CSV);

    beam::gui::TreatmentTarget edited = *store.findTarget("SCC2");
    edited.points.front().burstIntervalSeconds = 2.5;
    edited.points.front().amplitude = 0.42;
    EXPECT_TRUE(store.updateTarget(edited));
    EXPECT_DOUBLE_EQ(store.findTarget("SCC2")->points.front().burstIntervalSeconds, 2.5);
    EXPECT_DOUBLE_EQ(store.findTarget("SCC2")->points.front().amplitude, 0.42);

    // Other targets are untouched, and the list keeps its order.
    EXPECT_DOUBLE_EQ(store.findTarget("SCC1")->points.front().burstIntervalSeconds, 0.70);
    EXPECT_EQ(store.targets().size(), 12u);

    beam::gui::TreatmentTarget unknown;
    unknown.name = "NoSuchTarget";
    EXPECT_FALSE(store.updateTarget(unknown));
}

TEST(TreatmentTargetStore, BadDataThrowsRatherThanDefaulting) {
    beam::gui::TreatmentTargetStore store;
    EXPECT_THROW(store.loadTargets("no/such/file.csv"), std::runtime_error);

    const std::string header = "name,point,show,amplitude,startTime,endTime,bd,bi,pd,pi\n";

    const std::string shortRow = writeTemp("targets_short.csv", header + "SCC1,1,1,0.75,0,30\n");
    EXPECT_THROW(store.loadTargets(shortRow), std::runtime_error);

    const std::string notANumber =
        writeTemp("targets_nan.csv", header + "SCC1,1,1,oops,0,30,0.06,1.1,0.005,0.01\n");
    EXPECT_THROW(store.loadTargets(notANumber), std::runtime_error);

    // A repeated name is now a second point, not a duplicate -- but its point
    // number has to follow on, or a row was lost upstream.
    const std::string twoPoints =
        writeTemp("targets_two_points.csv", header +
                                                "SCC1,1,1,0.75,0,30,0.06,1.1,0.005,0.01\n"
                                                "SCC1,2,0,0.50,0,30,0.06,1.1,0.005,0.01\n");
    ASSERT_NO_THROW(store.loadTargets(twoPoints));
    ASSERT_EQ(store.findTarget("SCC1")->points.size(), 2u);
    EXPECT_FALSE(store.findTarget("SCC1")->points[1].show);

    const std::string gap =
        writeTemp("targets_gap.csv", header +
                                         "SCC1,1,1,0.75,0,30,0.06,1.1,0.005,0.01\n"
                                         "SCC1,3,1,0.50,0,30,0.06,1.1,0.005,0.01\n");
    EXPECT_THROW(store.loadTargets(gap), std::runtime_error);

    const std::string repeatedNumber =
        writeTemp("targets_repeat.csv", header +
                                            "SCC1,1,1,0.75,0,30,0.06,1.1,0.005,0.01\n"
                                            "SCC1,1,1,0.50,0,30,0.06,1.1,0.005,0.01\n");
    EXPECT_THROW(store.loadTargets(repeatedNumber), std::runtime_error);
}

TEST(TreatmentTargetStore, ProtocolsCanBeCreatedRenamedAndDeleted) {
    beam::gui::TreatmentTargetStore store;
    store.loadTargets(BEAM_TREATMENT_TARGETS_CSV);
    store.loadProtocols(BEAM_TREATMENT_PROTOCOLS_CSV);
    const std::size_t before = store.protocols().size();

    EXPECT_TRUE(store.addProtocol("MyProtocol"));
    EXPECT_FALSE(store.addProtocol("MyProtocol")) << "duplicate name must be refused";
    EXPECT_FALSE(store.addProtocol("")) << "empty name must be refused";
    EXPECT_EQ(store.protocols().size(), before + 1);
    ASSERT_NE(store.findProtocol("MyProtocol"), nullptr);
    EXPECT_TRUE(store.findProtocol("MyProtocol")->entries.empty());

    beam::gui::TreatmentProtocolEntry first;
    first.target = "SCC2";
    beam::gui::TreatmentProtocolEntry second;
    second.target = "aMCC1";
    second.durationSeconds = 60.0;
    EXPECT_TRUE(store.appendProtocolEntry("MyProtocol", first));
    EXPECT_TRUE(store.appendProtocolEntry("MyProtocol", second));
    EXPECT_FALSE(store.appendProtocolEntry("NoSuchProtocol", first));

    const beam::gui::TreatmentProtocolDefinition* mine = store.findProtocol("MyProtocol");
    ASSERT_EQ(mine->entries.size(), 2u);
    EXPECT_EQ(mine->entries[0].order, 1);  // renumbered on write
    EXPECT_EQ(mine->entries[1].order, 2);
    EXPECT_EQ(mine->entries[1].target, "aMCC1");
    ASSERT_TRUE(mine->entries[1].durationSeconds.has_value());
    EXPECT_DOUBLE_EQ(*mine->entries[1].durationSeconds, 60.0);
    EXPECT_FALSE(mine->entries[0].durationSeconds.has_value());

    // Reordering is setProtocolEntries with the list reversed.
    std::vector<beam::gui::TreatmentProtocolEntry> reversed{mine->entries[1], mine->entries[0]};
    EXPECT_TRUE(store.setProtocolEntries("MyProtocol", reversed));
    EXPECT_EQ(store.findProtocol("MyProtocol")->entries[0].target, "aMCC1");
    EXPECT_EQ(store.findProtocol("MyProtocol")->entries[0].order, 1);

    EXPECT_TRUE(store.renameProtocol("MyProtocol", "Renamed"));
    EXPECT_FALSE(store.renameProtocol("Renamed", "Default")) << "colliding name must be refused";
    EXPECT_FALSE(store.renameProtocol("NoSuchProtocol", "Whatever"));
    EXPECT_EQ(store.findProtocol("MyProtocol"), nullptr);
    ASSERT_NE(store.findProtocol("Renamed"), nullptr);

    EXPECT_TRUE(store.removeProtocol("Renamed"));
    EXPECT_FALSE(store.removeProtocol("Renamed"));
    EXPECT_EQ(store.protocols().size(), before);
}

TEST(TreatmentTargetStore, SaveThenLoadRoundTrips) {
    beam::gui::TreatmentTargetStore original;
    original.loadTargets(BEAM_TREATMENT_TARGETS_CSV);
    original.loadProtocols(BEAM_TREATMENT_PROTOCOLS_CSV);

    beam::gui::TreatmentTarget edited = *original.findTarget("SCC2");
    edited.points.front().burstIntervalSeconds = 1.75;
    original.updateTarget(edited);
    original.addProtocol("RoundTrip");
    beam::gui::TreatmentProtocolEntry entry;
    entry.target = "SCC1";
    entry.amplitude = 0.33;
    original.appendProtocolEntry("RoundTrip", entry);

    const std::string targetsPath = std::string(BEAM_TEST_SCRATCH) + "/rt_targets.csv";
    const std::string protocolsPath = std::string(BEAM_TEST_SCRATCH) + "/rt_protocols.csv";
    ASSERT_NO_THROW(original.saveTargets(targetsPath));
    ASSERT_NO_THROW(original.saveProtocols(protocolsPath));

    beam::gui::TreatmentTargetStore reloaded;
    ASSERT_NO_THROW(reloaded.loadTargets(targetsPath));
    ASSERT_NO_THROW(reloaded.loadProtocols(protocolsPath));

    EXPECT_EQ(reloaded.targets().size(), original.targets().size());
    EXPECT_EQ(reloaded.protocols().size(), original.protocols().size());
    EXPECT_DOUBLE_EQ(reloaded.findTarget("SCC2")->points.front().burstIntervalSeconds, 1.75);
    // Including the names that dangle: a save must not quietly drop the
    // entries pointing at them, which would make the data error unreportable.
    EXPECT_EQ(reloaded.danglingTargetNames(), original.danglingTargetNames());

    const beam::gui::TreatmentProtocolDefinition* roundTrip = reloaded.findProtocol("RoundTrip");
    ASSERT_NE(roundTrip, nullptr);
    ASSERT_EQ(roundTrip->entries.size(), 1u);
    ASSERT_TRUE(roundTrip->entries[0].amplitude.has_value());
    EXPECT_DOUBLE_EQ(*roundTrip->entries[0].amplitude, 0.33);
    // An absent duration must survive as absent, not as zero.
    EXPECT_FALSE(roundTrip->entries[0].durationSeconds.has_value());
}

TEST(TreatmentTargetStore, CommentsAndBlankLinesAreIgnored) {
    beam::gui::TreatmentTargetStore store;
    const std::string path = writeTemp("targets_comments.csv",
                                        "# leading comment\n"
                                        "name,point,show,amplitude,startTime,endTime,bd,bi,pd,pi\n"
                                        "\n"
                                        "# a note about SCC1\n"
                                        "SCC1,1,1,0.75,0,30,0.06,1.1,0.005,0.01\n"
                                        "\n");
    ASSERT_NO_THROW(store.loadTargets(path));
    EXPECT_EQ(store.targets().size(), 1u);
    EXPECT_EQ(store.targets().front().name, "SCC1");
}
