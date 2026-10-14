// The record of what was actually delivered, as opposed to what was planned.
// BeamV0's equivalent is treatmentProtocolTables(i).sessions(visit).Data.

#include <fstream>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "gui/treatment_log.hpp"

namespace {

std::string scratch(const std::string& name) {
    return std::string(BEAM_TEST_SCRATCH) + "/" + name;
}

beam::gui::TreatmentLogRow makeRow(int visit, const std::string& target) {
    beam::gui::TreatmentLogRow row;
    row.visit = visit;
    row.protocol = "Default";
    row.target = target;
    row.xMm = 34.3;
    row.yMm = 68.0;
    row.zMm = -31.3;
    row.amplitude = 0.5;
    row.startTimeSeconds = 0.0;
    row.endTimeSeconds = 30.0;
    row.burstDurationSeconds = 0.09;
    row.burstIntervalSeconds = 1.4;
    row.pulseDurationSeconds = 0.001;
    row.pulseIntervalSeconds = 0.005;
    row.startedAt = "2026-10-14T09:15:00";
    return row;
}

}  // namespace

TEST(TreatmentLog, AppendNumbersEachVisitFromOne) {
    beam::gui::TreatmentLog log;
    EXPECT_EQ(log.append(makeRow(1, "SCC1")), 1);
    EXPECT_EQ(log.append(makeRow(1, "SCC2")), 2);
    // A second visit starts its own sequence: the number is the order within
    // the visit, which is what BeamV0 keeps one table per.
    EXPECT_EQ(log.append(makeRow(2, "SCC1")), 1);
    EXPECT_EQ(log.append(makeRow(1, "SCC3")), 3);
    EXPECT_EQ(log.rows().size(), 4u);
}

TEST(TreatmentLog, RowsForVisitComeBackInDeliveryOrder) {
    beam::gui::TreatmentLog log;
    log.append(makeRow(1, "SCC1"));
    log.append(makeRow(2, "aMCC1"));
    log.append(makeRow(1, "SCC2"));

    const std::vector<beam::gui::TreatmentLogRow> first = log.rowsFor("Default", 1);
    ASSERT_EQ(first.size(), 2u);
    EXPECT_EQ(first[0].target, "SCC1");
    EXPECT_EQ(first[1].target, "SCC2");
    EXPECT_EQ(first[0].sequence, 1);
    EXPECT_EQ(first[1].sequence, 2);

    EXPECT_EQ(log.rowsFor("Default", 2).size(), 1u);
    EXPECT_TRUE(log.rowsFor("Default", 3).empty());
}

TEST(TreatmentLog, ARowStaysOpenUntilItIsClosed) {
    beam::gui::TreatmentLog log;
    const int sequence = log.append(makeRow(1, "SCC1"));
    ASSERT_NE(log.find("Default", 1, sequence), nullptr);
    EXPECT_TRUE(log.find("Default", 1, sequence)->endedAt.empty());

    EXPECT_TRUE(log.close("Default", 1, sequence, "2026-10-14T09:15:30"));
    EXPECT_EQ(log.find("Default", 1, sequence)->endedAt, "2026-10-14T09:15:30");
    // An abort closes a row that has already been closed by nothing else.
    EXPECT_FALSE(log.close("Default", 1, 99, "whenever"));
    EXPECT_EQ(log.find("Default", 9, 1), nullptr);
}

TEST(TreatmentLog, ResponsesAndNotesAttachToAnExistingRow) {
    beam::gui::TreatmentLog log;
    const int sequence = log.append(makeRow(1, "SCC1"));
    EXPECT_FALSE(log.find("Default", 1, sequence)->responsePain.has_value());

    EXPECT_TRUE(log.setResponse("Default", 1, sequence, 3.0, 4.0));
    EXPECT_TRUE(log.setNotes("Default", 1, sequence, "tolerated well, no complaint"));
    EXPECT_DOUBLE_EQ(*log.find("Default", 1, sequence)->responsePain, 3.0);
    EXPECT_DOUBLE_EQ(*log.find("Default", 1, sequence)->responseMood, 4.0);
    EXPECT_EQ(log.find("Default", 1, sequence)->notes, "tolerated well, no complaint");

    EXPECT_FALSE(log.setResponse("Default", 2, 1, 1.0, 1.0));
    EXPECT_FALSE(log.setNotes("Default", 2, 1, "nowhere"));
}

