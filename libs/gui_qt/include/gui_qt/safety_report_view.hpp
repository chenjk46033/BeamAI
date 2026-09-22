#pragma once

#include <QWidget>

#include "gui/safety_presenter.hpp"

// The one Qt-dependent GUI library, kept separate from libs/gui on purpose
// (same "domain/presenter logic has no third-party deps" split infra_dicom
// established for DCMTK). libs/gui's presenters are pure and unit-tested
// without Qt; this is the thin layer that renders them.

class QLabel;
class QListWidget;

namespace beam::gui_qt {

// Renders a beam::gui::SonicationSafetyReport as
// BeamV0/GUIMatlab/BEAM/GUI/Safety/checkSonicationSafety.m's
// app.SystemStatusTextArea would: a status line ("Sonication Online" or
// "Not ready") plus the list of blocking messages. Green when the report
// passes, red otherwise -- matching the source's FontColor usage in the
// sibling checkCouplingSafety.m.
class SafetyReportView : public QWidget {
    Q_OBJECT

public:
    explicit SafetyReportView(QWidget* parent = nullptr);

    void setReport(const beam::gui::SonicationSafetyReport& report);

private:
    QLabel* statusLabel_;
    QListWidget* messageList_;
};

}  // namespace beam::gui_qt
