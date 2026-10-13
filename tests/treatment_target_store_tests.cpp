// Tests for beam::gui::TreatmentTargetStore -- BeamAI's own Target List and
// protocol data, loaded from data/*.csv rather than BeamV0's .mat.

#include <fstream>
#include <string>

#include <algorithm>

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

    // The protocol takes its copy when SCC1 enters it, so the two points above
    // are what it holds.
    ASSERT_TRUE(store.addProtocol("P"));
    ASSERT_TRUE(store.appendProtocolEntry("P", beam::gui::TreatmentProtocolEntry{1, "SCC1", {}, {}}));

    const std::vector<beam::gui::TreatmentTargetPoint> scheduled =
        store.pointsForSonication("P", static_cast<std::size_t>(std::max(0, store.firstEntryForTarget("P", "SCC1"))), 90.0);
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
        store.pointsForSonication("P", static_cast<std::size_t>(std::max(0, store.firstEntryForTarget("P", "SCC1"))), std::nullopt);
    ASSERT_EQ(unscheduled.size(), 2u);
    EXPECT_DOUBLE_EQ(unscheduled[1].startTimeSeconds, 5.0);
    EXPECT_DOUBLE_EQ(unscheduled[1].endTimeSeconds, 30.0);

    EXPECT_EQ(store.firstEntryForTarget("P", "NoSuchTarget"), -1);
    EXPECT_TRUE(store.pointsForSonication("P", 99, 30.0).empty());
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

// A protocol that already holds a target holds a copy of it -- name and points
// together -- so the Target List renaming its own entry does not reach in and
// rewrite the protocol's. The protocol keeps a complete target of its own.
TEST(TreatmentTargetStore, RenamingATargetLeavesProtocolsAlone) {
    beam::gui::TreatmentTargetStore store;
    const std::string targets = writeTemp("rn_targets.csv",
                                          "name,point,show,amplitude,startTime,endTime,bd,bi,pd,pi\n"
                                          "SCC1,1,1,0.5,0,30,0.03,0.7,0.005,0.01\n"
                                          "SCC2,1,1,0.5,0,30,0.03,0.7,0.005,0.01\n");
    const std::string protocols = writeTemp("rn_protocols.csv",
                                            "protocol,order,target,amplitude,duration\n"
                                            "PainSCC,1,SCC1,,30\n"
                                            "PainSCC,2,SCC2,,30\n"
                                            "PainSCC,3,SCC1,,45\n");
    ASSERT_NO_THROW(store.loadTargets(targets));
    ASSERT_NO_THROW(store.loadProtocols(protocols));
    ASSERT_TRUE(store.renameTarget("SCC1", "SCC1b"));
    EXPECT_EQ(store.findTarget("SCC1"), nullptr);
    ASSERT_NE(store.findTarget("SCC1b"), nullptr);
    const auto* protocol = store.findProtocol("PainSCC");
    ASSERT_NE(protocol, nullptr);
    EXPECT_EQ(protocol->entries[0].target, "SCC1");
    EXPECT_EQ(protocol->entries[2].target, "SCC1");
    EXPECT_EQ(protocol->entries[1].target, "SCC2");
    // Not dangling either: the protocol took its own copy when SCC1 entered it,
    // so it can still sonicate a target the Target List no longer defines.
    EXPECT_NE(store.entryPoints("PainSCC", static_cast<std::size_t>(std::max(0, store.firstEntryForTarget("PainSCC", "SCC1")))), nullptr);
    EXPECT_TRUE(store.danglingTargetNames().empty());
}

TEST(TreatmentTargetStore, RenameRefusesACollisionWithoutSideEffects) {
    beam::gui::TreatmentTargetStore store;
    const std::string targets = writeTemp("rn_collide.csv",
                                          "name,point,show,amplitude,startTime,endTime,bd,bi,pd,pi\n"
                                          "SCC1,1,1,0.5,0,30,0.03,0.7,0.005,0.01\n"
                                          "SCC2,1,1,0.5,0,30,0.03,0.7,0.005,0.01\n");
    ASSERT_NO_THROW(store.loadTargets(targets));
    EXPECT_FALSE(store.renameTarget("SCC1", "SCC2"));
    EXPECT_FALSE(store.renameTarget("SCC1", ""));
    EXPECT_FALSE(store.renameTarget("nope", "SCC3"));
    EXPECT_EQ(store.targets().size(), 2u);
    ASSERT_NE(store.findTarget("SCC1"), nullptr);
    ASSERT_NE(store.findTarget("SCC2"), nullptr);
}