TEST(TreatmentLog, SaveAndReloadKeepsEveryField) {
    beam::gui::TreatmentLog log;
    const int first = log.append(makeRow(1, "SCC1"));
    log.close("Default", 1, first, "2026-10-14T09:15:30");
    log.setResponse("Default", 1, first, 2.0, 5.0);
    // A comma in the notes must survive the round trip rather than splitting
    // the row.
    log.setNotes("Default", 1, first, "steady, no movement");
    beam::gui::TreatmentLogRow sham = makeRow(2, "aMCC3");
    sham.sham = true;
    log.append(std::move(sham));

    const std::string path = scratch("treatment_log_roundtrip.csv");
    ASSERT_NO_THROW(log.save(path));

    beam::gui::TreatmentLog reloaded;
    ASSERT_NO_THROW(reloaded.load(path));
    ASSERT_EQ(reloaded.rows().size(), 2u);

    const beam::gui::TreatmentLogRow* back = reloaded.find("Default", 1, first);
    ASSERT_NE(back, nullptr);
    EXPECT_EQ(back->protocol, "Default");
    EXPECT_EQ(back->target, "SCC1");
    EXPECT_DOUBLE_EQ(back->xMm, 34.3);
    EXPECT_DOUBLE_EQ(back->burstIntervalSeconds, 1.4);
    EXPECT_DOUBLE_EQ(back->pulseDurationSeconds, 0.001);
    EXPECT_EQ(back->startedAt, "2026-10-14T09:15:00");
    EXPECT_EQ(back->endedAt, "2026-10-14T09:15:30");
    EXPECT_DOUBLE_EQ(*back->responsePain, 2.0);
    EXPECT_EQ(back->notes, "steady, no movement");
    EXPECT_FALSE(back->sham);

    const beam::gui::TreatmentLogRow* second = reloaded.find("Default", 2, 1);
    ASSERT_NE(second, nullptr);
    EXPECT_TRUE(second->sham);
    // Never answered, so it must come back unanswered rather than as zero.
    EXPECT_FALSE(second->responsePain.has_value());
    EXPECT_FALSE(second->responseMood.has_value());
}

TEST(TreatmentLog, NoFileMeansNoDeliveriesRatherThanAnError) {
    beam::gui::TreatmentLog log;
    log.append(makeRow(1, "SCC1"));
    // Loading over an absent file leaves what is already there alone: a missing
    // log is a patient who has not been treated, not a corrupt install.
    ASSERT_NO_THROW(log.load(scratch("no_such_treatment_log.csv")));
    EXPECT_EQ(log.rows().size(), 1u);
}

TEST(TreatmentLog, AFileWithoutTheProtocolTableHeaderIsRejected) {
    const std::string path = scratch("treatment_log_foreign.csv");
    {
        std::ofstream file(path);
        file << "alpha,beta,gamma\n1,2,3\n";
    }
    beam::gui::TreatmentLog log;
    // Reading someone else's CSV as a log would invent rows out of whatever
    // happened to be in the columns.
    EXPECT_THROW(log.load(path), std::runtime_error);
}

// setLogInfoToCSV.m's own columns: the protocol table's, the Parameters string
// parsed out beside it, and the visit/site metadata it appends.
TEST(TreatmentLog, TheFileCarriesBeamV0sColumnNames) {
    beam::gui::TreatmentLog log;
    log.append(makeRow(1, "SCC1"));
    const std::string path = scratch("treatment_log_columns.csv");
    ASSERT_NO_THROW(log.save(path));

    std::ifstream file(path);
    std::string header;
    while (std::getline(file, header)) {
        if (!header.empty() && header[0] != '#') break;
    }
    for (const char* column :
         {"Number", "Name", "Duration", "Amplitude", "Parameters", "ResponsePain", "ResponseMood",
          "ResponseTotal", "Notes", "X", "Y", "Z", "BD", "BI", "PD", "PI", "VisitNumber",
          "SubjectID", "SiteID", "SessionDate", "BEAMSerialNumber", "Transducer1SerialNumber",
          "Transducer2SerialNumber", "SoftwareVersion", "Region", "HardwareSerialNumber",
          "AttenuationVal", "DC", "SonicationStartDTM", "SonicationEndDTM"}) {
        SCOPED_TRACE(column);
        EXPECT_NE(header.find(column), std::string::npos);
    }
}

