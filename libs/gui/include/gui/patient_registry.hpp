#pragma once

#include <string>
#include <vector>

// Who the device has treated, and which of them is being treated now.
//
// BeamV0 has no such thing: it holds one anonymous subject's data in
// sys.treatmentProtocolTables and a new patient means a new copy of the
// application's data directory. BeamAI's CSVs inherited that -- Participant ID
// and Site ID are typed on the Case page, shown in the header, and never
// written anywhere, and setLogInfoToCSV's SubjectID/SiteID columns are
// exported empty.
//
// The shape here is the one a clinic database would have, kept deliberately
// small so the same code serves a device dedicated to one person: that device
// has one patient and never shows a chooser, rather than being a separate
// build with a separate data model.
//
// What belongs to a person -- their visits, their log, where their targets
// were placed -- is stored under that person. What belongs to the install --
// the shipped Target List, the protocol library -- is not, and stays where it
// is.

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
    // Who they are, as a person rather than as a participant number. All
    // three are optional: somebody can be enrolled and treated before their
    // details are to hand, and an empty name must not stand between them and
    // the device. Separate from externalId on purpose -- a Participant ID is
    // what a CRF carries, and a name is what the operator recognises.
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
    // CSV with a header row; '#' starts a comment. A missing file is an empty
    // registry rather than an error -- that is a device nobody has used yet.
    // Throws std::runtime_error on a malformed row, leaving the registry as it
    // was: a half-read list of patients is worse than none, because the
    // chooser would then be quietly missing people.
    void load(const std::string& path);
    void save(const std::string& path) const;

    const std::vector<Patient>& patients() const { return patients_; }
    bool empty() const { return patients_.empty(); }

    // Adds a patient and returns them with their id filled in. `details`
    // supplies the identifier and whatever demographics are known; its id and
    // createdAt are ignored.
    //
    // An external id already on file returns THAT patient, unchanged, rather
    // than a second record -- two rows for one person splits their history in
    // half, and nothing downstream would notice.
    //
    // Unchanged, deliberately: this is called every time a case is accepted,
    // from a form that may still hold the last person typed into it. Editing
    // who somebody is goes through update(), which the operator asks for.
    Patient add(const Patient& details);
    Patient add(const std::string& externalId, const std::string& site);

    // By this registry's id, or by the operator's identifier. Both leave `out`
    // untouched and return false when there is no such patient.
    bool find(int id, Patient* out) const;
    bool findByExternalId(const std::string& externalId, Patient* out) const;

    // Replaces everything the operator can edit: identifier, site, and the
    // three demographic fields. Unlike add(), an empty field here DOES clear
    // the stored one -- this is the editor, and clearing a mistyped name has
    // to be possible. False if there is no such patient, if the identifier is
    // blank, or if it belongs to somebody else. The id and createdAt in
    // `details` are ignored.
    bool update(int id, const Patient& details);
    // The identifier and site alone, leaving demographics as they are.
    bool rename(int id, const std::string& externalId, const std::string& site);

    // Drops a patient from the list. False if there is no such patient.
    //
    // Whether they MAY be dropped is not decided here: this type knows who
    // exists, not what was done to them. The caller checks the treatment log
    // first -- a patient who has been sonicated is a record, and no later
    // decision erases that. In practice this removes a mistyped identifier
    // before anybody has been treated under it.
    //
    // The id is not released. Numbering continues past it, so the directory
    // that was theirs is never handed to somebody else.
    bool remove(int id);

private:
    std::vector<Patient> patients_;
    // One past the highest id ever issued, carried in the file. Not derived
    // from the rows: removing a patient would then release their number, and
    // the next person added would be handed the directory that was theirs.
    int nextId_ = 1;
};

}  // namespace beam::gui
