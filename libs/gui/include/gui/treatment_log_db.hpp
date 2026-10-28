#pragma once

#include "gui/treatment_log.hpp"
#include "infra_db/schema.hpp"

// The log's database endpoints. Visits are shared with the visit state, so
// these create a visit row when one is missing but never delete one.
namespace beam::gui {

void loadLog(beam::db::Database& database, int patientId, TreatmentLog* log);
void saveLog(beam::db::Database& database, int patientId, const TreatmentLog& log);

}  // namespace beam::gui
