#include "gui/treatment_target_store_db.hpp"

namespace beam::gui {

void loadTargets(beam::db::Database& database, int patientId, TreatmentTargetStore* store) {
    std::vector<TreatmentTarget> loaded;
    auto read = database.prepare(
        "SELECT name, ordinal, show, amplitude, start_time_s, end_time_s, "
        "burst_duration_s, burst_interval_s, pulse_duration_s, pulse_interval_s, "
        "x_mm, y_mm, z_mm FROM target WHERE patient_id = ? ORDER BY name, ordinal");
    read.bind(1, static_cast<long long>(patientId));
    while (read.step()) {
        const beam::db::Row r = read.row();
        const std::string name = r.text(0);
        if (loaded.empty() || loaded.back().name != name) loaded.push_back({name, {}});
        TreatmentTargetPoint point;
        point.order = static_cast<int>(r.integer(1));
        point.show = r.integer(2) != 0;
        point.amplitude = r.real(3);
        point.startTimeSeconds = r.real(4);
        point.endTimeSeconds = r.real(5);
        point.burstDurationSeconds = r.real(6);
        point.burstIntervalSeconds = r.real(7);
        point.pulseDurationSeconds = r.real(8);
        point.pulseIntervalSeconds = r.real(9);
        point.xMm = r.real(10);
        point.yMm = r.real(11);
        point.zMm = r.real(12);
        loaded.back().points.push_back(point);
    }
    store->adoptTargets(std::move(loaded));
}

void saveTargets(beam::db::Database& database, int patientId,
                 const TreatmentTargetStore& store) {
    beam::db::Transaction work(database);
    auto clear = database.prepare("DELETE FROM target WHERE patient_id = ?");
    clear.bind(1, static_cast<long long>(patientId)).run();

    auto insert = database.prepare(
        "INSERT INTO target (patient_id, name, ordinal, show, amplitude, start_time_s, "
        "end_time_s, burst_duration_s, burst_interval_s, pulse_duration_s, "
        "pulse_interval_s, x_mm, y_mm, z_mm) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    for (const TreatmentTarget& target : store.targets()) {
        for (const TreatmentTargetPoint& point : target.points) {
            insert.bind(1, static_cast<long long>(patientId))
                .bind(2, target.name)
                .bind(3, static_cast<long long>(point.order))
                .bind(4, static_cast<long long>(point.show ? 1 : 0))
                .bind(5, point.amplitude)
                .bind(6, point.startTimeSeconds)
                .bind(7, point.endTimeSeconds)
                .bind(8, point.burstDurationSeconds)
                .bind(9, point.burstIntervalSeconds)
                .bind(10, point.pulseDurationSeconds)
                .bind(11, point.pulseIntervalSeconds)
                .bind(12, point.xMm)
                .bind(13, point.yMm)
                .bind(14, point.zMm)
                .run();
        }
    }
    work.commit();
}

}  // namespace beam::gui
