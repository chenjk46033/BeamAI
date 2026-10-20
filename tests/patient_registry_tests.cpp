// Tests for beam::gui::PatientRegistry -- the list of people this device has
// treated. BeamV0 has no equivalent; a new subject there meant a new copy of
// the data directory.

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include "gui/patient_registry.hpp"

namespace {

std::string scratch(const std::string& name) {
    return std::string(BEAM_TEST_SCRATCH) + "/" + name;
}

std::string writeTemp(const std::string& name, const std::string& body) {
    const std::string path = scratch(name);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << body;
    return path;
}

std::string contents(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

}  // namespace

// A device nobody has used yet. Refusing to start would be wrong.
TEST(PatientRegistry, AMissingFileIsAnEmptyRegistry) {
    beam::gui::PatientRegistry registry;
    ASSERT_NO_THROW(registry.load(scratch("patients_that_do_not_exist.csv")));
    EXPECT_TRUE(registry.empty());
    EXPECT_TRUE(registry.patients().empty());
}

TEST(PatientRegistry, AHeaderOnlyFileIsAnEmptyRegistry) {
    const std::string path =
        writeTemp("patients_header_only.csv", "# a comment\nid,externalId,site,createdAt\n");
    beam::gui::PatientRegistry registry;
    ASSERT_NO_THROW(registry.load(path));
    EXPECT_TRUE(registry.empty());
}

TEST(PatientRegistry, AddNumbersFromOneAndStamps) {
    beam::gui::PatientRegistry registry;
    const beam::gui::Patient first = registry.add("F040", "Site 1");
    const beam::gui::Patient second = registry.add("F041", "Site 1");

    EXPECT_EQ(first.id, 1);
    EXPECT_EQ(second.id, 2);
    EXPECT_EQ(first.externalId, "F040");
    EXPECT_EQ(first.site, "Site 1");
    // yyyy-mm-ddThh:mm:ss
    EXPECT_EQ(first.createdAt.size(), 19u);
    EXPECT_EQ(first.createdAt[4], '-');
    EXPECT_EQ(first.createdAt[10], 'T');
    EXPECT_EQ(registry.patients().size(), 2u);
}

TEST(PatientRegistry, RoundTripsThroughTheFile) {
    const std::string path = scratch("patients_round_trip.csv");
    beam::gui::PatientRegistry written;
    const beam::gui::Patient alpha = written.add("F040", "Site 1");
    const beam::gui::Patient beta = written.add("Smith, John", "Site 2");
    ASSERT_NO_THROW(written.save(path));

    beam::gui::PatientRegistry read;
    ASSERT_NO_THROW(read.load(path));
    ASSERT_EQ(read.patients().size(), 2u);
    EXPECT_EQ(read.patients()[0].id, alpha.id);
    EXPECT_EQ(read.patients()[0].externalId, "F040");
    EXPECT_EQ(read.patients()[0].site, "Site 1");
    EXPECT_EQ(read.patients()[0].createdAt, alpha.createdAt);
    // An identifier an operator typed can hold the separator.
    EXPECT_EQ(read.patients()[1].externalId, "Smith, John");
    EXPECT_EQ(read.patients()[1].id, beta.id);
}

TEST(PatientRegistry, SavingAnEmptyRegistryWritesAReadableFile) {
    const std::string path = scratch("patients_empty.csv");
    beam::gui::PatientRegistry written;
    ASSERT_NO_THROW(written.save(path));
    EXPECT_NE(contents(path).find("id,externalId,site,lastName,firstName,dateOfBirth,createdAt"),
              std::string::npos);

    beam::gui::PatientRegistry read;
    ASSERT_NO_THROW(read.load(path));
    EXPECT_TRUE(read.empty());
}

// Two rows for one person splits their history in half and nothing downstream
// would notice, so the second add returns the first record.
TEST(PatientRegistry, AddingAKnownExternalIdReturnsThatPatient) {
    beam::gui::PatientRegistry registry;
    const beam::gui::Patient first = registry.add("F040", "Site 1");
    const beam::gui::Patient again = registry.add("F040", "Site 1");

    EXPECT_EQ(again.id, first.id);
    EXPECT_EQ(registry.patients().size(), 1u);
}

// add() opens, it does not edit. It runs on every case acceptance, from a
// form that may still hold the last person typed into it.
TEST(PatientRegistry, AddingAKnownExternalIdChangesNothing) {
    beam::gui::PatientRegistry registry;
    registry.add("F040", "Site 1");
    const beam::gui::Patient moved = registry.add("F040", "Site 2");

    EXPECT_EQ(moved.id, 1);
    EXPECT_EQ(moved.site, "Site 1");
    ASSERT_EQ(registry.patients().size(), 1u);
    EXPECT_EQ(registry.patients().front().site, "Site 1");
}

TEST(PatientRegistry, AddingWithNoIdentifierIsRefused) {
    beam::gui::PatientRegistry registry;
    EXPECT_THROW(registry.add("", "Site 1"), std::runtime_error);
    EXPECT_THROW(registry.add("   ", "Site 1"), std::runtime_error);
    EXPECT_TRUE(registry.empty());
}

TEST(PatientRegistry, FindsByIdAndByExternalId) {
    beam::gui::PatientRegistry registry;
    registry.add("F040", "Site 1");
    registry.add("F041", "Site 2");

    beam::gui::Patient found;
    ASSERT_TRUE(registry.find(2, &found));
    EXPECT_EQ(found.externalId, "F041");
    ASSERT_TRUE(registry.findByExternalId("F040", &found));
    EXPECT_EQ(found.id, 1);

    beam::gui::Patient untouched;
    untouched.id = 99;
    EXPECT_FALSE(registry.find(7, &untouched));
    EXPECT_FALSE(registry.findByExternalId("F999", &untouched));
    EXPECT_EQ(untouched.id, 99);
    // A null out is the "is there such a patient" question.
    EXPECT_TRUE(registry.find(1, nullptr));
    EXPECT_FALSE(registry.find(7, nullptr));
}

TEST(PatientRegistry, RenameChangesTheIdentifierAndSite) {
    beam::gui::PatientRegistry registry;
    registry.add("F040", "Site 1");

    EXPECT_TRUE(registry.rename(1, "F040-B", "Site 3"));
    beam::gui::Patient found;
    ASSERT_TRUE(registry.find(1, &found));
    EXPECT_EQ(found.externalId, "F040-B");
    EXPECT_EQ(found.site, "Site 3");
    EXPECT_FALSE(registry.findByExternalId("F040", nullptr));
}

TEST(PatientRegistry, RenameRefusesAnIdentifierSomebodyElseHas) {
    beam::gui::PatientRegistry registry;
    registry.add("F040", "Site 1");
    registry.add("F041", "Site 1");

    EXPECT_FALSE(registry.rename(2, "F040", "Site 1"));
    EXPECT_FALSE(registry.rename(2, "", "Site 1"));
    EXPECT_FALSE(registry.rename(7, "F042", "Site 1"));

    beam::gui::Patient found;
    ASSERT_TRUE(registry.find(2, &found));
    EXPECT_EQ(found.externalId, "F041");
    // Renaming a patient to the identifier they already hold is not a clash.
    EXPECT_TRUE(registry.rename(2, "F041", "Site 9"));
}

// Reusing a departed patient's id would hand their directory, and their
// visits, to somebody else.
TEST(PatientRegistry, IdsAreNeverReused) {
    const std::string path = writeTemp("patients_gap.csv",
                                       "id,externalId,site,createdAt\n"
                                       "1,\"F040\",\"Site 1\",\"2026-01-01T09:00:00\"\n"
                                       "7,\"F047\",\"Site 1\",\"2026-02-01T09:00:00\"\n");
    beam::gui::PatientRegistry registry;
    ASSERT_NO_THROW(registry.load(path));
    EXPECT_EQ(registry.add("F048", "Site 1").id, 8);
}

// A half-read list of patients is worse than none: the operator would be
// offered a chooser that is quietly missing people.
TEST(PatientRegistry, MalformedRowsAreRefused) {
    beam::gui::PatientRegistry registry;

    EXPECT_THROW(registry.load(writeTemp("patients_short.csv",
                                         "id,externalId,site,createdAt\n"
                                         "1,\"F040\",\"Site 1\"\n")),
                 std::runtime_error);
    EXPECT_THROW(registry.load(writeTemp("patients_not_a_number.csv",
                                         "id,externalId,site,createdAt\n"
                                         "one,\"F040\",\"Site 1\",\"\"\n")),
                 std::runtime_error);
    EXPECT_THROW(registry.load(writeTemp("patients_zero_id.csv",
                                         "id,externalId,site,createdAt\n"
                                         "0,\"F040\",\"Site 1\",\"\"\n")),
                 std::runtime_error);
    EXPECT_THROW(registry.load(writeTemp("patients_no_identifier.csv",
                                         "id,externalId,site,createdAt\n"
                                         "1,\"\",\"Site 1\",\"\"\n")),
                 std::runtime_error);
    EXPECT_THROW(registry.load(writeTemp("patients_duplicate_id.csv",
                                         "id,externalId,site,createdAt\n"
                                         "1,\"F040\",\"Site 1\",\"\"\n"
                                         "1,\"F041\",\"Site 1\",\"\"\n")),
                 std::runtime_error);
    EXPECT_THROW(registry.load(writeTemp("patients_duplicate_person.csv",
                                         "id,externalId,site,createdAt\n"
                                         "1,\"F040\",\"Site 1\",\"\"\n"
                                         "2,\"F040\",\"Site 1\",\"\"\n")),
                 std::runtime_error);

    // Nothing from a refused file is left behind to be treated.
    EXPECT_TRUE(registry.empty());
}

TEST(PatientRegistry, RemoveDropsThePatient) {
    beam::gui::PatientRegistry registry;
    registry.add("F040", "Site 1");
    registry.add("F041", "Site 1");

    EXPECT_TRUE(registry.remove(1));
    EXPECT_FALSE(registry.find(1, nullptr));
    EXPECT_FALSE(registry.findByExternalId("F040", nullptr));
    ASSERT_EQ(registry.patients().size(), 1u);
    EXPECT_EQ(registry.patients().front().externalId, "F041");

    EXPECT_FALSE(registry.remove(1));  // already gone
    EXPECT_FALSE(registry.remove(99));
}

// The departed patient's directory must never be handed to somebody else, so
// their number is not released by removing them.
TEST(PatientRegistry, RemoveDoesNotReleaseTheId) {
    beam::gui::PatientRegistry registry;
    registry.add("F040", "Site 1");
    registry.add("F041", "Site 1");
    ASSERT_TRUE(registry.remove(2));

    EXPECT_EQ(registry.add("F042", "Site 1").id, 3);
    // And removing the one just issued still does not wind the numbering back.
    ASSERT_TRUE(registry.remove(3));
    EXPECT_EQ(registry.add("F043", "Site 1").id, 4);
}

TEST(PatientRegistry, RemoveSurvivesTheRoundTrip) {
    const std::string path = scratch("patients_removed.csv");
    beam::gui::PatientRegistry written;
    written.add("F040", "Site 1");
    written.add("F041", "Site 1");
    ASSERT_TRUE(written.remove(1));
    ASSERT_NO_THROW(written.save(path));

    beam::gui::PatientRegistry read;
    ASSERT_NO_THROW(read.load(path));
    ASSERT_EQ(read.patients().size(), 1u);
    EXPECT_EQ(read.patients().front().id, 2);
    EXPECT_EQ(read.patients().front().externalId, "F041");
    // The gap at 1 is not reused after a reload either.
    EXPECT_EQ(read.add("F042", "Site 1").id, 3);
}

// An identifier freed by a removal can be given to somebody new: it is the
// directory that must not be reused, not the operator's own name for them.
TEST(PatientRegistry, AnIdentifierFreedByRemovalCanBeUsedAgain) {
    beam::gui::PatientRegistry registry;
    registry.add("F040", "Site 1");
    ASSERT_TRUE(registry.remove(1));

    const beam::gui::Patient again = registry.add("F040", "Site 2");
    EXPECT_EQ(again.id, 2);
    EXPECT_EQ(again.site, "Site 2");
    EXPECT_EQ(registry.patients().size(), 1u);
}

TEST(PatientRegistry, DemographicsRoundTrip) {
    const std::string path = scratch("patients_demographics.csv");
    beam::gui::PatientRegistry written;
    beam::gui::Patient details;
    details.externalId = "F040";
    details.site = "Site 1";
    details.lastName = "O'Brien, Jr";  // a name can hold the separator
    details.firstName = "Mary Anne";
    details.dateOfBirth = "1961-04-02";
    const beam::gui::Patient added = written.add(details);
    EXPECT_EQ(added.id, 1);
    ASSERT_NO_THROW(written.save(path));

    beam::gui::PatientRegistry read;
    ASSERT_NO_THROW(read.load(path));
    ASSERT_EQ(read.patients().size(), 1u);
    const beam::gui::Patient& back = read.patients().front();
    EXPECT_EQ(back.lastName, "O'Brien, Jr");
    EXPECT_EQ(back.firstName, "Mary Anne");
    EXPECT_EQ(back.dateOfBirth, "1961-04-02");
    EXPECT_EQ(back.site, "Site 1");
}

// Devices were in use before demographics existed. Their patient list has
// four columns and must go on loading.
TEST(PatientRegistry, AFileWrittenBeforeDemographicsStillLoads) {
    const std::string path = writeTemp("patients_legacy.csv",
                                       "# Who this device has treated.\n"
                                       "id,externalId,site,createdAt\n"
                                       "1,\"p001\",\"s001\",\"2026-10-20T11:02:17\"\n"
                                       "2,\"p002\",\"s001\",\"2026-10-20T11:04:52\"\n");
    beam::gui::PatientRegistry registry;
    ASSERT_NO_THROW(registry.load(path));
    ASSERT_EQ(registry.patients().size(), 2u);
    EXPECT_EQ(registry.patients()[0].externalId, "p001");
    EXPECT_EQ(registry.patients()[0].createdAt, "2026-10-20T11:02:17");
    EXPECT_TRUE(registry.patients()[0].lastName.empty());
    EXPECT_TRUE(registry.patients()[0].dateOfBirth.empty());
    // And numbering carries on from the rows, not from one.
    EXPECT_EQ(registry.add("p003", "s001").id, 3);
}

// A stale Case form must not rewrite whoever owns the identifier typed into
// it. This is the bug that turned "Doe, John" into the name left in the boxes
// from the patient before him.
TEST(PatientRegistry, AddNeverRewritesAnExistingPatient) {
    beam::gui::PatientRegistry registry;
    beam::gui::Patient details;
    details.externalId = "F040";
    details.site = "Site 1";
    details.lastName = "Kowalski";
    details.firstName = "Ada";
    details.dateOfBirth = "1970-01-05";
    registry.add(details);

    beam::gui::Patient blank;
    blank.externalId = "F040";
    blank.site = "Site 1";
    const beam::gui::Patient after = registry.add(blank);
    EXPECT_EQ(after.lastName, "Kowalski");
    EXPECT_EQ(after.firstName, "Ada");
    EXPECT_EQ(after.dateOfBirth, "1970-01-05");

    // Nor does a form holding somebody else's name reach the record.
    beam::gui::Patient stale;
    stale.externalId = "F040";
    stale.site = "Site 9";
    stale.lastName = "Testerson";
    stale.firstName = "Pat";
    stale.dateOfBirth = "2001-02-03";
    const beam::gui::Patient untouched = registry.add(stale);
    EXPECT_EQ(untouched.lastName, "Kowalski");
    EXPECT_EQ(untouched.firstName, "Ada");
    EXPECT_EQ(untouched.dateOfBirth, "1970-01-05");
    EXPECT_EQ(untouched.site, "Site 1");
    EXPECT_EQ(registry.patients().size(), 1u);
}

// The editor is the opposite: what it is given is what is stored, so a name
// typed by mistake can be cleared.
TEST(PatientRegistry, UpdateTakesEmptyFieldsAsGiven) {
    beam::gui::PatientRegistry registry;
    beam::gui::Patient details;
    details.externalId = "F040";
    details.site = "Site 1";
    details.lastName = "Typo";
    details.firstName = "Typo";
    details.dateOfBirth = "1970-01-05";
    registry.add(details);

    beam::gui::Patient cleared;
    cleared.externalId = "F040";
    cleared.site = "Site 1";
    EXPECT_TRUE(registry.update(1, cleared));
    const beam::gui::Patient& back = registry.patients().front();
    EXPECT_TRUE(back.lastName.empty());
    EXPECT_TRUE(back.firstName.empty());
    EXPECT_TRUE(back.dateOfBirth.empty());
    EXPECT_EQ(back.externalId, "F040");
}

TEST(PatientRegistry, UpdateRefusesABlankOrTakenIdentifier) {
    beam::gui::PatientRegistry registry;
    registry.add("F040", "Site 1");
    registry.add("F041", "Site 1");

    beam::gui::Patient taken;
    taken.externalId = "F040";
    EXPECT_FALSE(registry.update(2, taken));

    beam::gui::Patient blank;
    EXPECT_FALSE(registry.update(2, blank));

    beam::gui::Patient missing;
    missing.externalId = "F099";
    EXPECT_FALSE(registry.update(7, missing));

    EXPECT_EQ(registry.patients()[1].externalId, "F041");
}

// rename() is the narrow edit and leaves demographics alone.
TEST(PatientRegistry, RenameKeepsTheDemographics) {
    beam::gui::PatientRegistry registry;
    beam::gui::Patient details;
    details.externalId = "F040";
    details.site = "Site 1";
    details.lastName = "Kowalski";
    details.firstName = "Ada";
    details.dateOfBirth = "1970-01-05";
    registry.add(details);

    ASSERT_TRUE(registry.rename(1, "F040-B", "Site 3"));
    const beam::gui::Patient& back = registry.patients().front();
    EXPECT_EQ(back.externalId, "F040-B");
    EXPECT_EQ(back.site, "Site 3");
    EXPECT_EQ(back.lastName, "Kowalski");
    EXPECT_EQ(back.firstName, "Ada");
    EXPECT_EQ(back.dateOfBirth, "1970-01-05");
}

TEST(PatientRegistry, DisplayNameFallsBackToTheParticipantId) {
    beam::gui::Patient patient;
    patient.externalId = "p001";
    EXPECT_EQ(beam::gui::patientDisplayName(patient), "p001");

    patient.lastName = "Kowalski";
    EXPECT_EQ(beam::gui::patientDisplayName(patient), "Kowalski");

    patient.firstName = "Ada";
    EXPECT_EQ(beam::gui::patientDisplayName(patient), "Kowalski, Ada");

    patient.lastName.clear();
    EXPECT_EQ(beam::gui::patientDisplayName(patient), "Ada");
}
