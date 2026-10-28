#pragma once

#include <string>
#include <vector>

// Who the device has treated, and which of them is being treated now. BeamV0
// has no equivalent: a new subject there meant a new copy of the data
// directory.

namespace beam::gui {

struct Patient {
    // This registry's own numbering, and meaningless outside it. Stable for
    // the life of the record: it is what the patient's directory is named
    // after, so renumbering would orphan their visits.
    int id = 0;
    // The operator's identifier -- the Participant ID from the Case page.
    // This is the one that appears on a CRF and the one a person is
    // recognised by between visits.
    std::string externalId;
    std::string site;
    // Optional: somebody can be treated before their details are to hand.
    std::string lastName;
    std::string firstName;
    // ISO yyyy-mm-dd, or empty.
    std::string dateOfBirth;
    // ISO-8601 local time, stamped when the patient was first added.
    std::string createdAt;
};

// "Last, First" when there is a name, the Participant ID when there is not --
// never empty, because this is what a chooser row is read by.
std::string patientDisplayName(const Patient& patient);

class PatientRegistry {
public:
    // A missing file is an empty registry. A malformed row throws and leaves
    // the registry as it was: a half-read list is worse than none.
    void load(const std::string& path);
    void save(const std::string& path) const;

    const std::vector<Patient>& patients() const { return patients_; }
    bool empty() const { return patients_.empty(); }

    // An external id already on file returns THAT patient unchanged -- this
    // runs on every case acceptance, from a form that may still hold the last
    // person typed into it. Editing who somebody is goes through update().
    Patient add(const Patient& details);
    Patient add(const std::string& externalId, const std::string& site);

    // By this registry's id, or by the operator's identifier. Both leave `out`
    // untouched and return false when there is no such patient.
    bool find(int id, Patient* out) const;
    bool findByExternalId(const std::string& externalId, Patient* out) const;

    // Unlike add(), an empty field here DOES clear the stored one: this is
    // the editor. False if the identifier is blank or belongs to somebody else.
    bool update(int id, const Patient& details);
    // The identifier and site alone, leaving demographics as they are.
    bool rename(int id, const std::string& externalId, const std::string& site);

    // Whether they MAY be dropped is the caller's call: it checks the log,
    // since a sonicated patient is a record. The id is never released, so a
    // departed patient's directory is not handed to somebody else.
    bool remove(int id);

    // Replaces the contents wholesale, as load() does. The database loader is
    // the other caller.
    void adopt(std::vector<Patient> patients, int nextId);
    int nextId() const { return nextId_; }

private:
    std::vector<Patient> patients_;
    // One past the highest id ever issued, carried in the file. Not derived
    // from the rows: removing a patient would then release their number, and
    // the next person added would be handed the directory that was theirs.
    int nextId_ = 1;
};

}  // namespace beam::gui