TEST(TreatmentTargetStore, EditedPointsSurviveASaveAndReload) {
    beam::gui::TreatmentTargetStore store;
    const std::string path = writeTemp("points_round.csv",
                                       "name,point,show,amplitude,startTime,endTime,bd,bi,pd,pi\n"
                                       "SCC1,1,1,0.5,0,30,0.03,0.7,0.005,0.01\n");
    ASSERT_NO_THROW(store.loadTargets(path));

    // What the grid hands back after an operator edits it: a second row, and
    // changed parameters on the first.
    std::vector<beam::gui::TreatmentTargetPoint> edited;
    beam::gui::TreatmentTargetPoint first;
    first.amplitude = 0.82;
    first.endTimeSeconds = 45.0;
    first.burstDurationSeconds = 0.06;
    edited.push_back(first);
    beam::gui::TreatmentTargetPoint second;
    second.amplitude = 0.33;
    second.show = false;
    edited.push_back(second);
    ASSERT_TRUE(store.setTargetPoints("SCC1", edited));

    const std::string out = writeTemp("points_round_out.csv", "");
    ASSERT_NO_THROW(store.saveTargets(out));

    beam::gui::TreatmentTargetStore reloaded;
    ASSERT_NO_THROW(reloaded.loadTargets(out));
    const auto* target = reloaded.findTarget("SCC1");
    ASSERT_NE(target, nullptr);
    ASSERT_EQ(target->points.size(), 2u);
    EXPECT_DOUBLE_EQ(target->points[0].amplitude, 0.82);
    EXPECT_DOUBLE_EQ(target->points[0].endTimeSeconds, 45.0);
    EXPECT_DOUBLE_EQ(target->points[0].burstDurationSeconds, 0.06);
    EXPECT_EQ(target->points[0].order, 1);
    EXPECT_DOUBLE_EQ(target->points[1].amplitude, 0.33);
    EXPECT_FALSE(target->points[1].show);
    EXPECT_EQ(target->points[1].order, 2);
}

TEST(TreatmentTargetStore, AnEmptyProtocolSurvivesASaveAndReload) {
    beam::gui::TreatmentTargetStore store;
    const std::string targets = writeTemp("empty_targets.csv",
                                          "name,point,show,amplitude,startTime,endTime,bd,bi,pd,pi\n"
                                          "SCC1,1,1,0.5,0,30,0.03,0.7,0.005,0.01\n");
    ASSERT_NO_THROW(store.loadTargets(targets));
    ASSERT_TRUE(store.addProtocol("Fresh"));
    ASSERT_TRUE(store.addProtocol("Scheduled"));
    beam::gui::TreatmentProtocolEntry entry;
    entry.target = "SCC1";
    entry.durationSeconds = 30.0;
    ASSERT_TRUE(store.appendProtocolEntry("Scheduled", entry));

    const std::string out = writeTemp("empty_protocols_out.csv", "");
    ASSERT_NO_THROW(store.saveProtocols(out));

    beam::gui::TreatmentTargetStore reloaded;
    ASSERT_NO_THROW(reloaded.loadProtocols(out));
    ASSERT_EQ(reloaded.protocols().size(), 2u);
    const auto* fresh = reloaded.findProtocol("Fresh");
    ASSERT_NE(fresh, nullptr);
    EXPECT_TRUE(fresh->entries.empty());
    const auto* scheduled = reloaded.findProtocol("Scheduled");
    ASSERT_NE(scheduled, nullptr);
    ASSERT_EQ(scheduled->entries.size(), 1u);
    EXPECT_EQ(scheduled->entries[0].target, "SCC1");
}

