#pragma once

#include "infra_db/database.hpp"

// The one store file's shape, and the migration that brings an older one up to
// it. Creating the tables is idempotent; the version pragma decides the rest.
namespace beam::db {

int schemaVersion();

// Throws DatabaseError if the file was written by a newer BeamAI.
void applySchema(Database& database);

}  // namespace beam::db
