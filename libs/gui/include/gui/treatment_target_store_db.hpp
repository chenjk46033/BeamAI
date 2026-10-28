#pragma once

#include "gui/treatment_target_store.hpp"
#include "infra_db/schema.hpp"

// A patient's own targets: the shipped seed says what a target is, this says
// where it went for this person.
namespace beam::gui {

void loadTargets(beam::db::Database& database, int patientId, TreatmentTargetStore* store);
void saveTargets(beam::db::Database& database, int patientId,
                 const TreatmentTargetStore& store);

}  // namespace beam::gui
