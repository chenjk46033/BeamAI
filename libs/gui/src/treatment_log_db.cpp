#include "gui/treatment_log_db.hpp"

#include <map>
#include <utility>

namespace beam::gui {
namespace {

// A visit is identified by its protocol and number, as the log rows are.
using VisitKey = std::pair<std::string, int>;

long long visitRowFor(beam::db::Database& database, int patientId, const VisitKey& key) {
    auto find = database.prepare(
        "SELECT id FROM visit WHERE patient_id = ? AND protocol = ? AND number = ?");
    find.bind(1, static_cast<long long>(patientId))
        .bind(2, key.first)
        .bind(3, static_cast<long long>(key.second));
    if (find.step()) return find.row().integer(0);

    auto insert = database.prepare(
        "INSERT INTO visit (patient_id, protocol, number) VALUES (?, ?, ?)");
    insert.bind(1, static_cast<long long>(patientId))
        .bind(2, key.first)
        .bind(3, static_cast<long long>(key.second))
        .run();
    return database.lastInsertId();
}

// One row per scan, keyed by the fingerprint that identifies it, so a rescan
// adds a row instead of overwriting what earlier visits were planned on.
long long imagingRowFor(beam::db::Database& database, int patientId,
                        const TreatmentLogRow& row) {
    if (row.mriFile.empty() && row.mriFingerprint.empty()) return 0;
    auto upsert = database.prepare(
        "INSERT INTO imaging (patient_id, mri_path, fingerprint, fiducials, recorded_at) "
        "VALUES (?, ?, ?, ?, ?) "
        "ON CONFLICT(patient_id, fingerprint) DO UPDATE SET "
        "mri_path = excluded.mri_path, fiducials = excluded.fiducials");
    upsert.bind(1, static_cast<long long>(patientId))
        .bind(2, row.mriFile)
        .bind(3, row.mriFingerprint)
        .bind(4, row.fiducialsMm)
        .bind(5, row.startedAt)
        .run();
    // On a conflict the insert updated an existing row, so its id is read back
    // rather than taken from lastInsertId.
    auto find = database.prepare(
        "SELECT id FROM imaging WHERE patient_id = ? AND fingerprint = ?");
    find.bind(1, static_cast<long long>(patientId)).bind(2, row.mriFingerprint);
    return find.step() ? find.row().integer(0) : 0;
}

}  // namespace

void loadLog(beam::db::Database& database, int patientId, TreatmentLog* log) {
    std::vector<TreatmentLogRow> rows;
    // The per-visit facts the CSV repeated on every row are joined back on, so
    // what the caller sees is the shape it has always seen.
    auto read = database.prepare(
        "SELECT v.number, s.sequence, v.protocol, s.target, s.x_mm, s.y_mm, s.z_mm, "
        "       s.amplitude, s.start_time_s, s.end_time_s, s.burst_duration_s, "
        "       s.burst_interval_s, s.pulse_duration_s, s.pulse_interval_s, s.sham, "
        "       s.status_code, s.started_at, s.ended_at, s.from_plan, s.device_model, "
        "       s.device_serial, s.cal_duty, s.cal_pressure, s.notes, "
        "       v.lock_lh, v.lock_lv, "
        "       COALESCE(i.mri_path, ''), COALESCE(i.fingerprint, ''), "
        "       COALESCE(i.fiducials, '') "
        "FROM sonication s "
        "JOIN visit v ON v.id = s.visit_id "
        "LEFT JOIN imaging i ON i.id = v.imaging_id "
        "WHERE v.patient_id = ? "
        "ORDER BY v.protocol, v.number, s.sequence");
    read.bind(1, static_cast<long long>(patientId));
    while (read.step()) {
        const beam::db::Row r = read.row();
        TreatmentLogRow row;
        row.visit = static_cast<int>(r.integer(0));
        row.sequence = static_cast<int>(r.integer(1));
        row.protocol = r.text(2);
        row.target = r.text(3);
        row.xMm = r.real(4);
        row.yMm = r.real(5);
        row.zMm = r.real(6);
        row.amplitude = r.real(7);
        row.startTimeSeconds = r.real(8);
        row.endTimeSeconds = r.real(9);
        row.burstDurationSeconds = r.real(10);
        row.burstIntervalSeconds = r.real(11);
        row.pulseDurationSeconds = r.real(12);
        row.pulseIntervalSeconds = r.real(13);
        row.sham = r.integer(14) != 0;
        row.statusCode = static_cast<int>(r.integer(15));
        row.startedAt = r.text(16);
        row.endedAt = r.text(17);
        row.fromPlan = r.integer(18) != 0;
        row.deviceModel = r.text(19);
        row.deviceSerial = r.text(20);
        row.calDuty = r.text(21);
        row.calPressure = r.text(22);
        row.notes = r.text(23);
        row.lockHorizontal = static_cast<int>(r.integer(24));
        row.lockVertical = static_cast<int>(r.integer(25));
        row.mriFile = r.text(26);
        row.mriFingerprint = r.text(27);
        row.fiducialsMm = r.text(28);
        rows.push_back(std::move(row));
    }
    log->adopt(std::move(rows));
}

void saveLog(beam::db::Database& database, int patientId, const TreatmentLog& log) {
    beam::db::Transaction work(database);

    // Visits are not owned here. They carry the lock position and the imaging
    // link, which this knows nothing about, so an existing one is reused and
    // only its sonications are replaced.
    std::map<VisitKey, long long> visits;
    for (const TreatmentLogRow& row : log.rows()) {
        const VisitKey key{row.protocol, row.visit};
        if (visits.find(key) == visits.end())
            visits.emplace(key, visitRowFor(database, patientId, key));
    }

    auto clear = database.prepare(
        "DELETE FROM sonication WHERE visit_id IN "
        "(SELECT id FROM visit WHERE patient_id = ?)");
    clear.bind(1, static_cast<long long>(patientId)).run();

    auto insert = database.prepare(
        "INSERT INTO sonication (visit_id, sequence, target, x_mm, y_mm, z_mm, amplitude, "
        "start_time_s, end_time_s, burst_duration_s, burst_interval_s, pulse_duration_s, "
        "pulse_interval_s, sham, status_code, started_at, ended_at, from_plan, "
        "device_model, device_serial, cal_duty, cal_pressure, notes) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    for (const TreatmentLogRow& row : log.rows()) {
        insert.bind(1, visits.at(VisitKey{row.protocol, row.visit}))
            .bind(2, static_cast<long long>(row.sequence))
            .bind(3, row.target)
            .bind(4, row.xMm)
            .bind(5, row.yMm)
            .bind(6, row.zMm)
            .bind(7, row.amplitude)
            .bind(8, row.startTimeSeconds)
            .bind(9, row.endTimeSeconds)
            .bind(10, row.burstDurationSeconds)
            .bind(11, row.burstIntervalSeconds)
            .bind(12, row.pulseDurationSeconds)
            .bind(13, row.pulseIntervalSeconds)
            .bind(14, static_cast<long long>(row.sham ? 1 : 0))
            .bind(15, static_cast<long long>(row.statusCode))
            .bind(16, row.startedAt)
            .bind(17, row.endedAt)
            .bind(18, static_cast<long long>(row.fromPlan ? 1 : 0))
            .bind(19, row.deviceModel)
            .bind(20, row.deviceSerial)
            .bind(21, row.calDuty)
            .bind(22, row.calPressure)
            .bind(23, row.notes)
            .run();
    }

    // The lock the deliveries went out at and the scan they were planned on
    // belong to the visit, not to every row that repeated them.
    auto lock = database.prepare("UPDATE visit SET lock_lh = ?, lock_lv = ? WHERE id = ?");
    auto link = database.prepare("UPDATE visit SET imaging_id = ? WHERE id = ?");
    for (const auto& [key, visitId] : visits) {
        for (const TreatmentLogRow& row : log.rows()) {
            if (row.protocol != key.first || row.visit != key.second) continue;
            if (row.lockHorizontal != 0 || row.lockVertical != 0)
                lock.bind(1, static_cast<long long>(row.lockHorizontal))
                    .bind(2, static_cast<long long>(row.lockVertical))
                    .bind(3, visitId)
                    .run();
            const long long imaging = imagingRowFor(database, patientId, row);
            if (imaging != 0) link.bind(1, imaging).bind(2, visitId).run();
            break;
        }
    }
    work.commit();
}

}  // namespace beam::gui