// The design the Treatment page follows: the Target List is a template. A
// protocol copies a target's points when the target enters it, and from then
// on the two are independent in both directions.
TEST(TreatmentTargetStore, AProtocolCopiesTheTemplateWhenATargetEntersIt) {
    beam::gui::TreatmentTargetStore store;
    store.loadTargets(BEAM_TREATMENT_TARGETS_CSV);
    ASSERT_TRUE(store.addProtocol("Plan"));
    ASSERT_TRUE(store.appendProtocolEntry("Plan",
                                          beam::gui::TreatmentProtocolEntry{1, "SCC1", {}, {}}));

    const std::vector<beam::gui::TreatmentTargetPoint>* copied = store.entryPoints("Plan", static_cast<std::size_t>(std::max(0, store.firstEntryForTarget("Plan", "SCC1"))));
    ASSERT_NE(copied, nullptr);
    ASSERT_EQ(copied->size(), store.findTarget("SCC1")->points.size());
    EXPECT_DOUBLE_EQ(copied->front().amplitude, store.findTarget("SCC1")->points.front().amplitude);

    // A target the protocol does not schedule has no entry at all.
    EXPECT_EQ(store.firstEntryForTarget("Plan", "SCC2"), -1);
    EXPECT_EQ(store.entryPoints("Plan", 1), nullptr);
}

TEST(TreatmentTargetStore, EditingTheTemplateDoesNotReachAPlanThatAlreadyCopiedIt) {
    beam::gui::TreatmentTargetStore store;
    store.loadTargets(BEAM_TREATMENT_TARGETS_CSV);
    ASSERT_TRUE(store.addProtocol("Plan"));
    ASSERT_TRUE(store.appendProtocolEntry("Plan",
                                          beam::gui::TreatmentProtocolEntry{1, "SCC1", {}, {}}));

    beam::gui::TreatmentTargetPoint changed = store.findTarget("SCC1")->points.front();
    changed.amplitude = 0.9;
    changed.burstDurationSeconds = 0.12;
    ASSERT_TRUE(store.setTargetPoints("SCC1", {changed}));

    // The template moved; the plan did not.
    EXPECT_DOUBLE_EQ(store.findTarget("SCC1")->points.front().amplitude, 0.9);
    ASSERT_NE(store.entryPoints("Plan", static_cast<std::size_t>(std::max(0, store.firstEntryForTarget("Plan", "SCC1")))), nullptr);
    EXPECT_DOUBLE_EQ(store.entryPoints("Plan", static_cast<std::size_t>(std::max(0, store.firstEntryForTarget("Plan", "SCC1"))))->front().amplitude, 0.5);
    EXPECT_DOUBLE_EQ(store.entryPoints("Plan", static_cast<std::size_t>(std::max(0, store.firstEntryForTarget("Plan", "SCC1"))))->front().burstDurationSeconds, 0.03);
}

TEST(TreatmentTargetStore, AnEditInsideOnePlanStaysInsideThatPlan) {
    beam::gui::TreatmentTargetStore store;
    store.loadTargets(BEAM_TREATMENT_TARGETS_CSV);
    for (const char* name : {"PlanA", "PlanB"}) {
        ASSERT_TRUE(store.addProtocol(name));
        ASSERT_TRUE(store.appendProtocolEntry(name,
                                              beam::gui::TreatmentProtocolEntry{1, "SCC1", {}, {}}));
    }

    beam::gui::TreatmentTargetPoint local = store.entryPoints("PlanA", static_cast<std::size_t>(std::max(0, store.firstEntryForTarget("PlanA", "SCC1"))))->front();
    local.amplitude = 0.77;
    ASSERT_TRUE(store.setEntryPoints("PlanA", static_cast<std::size_t>(std::max(0, store.firstEntryForTarget("PlanA", "SCC1"))), {local}));

    EXPECT_DOUBLE_EQ(store.entryPoints("PlanA", static_cast<std::size_t>(std::max(0, store.firstEntryForTarget("PlanA", "SCC1"))))->front().amplitude, 0.77);
    // Neither the other plan nor the template followed it.
    EXPECT_DOUBLE_EQ(store.entryPoints("PlanB", static_cast<std::size_t>(std::max(0, store.firstEntryForTarget("PlanB", "SCC1"))))->front().amplitude, 0.5);
    EXPECT_DOUBLE_EQ(store.findTarget("SCC1")->points.front().amplitude, 0.5);
}

