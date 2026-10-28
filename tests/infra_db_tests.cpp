#include <gtest/gtest.h>

#include "gui/patient_registry_db.hpp"
#include "gui/treatment_log_db.hpp"
#include "gui/treatment_target_store_db.hpp"
#include "infra_db/schema.hpp"

namespace {

using beam::db::Database;
using beam::db::DatabaseError;
using beam::db::Transaction;

Database freshStore() {
    Database database(":memory:");
    beam::db::applySchema(database);
    return database;
}

long long addPatient(Database& database, const std::string& externalId) {
    auto insert = database.prepare(
        "INSERT INTO patient (external_id, site) VALUES (?, ?)");
    insert.bind(1, externalId).bind(2, std::string("CCF")).run();
    return database.lastInsertId();
}

long long countOf(Database& database, const std::string& table) {
    auto count = database.prepare("SELECT COUNT(*) FROM " + table);
    EXPECT_TRUE(count.step());
    return count.row().integer(0);
}

TEST(InfraDb, AppliesSchemaAndStampsItsVersion) {
    Database database(":memory:");
    EXPECT_EQ(database.userVersion(), 0);
    beam::db::applySchema(database);
    EXPECT_EQ(database.userVersion(), beam::db::schemaVersion());
    // Idempotent: opening an up-to-date store must not rebuild it.
    beam::db::applySchema(database);
    EXPECT_EQ(database.userVersion(), beam::db::schemaVersion());
}

TEST(InfraDb, RefusesAStoreFromALaterVersion) {
    Database database(":memory:");
    database.setUserVersion(beam::db::schemaVersion() + 1);
    EXPECT_THROW(beam::db::applySchema(database), DatabaseError);
}

TEST(InfraDb, RoundTripsAPatient) {
    Database database = freshStore();
    const long long id = addPatient(database, "7");
    auto read = database.prepare("SELECT external_id, site FROM patient WHERE id = ?");
    read.bind(1, id);
    ASSERT_TRUE(read.step());
    EXPECT_EQ(read.row().text(0), "7");
    EXPECT_EQ(read.row().text(1), "CCF");
    EXPECT_FALSE(read.step());
}

TEST(InfraDb, ParticipantIdIsUnique) {
    Database database = freshStore();
    addPatient(database, "7");
    EXPECT_THROW(addPatient(database, "7"), DatabaseError);
}

// The reason for the database. A visit's state spans tables, and a failure
// part way through must leave none of it rather than some of it.
TEST(InfraDb, AnAbandonedTransactionLeavesNothingBehind) {
    Database database = freshStore();
    const long long patient = addPatient(database, "7");
    {
        Transaction work(database);
        auto visit = database.prepare(
            "INSERT INTO visit (patient_id, number, protocol) VALUES (?, ?, ?)");
        visit.bind(1, patient).bind(2, 1LL).bind(3, std::string("Default")).run();
        auto sonication = database.prepare(
            "INSERT INTO sonication (visit_id, sequence, target) VALUES (?, ?, ?)");
        sonication.bind(1, database.lastInsertId()).bind(2, 1LL)
            .bind(3, std::string("SCC1")).run();
        // Falls out of scope without commit, as an exception would.
    }
    EXPECT_EQ(countOf(database, "visit"), 0);
    EXPECT_EQ(countOf(database, "sonication"), 0);
    EXPECT_EQ(countOf(database, "patient"), 1);
}

TEST(InfraDb, ACommittedTransactionKeepsEverything) {
    Database database = freshStore();
    const long long patient = addPatient(database, "7");
    {
        Transaction work(database);
        auto visit = database.prepare(
            "INSERT INTO visit (patient_id, number, protocol) VALUES (?, ?, ?)");
        visit.bind(1, patient).bind(2, 1LL).bind(3, std::string("Default")).run();
        work.commit();
    }
    EXPECT_EQ(countOf(database, "visit"), 1);
}

// Relationships are enforced, not just documented: an orphan visit cannot be
// written, and deleting a patient takes their record with them.
TEST(InfraDb, EnforcesTheVisitToPatientRelationship) {
    Database database = freshStore();
    auto orphan = database.prepare(
        "INSERT INTO visit (patient_id, number) VALUES (?, ?)");
    orphan.bind(1, 999LL).bind(2, 1LL);
    EXPECT_THROW(orphan.run(), DatabaseError);
}

TEST(InfraDb, DeletingAPatientTakesTheirVisitsAndSonications) {
    Database database = freshStore();
    const long long patient = addPatient(database, "7");
    Transaction work(database);
    auto visit = database.prepare("INSERT INTO visit (patient_id, number) VALUES (?, ?)");
    visit.bind(1, patient).bind(2, 1LL).run();
    const long long visitId = database.lastInsertId();
    auto sonication =
        database.prepare("INSERT INTO sonication (visit_id, sequence) VALUES (?, ?)");
    sonication.bind(1, visitId).bind(2, 1LL).run();
    work.commit();

    auto remove = database.prepare("DELETE FROM patient WHERE id = ?");
    remove.bind(1, patient).run();
    EXPECT_EQ(countOf(database, "visit"), 0);
    EXPECT_EQ(countOf(database, "sonication"), 0);
}

// One scan, many visits: a second visit points at the same imaging row rather
// than recording the link again.
TEST(InfraDb, OneImagingRowServesManyVisits) {
    Database database = freshStore();
    const long long patient = addPatient(database, "7");
    auto imaging = database.prepare(
        "INSERT INTO imaging (patient_id, mri_path, fingerprint) VALUES (?, ?, ?)");
    imaging.bind(1, patient)
        .bind(2, std::string("C:/scans/F040.nii"))
        .bind(3, std::string("192:-89.8..158.5"))
        .run();
    const long long scan = database.lastInsertId();

    auto visit = database.prepare(
        "INSERT INTO visit (patient_id, number, imaging_id) VALUES (?, ?, ?)");
    visit.bind(1, patient).bind(2, 1LL).bind(3, scan).run();
    visit.bind(1, patient).bind(2, 2LL).bind(3, scan).run();

    auto shared = database.prepare(
        "SELECT COUNT(DISTINCT imaging_id) FROM visit WHERE patient_id = ?");
    shared.bind(1, patient);
    ASSERT_TRUE(shared.step());
    EXPECT_EQ(shared.row().integer(0), 1);
    EXPECT_EQ(countOf(database, "imaging"), 1);
}

TEST(InfraDb, TheSameScanIsNotRecordedTwiceForOnePatient) {
    Database database = freshStore();
    const long long patient = addPatient(database, "7");
    auto imaging = database.prepare(
        "INSERT INTO imaging (patient_id, mri_path, fingerprint) VALUES (?, ?, ?)");
    imaging.bind(1, patient).bind(2, std::string("a.nii"))
        .bind(3, std::string("same")).run();
    imaging.bind(1, patient).bind(2, std::string("b.nii")).bind(3, std::string("same"));
    EXPECT_THROW(imaging.run(), DatabaseError);
}

// The point of one file: a question that spans patients is a query.
TEST(InfraDb, QueriesAcrossPatients) {
    Database database = freshStore();
    auto visit = database.prepare("INSERT INTO visit (patient_id, number) VALUES (?, ?)");
    auto sonication = database.prepare(
        "INSERT INTO sonication (visit_id, sequence, amplitude) VALUES (?, ?, ?)");
    for (const char* who : {"1", "2", "3"}) {
        const long long patient = addPatient(database, who);
        visit.bind(1, patient).bind(2, 1LL).run();
        const long long visitId = database.lastInsertId();
        sonication.bind(1, visitId).bind(2, 1LL).bind(3, 0.5).run();
        sonication.bind(1, visitId).bind(2, 2LL).bind(3, 0.8).run();
    }
    auto loud = database.prepare(
        "SELECT p.external_id, COUNT(*) FROM sonication s "
        "JOIN visit v ON v.id = s.visit_id "
        "JOIN patient p ON p.id = v.patient_id "
        "WHERE s.amplitude > 0.6 GROUP BY p.id ORDER BY p.external_id");
    int found = 0;
    while (loud.step()) {
        EXPECT_EQ(loud.row().integer(1), 1);
        ++found;
    }
    EXPECT_EQ(found, 3);
}


// ---- the patient registry over the database -------------------------------

using beam::gui::Patient;
using beam::gui::PatientRegistry;

PatientRegistry registryWith(std::initializer_list<const char*> ids) {
    PatientRegistry registry;
    for (const char* id : ids) registry.add(id, "CCF");
    return registry;
}

TEST(PatientRegistryDb, RoundTripsEveryField) {
    Database database = freshStore();
    PatientRegistry written;
    Patient details;
    details.externalId = "14";
    details.site = "UUtah";
    details.lastName = "Chen";
    details.firstName = "JK";
    details.dateOfBirth = "1980-02-29";
    const Patient added = written.add(details);

    beam::gui::savePatients(database, written);
    PatientRegistry read;
    beam::gui::loadPatients(database, &read);

    ASSERT_EQ(read.patients().size(), 1u);
    const Patient& back = read.patients().front();
    EXPECT_EQ(back.id, added.id);
    EXPECT_EQ(back.externalId, "14");
    EXPECT_EQ(back.site, "UUtah");
    EXPECT_EQ(back.lastName, "Chen");
    EXPECT_EQ(back.firstName, "JK");
    EXPECT_EQ(back.dateOfBirth, "1980-02-29");
    EXPECT_EQ(back.createdAt, added.createdAt);
}

TEST(PatientRegistryDb, SavingReplacesRatherThanAccumulates) {
    Database database = freshStore();
    PatientRegistry registry = registryWith({"1", "2", "3"});
    beam::gui::savePatients(database, registry);
    beam::gui::savePatients(database, registry);
    PatientRegistry read;
    beam::gui::loadPatients(database, &read);
    EXPECT_EQ(read.patients().size(), 3u);
}

TEST(PatientRegistryDb, ARemovedPatientDisappears) {
    Database database = freshStore();
    PatientRegistry registry = registryWith({"1", "2"});
    const int second = registry.patients().back().id;
    ASSERT_TRUE(registry.remove(second));
    beam::gui::savePatients(database, registry);

    PatientRegistry read;
    beam::gui::loadPatients(database, &read);
    ASSERT_EQ(read.patients().size(), 1u);
    EXPECT_EQ(read.patients().front().externalId, "1");
}

// The guarantee the CSV carried in a marker rather than deriving from rows: a
// departed patient's number is never handed to the next person, because their
// directory is named after it.
TEST(PatientRegistryDb, ANumberIsNeverReissuedAfterRemoval) {
    Database database = freshStore();
    PatientRegistry registry = registryWith({"1", "2"});
    const int highest = registry.patients().back().id;
    ASSERT_TRUE(registry.remove(highest));
    beam::gui::savePatients(database, registry);

    PatientRegistry read;
    beam::gui::loadPatients(database, &read);
    const Patient next = read.add("3", "CCF");
    EXPECT_GT(next.id, highest);
}

TEST(PatientRegistryDb, AnEmptyRegistryLoadsAsEmpty) {
    Database database = freshStore();
    PatientRegistry registry;
    beam::gui::savePatients(database, registry);
    PatientRegistry read;
    beam::gui::loadPatients(database, &read);
    EXPECT_TRUE(read.empty());
    EXPECT_EQ(read.add("1", "CCF").id, 1);
}


// ---- the treatment log over the database ----------------------------------

using beam::gui::TreatmentLog;
using beam::gui::TreatmentLogRow;

TreatmentLogRow deliveredRow(int visit, int sequence, const char* target) {
    TreatmentLogRow row;
    row.visit = visit;
    row.sequence = sequence;
    row.protocol = "Default";
    row.target = target;
    row.xMm = 31.5; row.yMm = -2.25; row.zMm = 17.75;
    row.amplitude = 0.65;
    row.startTimeSeconds = 0.0;
    row.endTimeSeconds = 30.0;
    row.burstDurationSeconds = 0.06;
    row.burstIntervalSeconds = 1.1;
    row.pulseDurationSeconds = 0.005;
    row.pulseIntervalSeconds = 0.01;
    row.sham = false;
    row.statusCode = 200;
    row.startedAt = "2026-10-28T11:02:03";
    row.endedAt = "2026-10-28T11:02:33";
    row.fromPlan = true;
    row.deviceModel = "BEAM-1";
    row.deviceSerial = "SN-0042";
    row.calDuty = "12.5";
    row.calPressure = "0.84";
    row.notes = "steady";
    row.lockHorizontal = 3;
    row.lockVertical = 2;
    row.mriFile = "C:/scans/p1/t1.nii.gz";
    row.mriFingerprint = "256x256x176@1.0";
    row.fiducialsMm = "12.0;-8.5;3.25";
    return row;
}

int patientRow(Database& database, const char* externalId) {
    return static_cast<int>(addPatient(database, externalId));
}

TEST(TreatmentLogDb, RoundTripsADeliveredSonication) {
    Database database = freshStore();
    const int patient = patientRow(database, "1");
    TreatmentLog written;
    written.adopt({deliveredRow(1, 1, "SCC1")});
    beam::gui::saveLog(database, patient, written);

    TreatmentLog read;
    beam::gui::loadLog(database, patient, &read);
    ASSERT_EQ(read.rows().size(), 1u);
    const TreatmentLogRow& back = read.rows().front();
    const TreatmentLogRow& sent = written.rows().front();
    EXPECT_EQ(back.visit, sent.visit);
    EXPECT_EQ(back.sequence, sent.sequence);
    EXPECT_EQ(back.protocol, sent.protocol);
    EXPECT_EQ(back.target, sent.target);
    EXPECT_DOUBLE_EQ(back.xMm, sent.xMm);
    EXPECT_DOUBLE_EQ(back.yMm, sent.yMm);
    EXPECT_DOUBLE_EQ(back.zMm, sent.zMm);
    EXPECT_DOUBLE_EQ(back.amplitude, sent.amplitude);
    EXPECT_DOUBLE_EQ(back.burstIntervalSeconds, sent.burstIntervalSeconds);
    EXPECT_EQ(back.sham, sent.sham);
    EXPECT_EQ(back.statusCode, sent.statusCode);
    EXPECT_EQ(back.startedAt, sent.startedAt);
    EXPECT_EQ(back.endedAt, sent.endedAt);
    EXPECT_EQ(back.fromPlan, sent.fromPlan);
    EXPECT_EQ(back.deviceSerial, sent.deviceSerial);
    EXPECT_EQ(back.calPressure, sent.calPressure);
    EXPECT_EQ(back.notes, sent.notes);
}

// Normalised out of the row and joined back on, so the caller sees what it
// always saw while the store keeps one copy per visit.
TEST(TreatmentLogDb, LockPositionSurvivesNormalisation) {
    Database database = freshStore();
    const int patient = patientRow(database, "1");
    TreatmentLog written;
    written.adopt({deliveredRow(1, 1, "SCC1"), deliveredRow(1, 2, "SCC2")});
    beam::gui::saveLog(database, patient, written);

    TreatmentLog read;
    beam::gui::loadLog(database, patient, &read);
    ASSERT_EQ(read.rows().size(), 2u);
    for (const TreatmentLogRow& row : read.rows()) {
        EXPECT_EQ(row.lockHorizontal, 3);
        EXPECT_EQ(row.lockVertical, 2);
    }
    // One visit row, not one per sonication.
    EXPECT_EQ(countOf(database, "visit"), 1);
}

TEST(TreatmentLogDb, KeepsVisitsApart) {
    Database database = freshStore();
    const int patient = patientRow(database, "1");
    TreatmentLog written;
    written.adopt({deliveredRow(1, 1, "SCC1"), deliveredRow(2, 1, "aMCC1")});
    beam::gui::saveLog(database, patient, written);
    EXPECT_EQ(countOf(database, "visit"), 2);

    TreatmentLog read;
    beam::gui::loadLog(database, patient, &read);
    ASSERT_EQ(read.rows().size(), 2u);
    EXPECT_EQ(read.rows()[0].visit, 1);
    EXPECT_EQ(read.rows()[1].visit, 2);
}

TEST(TreatmentLogDb, OnePatientsLogDoesNotLeakIntoAnothers) {
    Database database = freshStore();
    const int first = patientRow(database, "1");
    const int second = patientRow(database, "2");
    TreatmentLog mine;
    mine.adopt({deliveredRow(1, 1, "SCC1")});
    beam::gui::saveLog(database, first, mine);

    TreatmentLog theirs;
    beam::gui::loadLog(database, second, &theirs);
    EXPECT_TRUE(theirs.rows().empty());
}

TEST(TreatmentLogDb, SavingTwiceDoesNotDuplicate) {
    Database database = freshStore();
    const int patient = patientRow(database, "1");
    TreatmentLog written;
    written.adopt({deliveredRow(1, 1, "SCC1")});
    beam::gui::saveLog(database, patient, written);
    beam::gui::saveLog(database, patient, written);
    EXPECT_EQ(countOf(database, "sonication"), 1);
    EXPECT_EQ(countOf(database, "visit"), 1);
}

// The log shares the visit row with the visit state, so saving it must not
// destroy the imaging link or the fields it knows nothing about.
TEST(TreatmentLogDb, SavingTheLogLeavesVisitStateAlone) {
    Database database = freshStore();
    const int patient = patientRow(database, "1");
    TreatmentLog written;
    written.adopt({deliveredRow(1, 1, "SCC1")});
    beam::gui::saveLog(database, patient, written);

    auto imaging = database.prepare(
        "INSERT INTO imaging (patient_id, mri_path, fingerprint) VALUES (?, ?, ?)");
    imaging.bind(1, static_cast<long long>(patient))
        .bind(2, std::string("C:/scans/F040.nii"))
        .bind(3, std::string("fp-1"))
        .run();
    const long long scan = database.lastInsertId();
    auto attach = database.prepare(
        "UPDATE visit SET imaging_id = ?, auto_rank = 0, current_sonication = 4 "
        "WHERE patient_id = ?");
    attach.bind(1, scan).bind(2, static_cast<long long>(patient)).run();

    // Re-save the log, as a delivery would.
    written.adopt({deliveredRow(1, 1, "SCC1"), deliveredRow(1, 2, "SCC2")});
    beam::gui::saveLog(database, patient, written);

    auto check = database.prepare(
        "SELECT imaging_id, auto_rank, current_sonication FROM visit WHERE patient_id = ?");
    check.bind(1, static_cast<long long>(patient));
    ASSERT_TRUE(check.step());
    // The cursor and the ranking flag are not this writer's to touch.
    EXPECT_EQ(check.row().integer(1), 0);
    EXPECT_EQ(check.row().integer(2), 4);
    // The scan the rows were delivered on wins over one attached behind them.
    EXPECT_NE(check.row().integer(0), scan);

    TreatmentLog read;
    beam::gui::loadLog(database, patient, &read);
    ASSERT_EQ(read.rows().size(), 2u);
    EXPECT_EQ(read.rows().front().mriFile, "C:/scans/p1/t1.nii.gz");
    EXPECT_EQ(read.rows().front().mriFingerprint, "256x256x176@1.0");
}

// Rows carrying no scan must not clear a link already on the visit.
TEST(TreatmentLogDb, RowsWithNoScanLeaveAnAttachedOneAlone) {
    Database database = freshStore();
    const int patient = patientRow(database, "1");
    TreatmentLogRow bare = deliveredRow(1, 1, "SCC1");
    bare.mriFile.clear();
    bare.mriFingerprint.clear();
    bare.fiducialsMm.clear();
    TreatmentLog written;
    written.adopt({bare});
    beam::gui::saveLog(database, patient, written);

    auto imaging = database.prepare(
        "INSERT INTO imaging (patient_id, mri_path, fingerprint) VALUES (?, ?, ?)");
    imaging.bind(1, static_cast<long long>(patient))
        .bind(2, std::string("C:/scans/F040.nii"))
        .bind(3, std::string("fp-1"))
        .run();
    const long long scan = database.lastInsertId();
    auto attach = database.prepare("UPDATE visit SET imaging_id = ? WHERE patient_id = ?");
    attach.bind(1, scan).bind(2, static_cast<long long>(patient)).run();

    beam::gui::saveLog(database, patient, written);
    auto check = database.prepare("SELECT imaging_id FROM visit WHERE patient_id = ?");
    check.bind(1, static_cast<long long>(patient));
    ASSERT_TRUE(check.step());
    EXPECT_EQ(check.row().integer(0), scan);
}


// ---- a patient's own targets over the database -----------------------------

using beam::gui::TreatmentTarget;
using beam::gui::TreatmentTargetPoint;
using beam::gui::TreatmentTargetStore;

TreatmentTargetPoint placedPoint(int order, double x, double y, double z) {
    TreatmentTargetPoint point;
    point.order = order;
    point.show = true;
    point.amplitude = 0.65;
    point.startTimeSeconds = 0.0;
    point.endTimeSeconds = 30.0;
    point.burstDurationSeconds = 0.06;
    point.burstIntervalSeconds = 1.1;
    point.pulseDurationSeconds = 0.005;
    point.pulseIntervalSeconds = 0.01;
    point.xMm = x;
    point.yMm = y;
    point.zMm = z;
    return point;
}

// The capability that did not exist: a placement the operator made survives
// being put away and read back.
TEST(TargetStoreDb, APlacedCoordinateSurvives) {
    Database database = freshStore();
    const int patient = patientRow(database, "1");
    TreatmentTargetStore written;
    written.adoptTargets({TreatmentTarget{"SCC1", {placedPoint(1, 31.49, 35.12, 3.02)}}});
    beam::gui::saveTargets(database, patient, written);

    TreatmentTargetStore read;
    beam::gui::loadTargets(database, patient, &read);
    const TreatmentTarget* back = read.findTarget("SCC1");
    ASSERT_NE(back, nullptr);
    ASSERT_EQ(back->points.size(), 1u);
    EXPECT_DOUBLE_EQ(back->points.front().xMm, 31.49);
    EXPECT_DOUBLE_EQ(back->points.front().yMm, 35.12);
    EXPECT_DOUBLE_EQ(back->points.front().zMm, 3.02);
}

TEST(TargetStoreDb, RoundTripsParametersAlongsideThePlacement) {
    Database database = freshStore();
    const int patient = patientRow(database, "1");
    TreatmentTargetStore written;
    written.adoptTargets({TreatmentTarget{"SCC1", {placedPoint(1, 1.0, 2.0, 3.0)}}});
    beam::gui::saveTargets(database, patient, written);

    TreatmentTargetStore read;
    beam::gui::loadTargets(database, patient, &read);
    const TreatmentTargetPoint& back = read.findTarget("SCC1")->points.front();
    EXPECT_DOUBLE_EQ(back.amplitude, 0.65);
    EXPECT_DOUBLE_EQ(back.burstIntervalSeconds, 1.1);
    EXPECT_DOUBLE_EQ(back.pulseDurationSeconds, 0.005);
    EXPECT_TRUE(back.show);
    EXPECT_EQ(back.order, 1);
}

TEST(TargetStoreDb, KeepsEveryPointOfAMultiPointTarget) {
    Database database = freshStore();
    const int patient = patientRow(database, "1");
    TreatmentTargetStore written;
    written.adoptTargets({TreatmentTarget{
        "SCC1", {placedPoint(1, 10, 11, 12), placedPoint(2, 20, 21, 22),
                 placedPoint(3, 30, 31, 32)}}});
    beam::gui::saveTargets(database, patient, written);

    TreatmentTargetStore read;
    beam::gui::loadTargets(database, patient, &read);
    const TreatmentTarget* back = read.findTarget("SCC1");
    ASSERT_NE(back, nullptr);
    ASSERT_EQ(back->points.size(), 3u);
    EXPECT_DOUBLE_EQ(back->points[1].xMm, 20);
    EXPECT_EQ(back->points[2].order, 3);
}

TEST(TargetStoreDb, OnePatientsPlacementIsNotAnothers) {
    Database database = freshStore();
    const int first = patientRow(database, "1");
    const int second = patientRow(database, "2");
    TreatmentTargetStore mine;
    mine.adoptTargets({TreatmentTarget{"SCC1", {placedPoint(1, 31.49, 35.12, 3.02)}}});
    beam::gui::saveTargets(database, first, mine);
    TreatmentTargetStore theirs;
    theirs.adoptTargets({TreatmentTarget{"SCC1", {placedPoint(1, -5, -6, -7)}}});
    beam::gui::saveTargets(database, second, theirs);

    TreatmentTargetStore readFirst;
    beam::gui::loadTargets(database, first, &readFirst);
    EXPECT_DOUBLE_EQ(readFirst.findTarget("SCC1")->points.front().xMm, 31.49);
    TreatmentTargetStore readSecond;
    beam::gui::loadTargets(database, second, &readSecond);
    EXPECT_DOUBLE_EQ(readSecond.findTarget("SCC1")->points.front().xMm, -5);
}

TEST(TargetStoreDb, SavingTwiceDoesNotDuplicate) {
    Database database = freshStore();
    const int patient = patientRow(database, "1");
    TreatmentTargetStore written;
    written.adoptTargets({TreatmentTarget{"SCC1", {placedPoint(1, 1, 2, 3)}}});
    beam::gui::saveTargets(database, patient, written);
    beam::gui::saveTargets(database, patient, written);
    EXPECT_EQ(countOf(database, "target"), 1);
}


// Saving the registry must not disturb anything hanging off a patient: every
// child table cascades on patient(id).
TEST(PatientRegistryDb, SavingTheRegistryKeepsVisitsThatAlreadyExist) {
    Database database = freshStore();
    PatientRegistry registry;
    const Patient added = registry.add("1", "IU");
    beam::gui::savePatients(database, registry);

    auto visit = database.prepare(
        "INSERT INTO visit (patient_id, number, protocol) VALUES (?, ?, ?)");
    visit.bind(1, static_cast<long long>(added.id)).bind(2, 1LL)
        .bind(3, std::string("Default")).run();
    ASSERT_EQ(countOf(database, "visit"), 1);

    // Anything that touches the registry: renaming, adding somebody else, or
    // simply saving again.
    beam::gui::savePatients(database, registry);
    EXPECT_EQ(countOf(database, "visit"), 1);

    registry.add("2", "IU");
    beam::gui::savePatients(database, registry);
    EXPECT_EQ(countOf(database, "visit"), 1);

    registry.rename(added.id, "1-renamed", "CCF");
    beam::gui::savePatients(database, registry);
    EXPECT_EQ(countOf(database, "visit"), 1);
}

TEST(PatientRegistryDb, SavingTheRegistryKeepsImagingAndTargets) {
    Database database = freshStore();
    PatientRegistry registry;
    const Patient added = registry.add("1", "IU");
    beam::gui::savePatients(database, registry);

    auto imaging = database.prepare(
        "INSERT INTO imaging (patient_id, mri_path, fingerprint) VALUES (?, ?, ?)");
    imaging.bind(1, static_cast<long long>(added.id))
        .bind(2, std::string("C:/scans/F040.nii"))
        .bind(3, std::string("fp-1"))
        .run();
    auto target = database.prepare(
        "INSERT INTO target (patient_id, name, ordinal, x_mm) VALUES (?, ?, ?, ?)");
    target.bind(1, static_cast<long long>(added.id)).bind(2, std::string("SCC1"))
        .bind(3, 1LL).bind(4, 31.49).run();

    beam::gui::savePatients(database, registry);
    EXPECT_EQ(countOf(database, "imaging"), 1);
    EXPECT_EQ(countOf(database, "target"), 1);
}

// A patient genuinely dropped from the registry still goes, with their records.
TEST(PatientRegistryDb, ADepartedPatientStillTakesTheirRecords) {
    Database database = freshStore();
    PatientRegistry registry;
    const Patient first = registry.add("1", "IU");
    const Patient second = registry.add("2", "IU");
    beam::gui::savePatients(database, registry);
    auto visit = database.prepare("INSERT INTO visit (patient_id, number) VALUES (?, ?)");
    visit.bind(1, static_cast<long long>(first.id)).bind(2, 1LL).run();
    visit.bind(1, static_cast<long long>(second.id)).bind(2, 1LL).run();

    ASSERT_TRUE(registry.remove(second.id));
    beam::gui::savePatients(database, registry);
    EXPECT_EQ(countOf(database, "patient"), 1);
    EXPECT_EQ(countOf(database, "visit"), 1);
}

// The linkage a sonication carries is its visit; the view brings the patient
// down to the row so reading the record needs no join.
TEST(InfraDb, SonicationLogCarriesThePatient) {
    Database database = freshStore();
    const long long patient = addPatient(database, "7");
    auto visit = database.prepare(
        "INSERT INTO visit (patient_id, number, protocol, lock_lh, lock_lv) "
        "VALUES (?, ?, ?, ?, ?)");
    visit.bind(1, patient).bind(2, 3LL).bind(3, std::string("Default"))
        .bind(4, 2LL).bind(5, 4LL).run();
    const long long visitId = database.lastInsertId();
    auto sonication = database.prepare(
        "INSERT INTO sonication (visit_id, sequence, target) VALUES (?, ?, ?)");
    sonication.bind(1, visitId).bind(2, 1LL).bind(3, std::string("SCC1")).run();

    auto read = database.prepare(
        "SELECT patient_id, participant, site, visit, protocol, lock_y, lock_z, "
        "target FROM sonication_log");
    ASSERT_TRUE(read.step());
    const beam::db::Row row = read.row();
    EXPECT_EQ(row.integer(0), patient);
    EXPECT_EQ(row.text(1), "7");
    EXPECT_EQ(row.text(2), "CCF");
    EXPECT_EQ(row.integer(3), 3);
    EXPECT_EQ(row.text(4), "Default");
    EXPECT_EQ(row.integer(5), 2);
    EXPECT_EQ(row.integer(6), 4);
    EXPECT_EQ(row.text(7), "SCC1");
}

TEST(InfraDb, VisitLogCountsItsSonications) {
    Database database = freshStore();
    const long long patient = addPatient(database, "8");
    auto visit = database.prepare("INSERT INTO visit (patient_id, number) VALUES (?, ?)");
    visit.bind(1, patient).bind(2, 1LL).run();
    const long long visitId = database.lastInsertId();
    auto sonication = database.prepare(
        "INSERT INTO sonication (visit_id, sequence) VALUES (?, ?)");
    for (long long sequence = 1; sequence <= 3; ++sequence)
        sonication.bind(1, visitId).bind(2, sequence).run();

    auto read = database.prepare("SELECT participant, sonications FROM visit_log");
    ASSERT_TRUE(read.step());
    EXPECT_EQ(read.row().text(0), "8");
    EXPECT_EQ(read.row().integer(1), 3);
}

// A store stamped at the current version but missing a view is what a part-way
// upgrade leaves behind; opening it has to put the view back.
TEST(InfraDb, ViewsComeBackOnAStoreAlreadyStampedWithoutThem) {
    Database database = freshStore();
    database.execute("DROP VIEW sonication_log");
    EXPECT_EQ(countOf(database, "sqlite_master WHERE type = 'view'"), 1);

    beam::db::applySchema(database);
    EXPECT_EQ(database.userVersion(), beam::db::schemaVersion());
    EXPECT_EQ(countOf(database, "sqlite_master WHERE type = 'view'"), 2);
    EXPECT_EQ(countOf(database, "sonication_log"), 0);
}


// The scan a visit was planned on is a per-visit fact, normalised out of the
// rows that each repeated it and joined back on read.
TEST(TreatmentLogDb, ScanLinkSurvivesNormalisation) {
    Database database = freshStore();
    const int patient = patientRow(database, "1");
    TreatmentLog written;
    written.adopt({deliveredRow(1, 1, "SCC1"), deliveredRow(1, 2, "SCC2")});
    beam::gui::saveLog(database, patient, written);

    // One scan, not one per sonication, and the visit points at it.
    EXPECT_EQ(countOf(database, "imaging"), 1);
    EXPECT_EQ(countOf(database, "visit WHERE imaging_id IS NOT NULL"), 1);

    TreatmentLog read;
    beam::gui::loadLog(database, patient, &read);
    ASSERT_EQ(read.rows().size(), 2u);
    for (const TreatmentLogRow& row : read.rows()) {
        EXPECT_EQ(row.mriFile, "C:/scans/p1/t1.nii.gz");
        EXPECT_EQ(row.mriFingerprint, "256x256x176@1.0");
        EXPECT_EQ(row.fiducialsMm, "12.0;-8.5;3.25");
    }
}

TEST(TreatmentLogDb, SavingTwiceDoesNotDuplicateTheScan) {
    Database database = freshStore();
    const int patient = patientRow(database, "1");
    TreatmentLog written;
    written.adopt({deliveredRow(1, 1, "SCC1")});
    beam::gui::saveLog(database, patient, written);
    beam::gui::saveLog(database, patient, written);
    EXPECT_EQ(countOf(database, "imaging"), 1);
}

// A second scan must not overwrite the one earlier visits were run against.
TEST(TreatmentLogDb, ARescanIsItsOwnRow) {
    Database database = freshStore();
    const int patient = patientRow(database, "1");
    TreatmentLogRow first = deliveredRow(1, 1, "SCC1");
    TreatmentLogRow second = deliveredRow(2, 1, "SCC1");
    second.mriFile = "C:/scans/p1/t2.nii.gz";
    second.mriFingerprint = "256x256x176@0.9";
    TreatmentLog written;
    written.adopt({first, second});
    beam::gui::saveLog(database, patient, written);

    EXPECT_EQ(countOf(database, "imaging"), 2);
    auto read = database.prepare(
        "SELECT v.number, i.mri_path FROM visit v "
        "JOIN imaging i ON i.id = v.imaging_id ORDER BY v.number");
    ASSERT_TRUE(read.step());
    EXPECT_EQ(read.row().text(1), "C:/scans/p1/t1.nii.gz");
    ASSERT_TRUE(read.step());
    EXPECT_EQ(read.row().text(1), "C:/scans/p1/t2.nii.gz");
}

// A row with no scan on it must not manufacture an empty imaging row.
TEST(TreatmentLogDb, NoScanMeansNoImagingRow) {
    Database database = freshStore();
    const int patient = patientRow(database, "1");
    TreatmentLogRow row = deliveredRow(1, 1, "SCC1");
    row.mriFile.clear();
    row.mriFingerprint.clear();
    row.fiducialsMm.clear();
    TreatmentLog written;
    written.adopt({row});
    beam::gui::saveLog(database, patient, written);
    EXPECT_EQ(countOf(database, "imaging"), 0);
    EXPECT_EQ(countOf(database, "visit WHERE imaging_id IS NULL"), 1);
}

}  // namespace
