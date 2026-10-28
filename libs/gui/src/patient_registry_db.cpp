#include "gui/patient_registry_db.hpp"

namespace beam::gui {
namespace {

constexpr const char* kNextIdKey = "next_patient_id";

int readNextId(beam::db::Database& database) {
    auto read = database.prepare("SELECT value FROM meta WHERE key = ?");
    read.bind(1, std::string(kNextIdKey));
    if (!read.step()) return 1;
    try {
        return std::stoi(read.row().text(0));
    } catch (const std::exception&) {
        return 1;
    }
}

}  // namespace

void loadPatients(beam::db::Database& database, PatientRegistry* registry) {
    std::vector<Patient> rows;
    auto read = database.prepare(
        "SELECT id, external_id, site, last_name, first_name, date_of_birth, created_at "
        "FROM patient ORDER BY id");
    while (read.step()) {
        const beam::db::Row row = read.row();
        Patient patient;
        patient.id = static_cast<int>(row.integer(0));
        patient.externalId = row.text(1);
        patient.site = row.text(2);
        patient.lastName = row.text(3);
        patient.firstName = row.text(4);
        patient.dateOfBirth = row.text(5);
        patient.createdAt = row.text(6);
        rows.push_back(std::move(patient));
    }
    registry->adopt(std::move(rows), readNextId(database));
}

void savePatients(beam::db::Database& database, const PatientRegistry& registry) {
    beam::db::Transaction work(database);

    // Never DELETE then re-insert: every child table cascades on patient(id),
    // so clearing the table destroys the records of patients who never changed.
    auto upsert = database.prepare(
        "INSERT INTO patient (id, external_id, site, last_name, first_name, "
        "date_of_birth, created_at) VALUES (?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET external_id = excluded.external_id, "
        "site = excluded.site, last_name = excluded.last_name, "
        "first_name = excluded.first_name, date_of_birth = excluded.date_of_birth, "
        "created_at = excluded.created_at");
    std::string keep;
    for (const Patient& patient : registry.patients()) {
        upsert.bind(1, static_cast<long long>(patient.id))
            .bind(2, patient.externalId)
            .bind(3, patient.site)
            .bind(4, patient.lastName)
            .bind(5, patient.firstName)
            .bind(6, patient.dateOfBirth)
            .bind(7, patient.createdAt)
            .run();
        if (!keep.empty()) keep += ',';
        keep += std::to_string(patient.id);
    }
    // Ids are integers this code produced, so there is nothing to escape.
    database.execute(keep.empty()
                         ? std::string("DELETE FROM patient")
                         : "DELETE FROM patient WHERE id NOT IN (" + keep + ")");
    auto meta = database.prepare(
        "INSERT INTO meta (key, value) VALUES (?, ?) "
        "ON CONFLICT(key) DO UPDATE SET value = excluded.value");
    meta.bind(1, std::string(kNextIdKey))
        .bind(2, std::to_string(registry.nextId()))
        .run();
    work.commit();
}

}  // namespace beam::gui
