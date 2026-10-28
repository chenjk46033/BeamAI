#pragma once

#include "gui/patient_registry.hpp"
#include "infra_db/schema.hpp"

// The registry's database endpoints. load()/save() on the class itself stay as
// they are and become the CSV export path.
namespace beam::gui {

void loadPatients(beam::db::Database& database, PatientRegistry* registry);
void savePatients(beam::db::Database& database, const PatientRegistry& registry);

}  // namespace beam::gui