// The chosen rule for a re-add: the plan's edits are discarded with the target,
// and it comes back as whatever the template says now.
TEST(TreatmentTargetStore, RemovingATargetFromAPlanDiscardsThatPlansEdits) {
    beam::gui::TreatmentTargetStore store;
    store.loadTargets(BEAM_TREATMENT_TARGETS_CSV);
    ASSERT_TRUE(store.addProtocol("Plan"));
    ASSERT_TRUE(store.appendProtocolEntry("Plan",
                                          beam::gui::TreatmentProtocolEntry{1, "SCC1", {}, {}}));

    beam::gui::TreatmentTargetPoint local = store.entryPoints("Plan", static_cast<std::size_t>(std::max(0, store.firstEntryForTarget("Plan", "SCC1"))))->front();
    local.amplitude = 0.77;
    ASSERT_TRUE(store.setEntryPoints("Plan", static_cast<std::size_t>(std::max(0, store.firstEntryForTarget("Plan", "SCC1"))), {local}));

    ASSERT_TRUE(store.setProtocolEntries("Plan", {}));
    EXPECT_EQ(store.entryPoints("Plan", static_cast<std::size_t>(std::max(0, store.firstEntryForTarget("Plan", "SCC1")))), nullptr);

    beam::gui::TreatmentTargetPoint templateNow = store.findTarget("SCC1")->points.front();
    templateNow.amplitude = 0.31;
    ASSERT_TRUE(store.setTargetPoints("SCC1", {templateNow}));
    ASSERT_TRUE(store.appendProtocolEntry("Plan",
                                          beam::gui::TreatmentProtocolEntry{1, "SCC1", {}, {}}));
    ASSERT_NE(store.entryPoints("Plan", static_cast<std::size_t>(std::max(0, store.firstEntryForTarget("Plan", "SCC1")))), nullptr);
    EXPECT_DOUBLE_EQ(store.entryPoints("Plan", static_cast<std::size_t>(std::max(0, store.firstEntryForTarget("Plan", "SCC1"))))->front().amplitude, 0.31);
}

TEST(TreatmentTargetStore, PlanPointsSurviveASaveAndReload) {
    const std::string targets = std::string(BEAM_TEST_SCRATCH) + "/plan_targets.csv";
    const std::string protocols = std::string(BEAM_TEST_SCRATCH) + "/plan_protocols.csv";
    const std::string planPoints = std::string(BEAM_TEST_SCRATCH) + "/plan_points.csv";

    beam::gui::TreatmentTargetStore store;
    store.loadTargets(BEAM_TREATMENT_TARGETS_CSV);
    ASSERT_TRUE(store.addProtocol("Plan"));
    ASSERT_TRUE(store.appendProtocolEntry("Plan",
                                          beam::gui::TreatmentProtocolEntry{1, "SCC1", {}, {}}));
    beam::gui::TreatmentTargetPoint a = store.entryPoints("Plan", static_cast<std::size_t>(std::max(0, store.firstEntryForTarget("Plan", "SCC1"))))->front();
    beam::gui::TreatmentTargetPoint b = a;
    a.amplitude = 0.77;
    b.amplitude = 0.21;
    b.show = false;
    ASSERT_TRUE(store.setEntryPoints("Plan", static_cast<std::size_t>(std::max(0, store.firstEntryForTarget("Plan", "SCC1"))), {a, b}));
    ASSERT_NO_THROW(store.saveTargets(targets));
    ASSERT_NO_THROW(store.saveProtocols(protocols));
    ASSERT_NO_THROW(store.savePlanPoints(planPoints));

    beam::gui::TreatmentTargetStore reloaded;
    ASSERT_NO_THROW(reloaded.loadTargets(targets));
    ASSERT_NO_THROW(reloaded.loadProtocols(protocols));
    ASSERT_NO_THROW(reloaded.loadPlanPoints(planPoints));

    const std::vector<beam::gui::TreatmentTargetPoint>* back = reloaded.entryPoints("Plan", static_cast<std::size_t>(std::max(0, reloaded.firstEntryForTarget("Plan", "SCC1"))));
    ASSERT_NE(back, nullptr);
    ASSERT_EQ(back->size(), 2u);
    EXPECT_DOUBLE_EQ((*back)[0].amplitude, 0.77);
    EXPECT_DOUBLE_EQ((*back)[1].amplitude, 0.21);
    EXPECT_FALSE((*back)[1].show);
    // The template is untouched by any of it.
    EXPECT_DOUBLE_EQ(reloaded.findTarget("SCC1")->points.front().amplitude, 0.5);
}