TEST(TreatmentLog, TheParameterStringMatchesBeamV0sFormat) {
    const beam::gui::TreatmentLogRow row = makeRow(1, "SCC3");
    // getTreatmentProtocolParamString.m writes every value labelled, which is
    // what setLogInfoToCSV.m then parses back out.
    EXPECT_EQ(beam::gui::TreatmentLog::parameterString(row),
              "X:34.3,Y:68,Z:-31.3,BD:0.09,BI:1.4,PD:0.001,PI:0.005");
}

TEST(TreatmentLog, ResponseTotalRoundsAndClampsEachScore) {
    beam::gui::TreatmentLogRow row = makeRow(1, "SCC1");
    EXPECT_DOUBLE_EQ(beam::gui::TreatmentLog::responseTotal(row), 0.0);

    row.responsePain = 1.4;
    row.responseMood = 1.6;
    EXPECT_DOUBLE_EQ(beam::gui::TreatmentLog::responseTotal(row), 3.0);

    // Out of range in both directions, and a missing score counts as zero.
    row.responsePain = 9.0;
    row.responseMood = -9.0;
    EXPECT_DOUBLE_EQ(beam::gui::TreatmentLog::responseTotal(row), 0.0);
    row.responseMood = std::nullopt;
    EXPECT_DOUBLE_EQ(beam::gui::TreatmentLog::responseTotal(row), 2.0);
}


// The two things kept apart: the protocol is the model, the visit's log is what
// happened. BeamV0 seeds sessions(visit).Data from blank.Data and writes only
// into sessions -- blank is never written from the table.
TEST(TreatmentLog, SeedingAVisitFillsOnlyThePositionsItDoesNotHave) {
    beam::gui::TreatmentLog log;
    std::vector<beam::gui::TreatmentLogRow> planned;
    for (int i = 1; i <= 3; ++i) {
        beam::gui::TreatmentLogRow row = makeRow(1, "SCC" + std::to_string(i));
        row.sequence = i;
        row.startedAt.clear();  // planned, not delivered
        planned.push_back(row);
    }
    log.seedVisit(1, "Default", planned);
    ASSERT_EQ(log.rowsFor("Default", 1).size(), 3u);
    for (const auto& row : log.rowsFor("Default", 1))
        EXPECT_FALSE(beam::gui::TreatmentLog::delivered(row));

    // Row 2 gets fired, then the page reloads and seeds again.
    beam::gui::TreatmentLogRow fired = makeRow(1, "aMCC1");
    fired.protocol = "Default";
    fired.sequence = 2;
    fired.startedAt = "2026-10-14T10:00:00";
    log.record(std::move(fired));
    log.seedVisit(1, "Default", planned);

    ASSERT_EQ(log.rowsFor("Default", 1).size(), 3u) << "seeding must not duplicate a row";
    const beam::gui::TreatmentLogRow* second = log.find("Default", 1, 2);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second->target, "aMCC1") << "re-seeding overwrote what was delivered";
    EXPECT_TRUE(beam::gui::TreatmentLog::delivered(*second));
    EXPECT_FALSE(beam::gui::TreatmentLog::delivered(*log.find("Default", 1, 1)));
}

TEST(TreatmentLog, EachProtocolAndVisitKeepsItsOwnSequence) {
    beam::gui::TreatmentLog log;
    std::vector<beam::gui::TreatmentLogRow> planned;
    beam::gui::TreatmentLogRow row = makeRow(1, "SCC1");
    row.sequence = 1;
    planned.push_back(row);

    log.seedVisit(1, "Default", planned);
    log.seedVisit(1, "Addiction", planned);
    log.seedVisit(2, "Default", planned);

    // Three separate slots, not one overwritten three times: BeamV0 keys
    // sessions under the protocol as well as the visit.
    EXPECT_EQ(log.rows().size(), 3u);
    EXPECT_NE(log.find("Default", 1, 1), nullptr);
    EXPECT_NE(log.find("Addiction", 1, 1), nullptr);
    EXPECT_NE(log.find("Default", 2, 1), nullptr);
    EXPECT_EQ(log.find("PTSD", 1, 1), nullptr);
}

