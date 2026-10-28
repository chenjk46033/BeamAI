#include "infra_db/schema.hpp"

namespace beam::db {
namespace {

// Bump when the shape below changes, and add the migration beside it.
constexpr int kSchemaVersion = 2;

// Column names match the CSV headers the exports still carry, so a query
// written against the database reads the same as the file it produces.
constexpr const char* kVersion1 = R"SQL(
-- Small facts that belong to the store rather than to a row. next_patient_id
-- lives here because it must not be derived from the rows: removing the
-- highest-numbered patient would release their number, and the next person
-- added would inherit a departed patient's record.
CREATE TABLE IF NOT EXISTS meta (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS patient (
    id            INTEGER PRIMARY KEY,
    external_id   TEXT NOT NULL UNIQUE,
    site          TEXT NOT NULL DEFAULT '',
    last_name     TEXT NOT NULL DEFAULT '',
    first_name    TEXT NOT NULL DEFAULT '',
    date_of_birth TEXT NOT NULL DEFAULT '',
    created_at    TEXT NOT NULL DEFAULT '',
    last_treated  TEXT NOT NULL DEFAULT ''
);

-- The MRI is linked, never copied here: path plus the fingerprint that proves
-- a file found later is the same image. One row per scan, so a rescan does not
-- overwrite what earlier visits were run against.
CREATE TABLE IF NOT EXISTS imaging (
    id          INTEGER PRIMARY KEY,
    patient_id  INTEGER NOT NULL REFERENCES patient(id) ON DELETE CASCADE,
    mri_path    TEXT NOT NULL,
    fingerprint TEXT NOT NULL,
    fiducials   TEXT NOT NULL DEFAULT '',
    recorded_at TEXT NOT NULL DEFAULT '',
    UNIQUE (patient_id, fingerprint)
);

CREATE TABLE IF NOT EXISTS visit (
    id          INTEGER PRIMARY KEY,
    patient_id  INTEGER NOT NULL REFERENCES patient(id) ON DELETE CASCADE,
    number      INTEGER NOT NULL,
    protocol    TEXT NOT NULL DEFAULT '',
    imaging_id  INTEGER REFERENCES imaging(id),
    lock_lh     INTEGER NOT NULL DEFAULT 1,
    lock_lv     INTEGER NOT NULL DEFAULT 1,
    lock_rh     INTEGER NOT NULL DEFAULT 1,
    lock_rv     INTEGER NOT NULL DEFAULT 1,
    current_sonication INTEGER NOT NULL DEFAULT 0,
    edited      INTEGER NOT NULL DEFAULT 0,
    auto_rank   INTEGER NOT NULL DEFAULT 1,
    UNIQUE (patient_id, number, protocol)
);

-- One row per delivery, appended and never rewritten: this is the record of
-- what was actually put into a patient.
CREATE TABLE IF NOT EXISTS sonication (
    id          INTEGER PRIMARY KEY,
    visit_id    INTEGER NOT NULL REFERENCES visit(id) ON DELETE CASCADE,
    sequence    INTEGER NOT NULL,
    target      TEXT NOT NULL DEFAULT '',
    x_mm        REAL NOT NULL DEFAULT 0,
    y_mm        REAL NOT NULL DEFAULT 0,
    z_mm        REAL NOT NULL DEFAULT 0,
    amplitude   REAL NOT NULL DEFAULT 0,
    start_time_s REAL NOT NULL DEFAULT 0,
    end_time_s  REAL NOT NULL DEFAULT 0,
    burst_duration_s REAL NOT NULL DEFAULT 0,
    burst_interval_s REAL NOT NULL DEFAULT 0,
    pulse_duration_s REAL NOT NULL DEFAULT 0,
    pulse_interval_s REAL NOT NULL DEFAULT 0,
    sham        INTEGER NOT NULL DEFAULT 0,
    status_code INTEGER NOT NULL DEFAULT 0,
    started_at  TEXT NOT NULL DEFAULT '',
    ended_at    TEXT NOT NULL DEFAULT '',
    from_plan   INTEGER NOT NULL DEFAULT 1,
    device_model TEXT NOT NULL DEFAULT '',
    device_serial TEXT NOT NULL DEFAULT '',
    cal_duty    TEXT NOT NULL DEFAULT '',
    cal_pressure TEXT NOT NULL DEFAULT '',
    notes       TEXT NOT NULL DEFAULT ''
);

CREATE TABLE IF NOT EXISTS target (
    id          INTEGER PRIMARY KEY,
    patient_id  INTEGER NOT NULL REFERENCES patient(id) ON DELETE CASCADE,
    name        TEXT NOT NULL,
    ordinal     INTEGER NOT NULL DEFAULT 0,
    show        INTEGER NOT NULL DEFAULT 1,
    x_mm        REAL NOT NULL DEFAULT 0,
    y_mm        REAL NOT NULL DEFAULT 0,
    z_mm        REAL NOT NULL DEFAULT 0,
    amplitude   REAL NOT NULL DEFAULT 0,
    start_time_s REAL NOT NULL DEFAULT 0,
    end_time_s  REAL NOT NULL DEFAULT 0,
    burst_duration_s REAL NOT NULL DEFAULT 0,
    burst_interval_s REAL NOT NULL DEFAULT 0,
    pulse_duration_s REAL NOT NULL DEFAULT 0,
    pulse_interval_s REAL NOT NULL DEFAULT 0,
    UNIQUE (patient_id, name, ordinal)
);

CREATE INDEX IF NOT EXISTS visit_by_patient ON visit(patient_id, number);
CREATE INDEX IF NOT EXISTS sonication_by_visit ON sonication(visit_id, sequence);
)SQL";

// A sonication belongs to a patient through its visit, which is right to store
// and tedious to query. These carry the patient down so reading needs no join.
constexpr const char* kViews = R"SQL(
DROP VIEW IF EXISTS sonication_log;
CREATE VIEW sonication_log AS
SELECT p.id            AS patient_id,
       p.external_id   AS participant,
       p.site          AS site,
       v.number        AS visit,
       v.protocol      AS protocol,
       v.lock_lh       AS lock_y,
       v.lock_lv       AS lock_z,
       s.*
FROM sonication s
JOIN visit v   ON v.id = s.visit_id
JOIN patient p ON p.id = v.patient_id;

DROP VIEW IF EXISTS visit_log;
CREATE VIEW visit_log AS
SELECT p.id          AS patient_id,
       p.external_id AS participant,
       v.id          AS visit_id,
       v.number      AS visit,
       v.protocol    AS protocol,
       v.lock_lh     AS lock_y,
       v.lock_lv     AS lock_z,
       v.current_sonication,
       i.mri_path    AS mri,
       i.fingerprint AS mri_fingerprint,
       (SELECT COUNT(*) FROM sonication s WHERE s.visit_id = v.id) AS sonications
FROM visit v
JOIN patient p      ON p.id = v.patient_id
LEFT JOIN imaging i ON i.id = v.imaging_id;
)SQL";

}  // namespace

int schemaVersion() { return kSchemaVersion; }

void applySchema(Database& database) {
    const int found = database.userVersion();
    if (found > kSchemaVersion)
        throw DatabaseError("this store was written by a newer version of BeamAI");
    // Views are derived, so they are redefined on every open: rebuilding costs
    // nothing and a store left stamped by a half-applied upgrade repairs itself.
    Transaction work(database);
    if (found < 1) database.execute(kVersion1);
    database.execute(kViews);
    if (found != kSchemaVersion) database.setUserVersion(kSchemaVersion);
    work.commit();
}

}  // namespace beam::db