// Data written before protocols owned points has no plan-points file at all.
TEST(TreatmentTargetStore, DataWithNoPlanPointsFileStartsFromTheTemplate) {
    beam::gui::TreatmentTargetStore store;
    store.loadTargets(BEAM_TREATMENT_TARGETS_CSV);
    store.loadProtocols(BEAM_TREATMENT_PROTOCOLS_CSV);
    ASSERT_NO_THROW(store.loadPlanPoints(std::string(BEAM_TEST_SCRATCH) + "/not_written_yet.csv"));

    const std::string protocol = store.protocols().front().name;
    const std::vector<std::string> targets = store.distinctTargetsOf(protocol);
    ASSERT_FALSE(targets.empty());
    for (const std::string& name : targets) {
        if (store.findTarget(name) == nullptr) continue;  // dangling by design
        ASSERT_NE(store.entryPoints(protocol, static_cast<std::size_t>(std::max(0, store.firstEntryForTarget(protocol, name)))), nullptr);
        EXPECT_EQ(store.entryPoints(protocol, static_cast<std::size_t>(std::max(0, store.firstEntryForTarget(protocol, name))))->size(), store.findTarget(name)->points.size());
    }
}

// The plan's own edits survive a rename of the template entry, under the old
// name -- they belong to the plan, not to the Target List.
TEST(TreatmentTargetStore, RenamingATargetLeavesEachPlansCopyUnderItsOwnName) {
    beam::gui::TreatmentTargetStore store;
    store.loadTargets(BEAM_TREATMENT_TARGETS_CSV);
    ASSERT_TRUE(store.addProtocol("Plan"));
    ASSERT_TRUE(store.appendProtocolEntry("Plan",
                                          beam::gui::TreatmentProtocolEntry{1, "SCC1", {}, {}}));
    beam::gui::TreatmentTargetPoint local = store.entryPoints("Plan", static_cast<std::size_t>(std::max(0, store.firstEntryForTarget("Plan", "SCC1"))))->front();
    local.amplitude = 0.77;
    ASSERT_TRUE(store.setEntryPoints("Plan", static_cast<std::size_t>(std::max(0, store.firstEntryForTarget("Plan", "SCC1"))), {local}));

    ASSERT_TRUE(store.renameTarget("SCC1", "SCC1b"));
    EXPECT_EQ(store.firstEntryForTarget("Plan", "SCC1b"), -1);
    ASSERT_NE(store.entryPoints("Plan", static_cast<std::size_t>(std::max(0, store.firstEntryForTarget("Plan", "SCC1")))), nullptr);
    EXPECT_DOUBLE_EQ(store.entryPoints("Plan", static_cast<std::size_t>(std::max(0, store.firstEntryForTarget("Plan", "SCC1"))))->front().amplitude, 0.77);
}

// Deleting from the Target List is equally hands-off.
TEST(TreatmentTargetStore, DeletingATargetLeavesProtocolsThatHoldACopy) {
    beam::gui::TreatmentTargetStore store;
    store.loadTargets(BEAM_TREATMENT_TARGETS_CSV);
    ASSERT_TRUE(store.addProtocol("Plan"));
    ASSERT_TRUE(store.appendProtocolEntry("Plan",
                                          beam::gui::TreatmentProtocolEntry{1, "SCC1", {}, {}}));
    ASSERT_TRUE(store.removeTarget("SCC1"));

    EXPECT_EQ(store.findTarget("SCC1"), nullptr);
    const auto* protocol = store.findProtocol("Plan");
    ASSERT_NE(protocol, nullptr);
    ASSERT_EQ(protocol->entries.size(), 1u);
    EXPECT_EQ(protocol->entries.front().target, "SCC1");
    ASSERT_NE(store.entryPoints("Plan", static_cast<std::size_t>(std::max(0, store.firstEntryForTarget("Plan", "SCC1")))), nullptr);
    EXPECT_TRUE(store.danglingTargetNames().empty());
    // Still sonicable: the points came with it.
    EXPECT_FALSE(store.pointsForSonication("Plan", static_cast<std::size_t>(std::max(0, store.firstEntryForTarget("Plan", "SCC1"))), 30.0).empty());
}

// A name no protocol ever copied is the real dangling case, and still reported.
TEST(TreatmentTargetStore, ANameNoProtocolEverCopiedIsStillDangling) {
    beam::gui::TreatmentTargetStore store;
    store.loadTargets(BEAM_TREATMENT_TARGETS_CSV);
    store.loadProtocols(BEAM_TREATMENT_PROTOCOLS_CSV);
    EXPECT_FALSE(store.danglingTargetNames().empty());
}