TEST(TreatmentLog, RecordingADeliveryKeepsTheOperatorsResponse) {
    beam::gui::TreatmentLog log;
    std::vector<beam::gui::TreatmentLogRow> planned;
    beam::gui::TreatmentLogRow row = makeRow(1, "SCC1");
    row.sequence = 1;
    row.startedAt.clear();
    planned.push_back(row);
    log.seedVisit(1, "Default", planned);
    ASSERT_TRUE(log.setResponse("Default", 1, 1, 2.0, -1.0));
    ASSERT_TRUE(log.setNotes("Default", 1, 1, "first attempt"));

    // Re-firing the same slot replaces what was sent, not what was observed.
    beam::gui::TreatmentLogRow again = makeRow(1, "SCC1");
    again.protocol = "Default";
    again.sequence = 1;
    again.amplitude = 0.9;
    again.startedAt = "2026-10-14T11:00:00";
    log.record(std::move(again));

    const beam::gui::TreatmentLogRow* back = log.find("Default", 1, 1);
    ASSERT_NE(back, nullptr);
    EXPECT_DOUBLE_EQ(back->amplitude, 0.9);
    EXPECT_DOUBLE_EQ(*back->responsePain, 2.0);
    EXPECT_DOUBLE_EQ(*back->responseMood, -1.0);
    EXPECT_EQ(back->notes, "first attempt");
}

TEST(TreatmentLog, ReseedingFollowsThePlanWhileNothingHasRun) {
    beam::gui::TreatmentLog log;
    std::vector<beam::gui::TreatmentLogRow> planned;
    for (int i = 1; i <= 3; ++i) {
        beam::gui::TreatmentLogRow row = makeRow(1, "SCC1");
        row.sequence = i;
        row.startedAt.clear();
        planned.push_back(row);
    }
    log.seedVisit(1, "Default", planned);
    ASSERT_TRUE(log.setResponse("Default", 1, 2, 1.0, 1.0));

    for (auto& row : planned) row.target = "SCC2";
    log.seedVisit(1, "Default", planned);

    EXPECT_EQ(log.find("Default", 1, 1)->target, "SCC2");
    EXPECT_EQ(log.find("Default", 1, 3)->target, "SCC2");
    EXPECT_DOUBLE_EQ(*log.find("Default", 1, 2)->responsePain, 1.0) << "the response was discarded";
}

TEST(TreatmentLog, ADeliveryProtectsItsOwnRowAndNoOthers) {
    beam::gui::TreatmentLog log;
    std::vector<beam::gui::TreatmentLogRow> planned;
    for (int i = 1; i <= 3; ++i) {
        beam::gui::TreatmentLogRow row = makeRow(1, "SCC1");
        row.sequence = i;
        row.startedAt.clear();
        planned.push_back(row);
    }
    log.seedVisit(1, "Default", planned);

    beam::gui::TreatmentLogRow fired = makeRow(1, "SCC1");
    fired.protocol = "Default";
    fired.sequence = 1;
    fired.startedAt = "2026-10-14T10:00:00";
    log.record(std::move(fired));

    // Editing the protocol mid-visit reaches the rows still ahead of the
    // operator. Only the row that actually ran keeps what it ran with --
    // freezing the whole visit meant a new visit was the only way out.
    for (auto& row : planned) row.target = "SCC2";
    log.seedVisit(1, "Default", planned);
    EXPECT_EQ(log.find("Default", 1, 1)->target, "SCC1") << "the delivered row was rewritten";
    EXPECT_EQ(log.find("Default", 1, 2)->target, "SCC2");
    EXPECT_EQ(log.find("Default", 1, 3)->target, "SCC2");
    EXPECT_FALSE(log.visitFrozen("Default", 1));

    // Skipping a row is what freezes it: the positions stop matching.
    ASSERT_TRUE(log.removeAt("Default", 1, 3));
    EXPECT_TRUE(log.visitFrozen("Default", 1));
    for (auto& row : planned) row.target = "aMCC1";
    log.seedVisit(1, "Default", planned);
    EXPECT_EQ(log.find("Default", 1, 2)->target, "SCC2") << "a hand-edited visit kept its own";
}

TEST(TreatmentLog, AShorterScheduleDropsTrailingRowsBeforeTheVisitRuns) {
    beam::gui::TreatmentLog log;
    std::vector<beam::gui::TreatmentLogRow> planned;
    for (int i = 1; i <= 4; ++i) {
        beam::gui::TreatmentLogRow row = makeRow(1, "SCC1");
        row.sequence = i;
        row.startedAt.clear();
        planned.push_back(row);
    }
    log.seedVisit(1, "Default", planned);
    planned.resize(2);
    log.seedVisit(1, "Default", planned);

    EXPECT_NE(log.find("Default", 1, 2), nullptr);
    EXPECT_EQ(log.find("Default", 1, 3), nullptr);
    EXPECT_EQ(log.find("Default", 1, 4), nullptr);
}

TEST(TreatmentLog, ASonicationCanBeAddedOrSkippedBeforeItRuns) {
    beam::gui::TreatmentLog log;
    std::vector<beam::gui::TreatmentLogRow> planned;
    for (int i = 1; i <= 3; ++i) {
        beam::gui::TreatmentLogRow row = makeRow(1, "SCC" + std::to_string(i));
        row.sequence = i;
        row.startedAt.clear();
        planned.push_back(row);
    }
    log.seedVisit(1, "Default", planned);

    ASSERT_TRUE(log.insertAfter("Default", 1, 1, makeRow(1, "aMCC4")));
    ASSERT_EQ(log.rowsFor("Default", 1).size(), 4u);
    EXPECT_EQ(log.find("Default", 1, 2)->target, "aMCC4") << "inserted after the first";
    EXPECT_EQ(log.find("Default", 1, 3)->target, "SCC2") << "the rest moved down";
    EXPECT_FALSE(log.find("Default", 1, 2)->fromPlan);
    EXPECT_FALSE(beam::gui::TreatmentLog::delivered(*log.find("Default", 1, 2)));

    ASSERT_TRUE(log.removeAt("Default", 1, 1));
    ASSERT_EQ(log.rowsFor("Default", 1).size(), 3u);
    EXPECT_EQ(log.find("Default", 1, 1)->target, "aMCC4") << "the gap closed";

    // And re-seeding leaves the operator's sequence alone.
    log.seedVisit(1, "Default", planned);
    EXPECT_EQ(log.find("Default", 1, 1)->target, "aMCC4");
}

TEST(TreatmentLog, ADeliveredSonicationCannotBeSkippedOrShifted) {
    beam::gui::TreatmentLog log;
    std::vector<beam::gui::TreatmentLogRow> planned;
    for (int i = 1; i <= 3; ++i) {
        beam::gui::TreatmentLogRow row = makeRow(1, "SCC1");
        row.sequence = i;
        row.startedAt.clear();
        planned.push_back(row);
    }
    log.seedVisit(1, "Default", planned);

    beam::gui::TreatmentLogRow fired = makeRow(1, "SCC1");
    fired.protocol = "Default";
    fired.sequence = 2;
    fired.startedAt = "2026-10-14T10:00:00";
    log.record(std::move(fired));

    // Row 2 happened: it cannot be skipped, and nothing may be inserted above
    // it, because its sequence is where it happened.
    EXPECT_FALSE(log.removeAt("Default", 1, 2));
    EXPECT_FALSE(log.insertAfter("Default", 1, 1, makeRow(1, "aMCC1")));
    // Below it is still free.
    EXPECT_TRUE(log.insertAfter("Default", 1, 3, makeRow(1, "aMCC1")));
    EXPECT_TRUE(log.removeAt("Default", 1, 3));
}

// "The software logs the position, stimulation parameters, and correction
// values for every sonication" -- the operator's manual. The correction values
// are setLogInfoToCSV.m's AttenuationVal and DC, which it reads from
// app.sys.log{i} and BeamV0 leaves at 0.
TEST(TreatmentLog, CorrectionValuesAreLoggedAndSurviveAReload) {
    beam::gui::TreatmentLog log;
    beam::gui::TreatmentLogRow fired = makeRow(1, "SCC1");
    fired.attenuation = 0.8123;
    fired.dutyCycle = 0.0625;
    log.append(std::move(fired));
    // A sham sends nothing, so it has neither.
    beam::gui::TreatmentLogRow sham = makeRow(1, "SCC2");
    sham.sham = true;
    log.append(std::move(sham));

    const std::string path = scratch("treatment_log_correction.csv");
    ASSERT_NO_THROW(log.save(path));
    beam::gui::TreatmentLog reloaded;
    ASSERT_NO_THROW(reloaded.load(path));

    const beam::gui::TreatmentLogRow* back = reloaded.find("Default", 1, 1);
    ASSERT_NE(back, nullptr);
    EXPECT_DOUBLE_EQ(*back->attenuation, 0.8123);
    EXPECT_DOUBLE_EQ(*back->dutyCycle, 0.0625);
    // And the columns either side of them still line up.
    EXPECT_EQ(back->target, "SCC1");
    EXPECT_EQ(back->startedAt, "2026-10-14T09:15:00");

    const beam::gui::TreatmentLogRow* masked = reloaded.find("Default", 1, 2);
    ASSERT_NE(masked, nullptr);
    EXPECT_FALSE(masked->attenuation.has_value());
    EXPECT_FALSE(masked->dutyCycle.has_value());
    EXPECT_TRUE(masked->sham);
}