// The reported defect: two entries scheduling the same target are separate
// occurrences, and editing one must not move the other. Points live on the
// entry for exactly this reason.
TEST(TreatmentTargetStore, TwoEntriesForOneTargetAreIndependent) {
    beam::gui::TreatmentTargetStore store;
    store.loadTargets(BEAM_TREATMENT_TARGETS_CSV);
    ASSERT_TRUE(store.addProtocol("Plan"));
    ASSERT_TRUE(store.appendProtocolEntry("Plan",
                                          beam::gui::TreatmentProtocolEntry{1, "SCC1", {}, {}}));
    ASSERT_TRUE(store.appendProtocolEntry("Plan",
                                          beam::gui::TreatmentProtocolEntry{2, "SCC2", {}, {}}));
    ASSERT_TRUE(store.appendProtocolEntry("Plan",
                                          beam::gui::TreatmentProtocolEntry{3, "SCC1", {}, {}}));
    ASSERT_NE(store.entryPoints("Plan", 0), nullptr);
    ASSERT_NE(store.entryPoints("Plan", 2), nullptr);

    beam::gui::TreatmentTargetPoint edited = store.entryPoints("Plan", 0)->front();
    edited.amplitude = 0.91;
    edited.burstDurationSeconds = 0.077;
    edited.endTimeSeconds = 45.0;
    ASSERT_TRUE(store.setEntryPoints("Plan", 0, {edited}));

    // Entry 0 moved; entry 2 names the same target and did not.
    EXPECT_DOUBLE_EQ(store.entryPoints("Plan", 0)->front().amplitude, 0.91);
    EXPECT_DOUBLE_EQ(store.entryPoints("Plan", 0)->front().burstDurationSeconds, 0.077);
    EXPECT_DOUBLE_EQ(store.entryPoints("Plan", 2)->front().amplitude, 0.5);
    EXPECT_DOUBLE_EQ(store.entryPoints("Plan", 2)->front().burstDurationSeconds, 0.03);
    EXPECT_DOUBLE_EQ(store.entryPoints("Plan", 2)->front().endTimeSeconds, 30.0);
    // Nor did the template.
    EXPECT_DOUBLE_EQ(store.findTarget("SCC1")->points.front().amplitude, 0.5);
}

// And the independence has to survive the file.
TEST(TreatmentTargetStore, IndependentEntriesSurviveASaveAndReload) {
    const std::string targets = std::string(BEAM_TEST_SCRATCH) + "/ind_targets.csv";
    const std::string protocols = std::string(BEAM_TEST_SCRATCH) + "/ind_protocols.csv";
    const std::string planPoints = std::string(BEAM_TEST_SCRATCH) + "/ind_points.csv";

    beam::gui::TreatmentTargetStore store;
    store.loadTargets(BEAM_TREATMENT_TARGETS_CSV);
    ASSERT_TRUE(store.addProtocol("Plan"));
    for (int i = 0; i < 2; ++i)
        ASSERT_TRUE(store.appendProtocolEntry(
            "Plan", beam::gui::TreatmentProtocolEntry{i + 1, "SCC1", {}, {}}));
    beam::gui::TreatmentTargetPoint first = store.entryPoints("Plan", 0)->front();
    first.amplitude = 0.11;
    ASSERT_TRUE(store.setEntryPoints("Plan", 0, {first}));
    beam::gui::TreatmentTargetPoint second = store.entryPoints("Plan", 1)->front();
    second.amplitude = 0.22;
    ASSERT_TRUE(store.setEntryPoints("Plan", 1, {second}));

    ASSERT_NO_THROW(store.saveTargets(targets));
    ASSERT_NO_THROW(store.saveProtocols(protocols));
    ASSERT_NO_THROW(store.savePlanPoints(planPoints));

    beam::gui::TreatmentTargetStore reloaded;
    ASSERT_NO_THROW(reloaded.loadTargets(targets));
    ASSERT_NO_THROW(reloaded.loadProtocols(protocols));
    ASSERT_NO_THROW(reloaded.loadPlanPoints(planPoints));
    ASSERT_NE(reloaded.entryPoints("Plan", 0), nullptr);
    ASSERT_NE(reloaded.entryPoints("Plan", 1), nullptr);
    EXPECT_DOUBLE_EQ(reloaded.entryPoints("Plan", 0)->front().amplitude, 0.11);
    EXPECT_DOUBLE_EQ(reloaded.entryPoints("Plan", 1)->front().amplitude, 0.22);
}