// The menu offering these asks the same question the operation answers, so an
// action that cannot work is disabled rather than refused after the fact.
TEST(TreatmentLog, WhatCanBeInsertedOrSkippedMatchesWhatIsAccepted) {
    beam::gui::TreatmentLog log;
    std::vector<beam::gui::TreatmentLogRow> planned;
    for (int i = 1; i <= 4; ++i) {
        beam::gui::TreatmentLogRow row = makeRow(1, "SCC1");
        row.sequence = i;
        row.startedAt.clear();
        planned.push_back(row);
    }
    log.seedVisit(1, "Default", planned);

    beam::gui::TreatmentLogRow fired = makeRow(1, "SCC1");
    fired.protocol = "Default";
    fired.sequence = 2;
    fired.startedAt = "2026-10-14T10:00:00";
    log.record(std::move(fired));

    // Row 1 is itself unfired, but row 2 below it has run, so neither its
    // insert nor its skip can go ahead -- and both say so in advance.
    EXPECT_FALSE(log.canInsertAfter("Default", 1, 1));
    EXPECT_FALSE(log.canRemoveAt("Default", 1, 1));
    EXPECT_FALSE(log.insertAfter("Default", 1, 1, makeRow(1, "aMCC1")));
    EXPECT_FALSE(log.removeAt("Default", 1, 1));

    // Row 2 has run, so it cannot be skipped, but there is nothing fired below
    // it and inserting after it is fine.
    EXPECT_FALSE(log.canRemoveAt("Default", 1, 2));
    EXPECT_TRUE(log.canInsertAfter("Default", 1, 2));
    EXPECT_TRUE(log.insertAfter("Default", 1, 2, makeRow(1, "aMCC1")));

    EXPECT_FALSE(log.canInsertAfter("Default", 1, 99)) << "no such position";
}

// Skipping closes the gap, so the visit's last position falls vacant. Treating
// that as "missing" and refilling it from the plan put the row straight back:
// a visit with several skips came out exactly as long as it started.
TEST(TreatmentLog, SkippedSonicationsStaySkippedAcrossReseeding) {
    beam::gui::TreatmentLog log;
    std::vector<beam::gui::TreatmentLogRow> planned;
    for (int i = 1; i <= 6; ++i) {
        beam::gui::TreatmentLogRow row = makeRow(1, "SCC" + std::to_string(i));
        row.sequence = i;
        row.startedAt.clear();
        planned.push_back(row);
    }
    log.seedVisit(1, "Default", planned);
    ASSERT_EQ(log.rowsFor("Default", 1).size(), 6u);

    // Skip three, the way an operator works down the table.
    ASSERT_TRUE(log.removeAt("Default", 1, 2));  // SCC2
    ASSERT_TRUE(log.removeAt("Default", 1, 2));  // what was SCC3, now at 2
    ASSERT_TRUE(log.removeAt("Default", 1, 2));  // what was SCC4
    ASSERT_EQ(log.rowsFor("Default", 1).size(), 3u);

    // Every redraw of the page re-seeds; the skips have to survive that.
    for (int redraw = 0; redraw < 3; ++redraw) log.seedVisit(1, "Default", planned);

    const std::vector<beam::gui::TreatmentLogRow> left = log.rowsFor("Default", 1);
    ASSERT_EQ(left.size(), 3u) << "a skipped sonication came back";
    EXPECT_EQ(left[0].target, "SCC1");
    EXPECT_EQ(left[1].target, "SCC5");
    EXPECT_EQ(left[2].target, "SCC6");
    EXPECT_EQ(left[0].sequence, 1);
    EXPECT_EQ(left[1].sequence, 2);
    EXPECT_EQ(left[2].sequence, 3);
}
