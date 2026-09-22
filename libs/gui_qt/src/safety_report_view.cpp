#include "gui_qt/safety_report_view.hpp"

#include <QAbstractItemView>
#include <QColor>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QString>
#include <QVBoxLayout>

namespace beam::gui_qt {

SafetyReportView::SafetyReportView(QWidget* parent)
    : QWidget(parent), statusLabel_(new QLabel(this)), messageList_(new QListWidget(this)) {
    auto* layout = new QVBoxLayout(this);

    QFont font = statusLabel_->font();
    font.setPointSize(font.pointSize() + 4);
    font.setBold(true);
    statusLabel_->setFont(font);

    // Read-only status display, not an interactive list picker -- the
    // real app.SystemStatusTextArea is just a report surface, and
    // QListWidget's row-selection behavior (clickable, a focus rectangle,
    // and Qt's palette swapping selected text to its highlighted-text
    // color -- white by default, stomping the red failure color set
    // below) was showing through. Caught by the user ("I was able to
    // select and the font color changed to white").
    messageList_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    messageList_->setSelectionMode(QAbstractItemView::NoSelection);
    messageList_->setFocusPolicy(Qt::NoFocus);
    messageList_->setFrameShape(QFrame::NoFrame);  // don't look like a text box either
    // No scrollbars either -- besides the same "not a text box" ask, a
    // resize could leave this scrollbar detached from the box's own
    // painted border, floating in whatever's laid out next to it (caught
    // by the user: "the scroll bar lands inside image display"). There
    // are only ever 1-2 short lines; nothing to scroll to.
    messageList_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    messageList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // QListWidget's own sizeHint() defaults to a fixed ~256px-tall
    // estimate regardless of actual item count -- with only ever 1-2 short
    // lines here, that inflated preferred height was what this row
    // requested from its layout (measured: the whole Site ID/Participant
    // ID row was allocated 257px against ~65px of real content), pushing
    // every row below it (including the MRI images) down and stealing
    // real space from them. An explicit cap replaces that estimate.
    messageList_->setMaximumHeight(80);
    // QAbstractItemView paints its own QPalette::Base background (white/
    // light-gray by default on this Qt/Windows build) regardless of the
    // rest of the app's dark theme, so this list stood out as a distinct
    // panel instead of reading as part of the status area around it
    // (user: "should be the background of the app screen, no need to
    // stand out"). Transparent so the parent widget's own background
    // shows through instead.
    messageList_->setStyleSheet(QStringLiteral("background: transparent; border: none;"));

    layout->addWidget(statusLabel_);
    layout->addWidget(messageList_);

    setReport({});
}

void SafetyReportView::setReport(const beam::gui::SonicationSafetyReport& report) {
    // Pass/fail color-coding is this port's own addition, not source
    // behavior -- the real app.SystemStatusTextArea has a single fixed
    // FontColor (white-on-black) regardless of state (see
    // SystemStatusTextArea.FontColor in the .mlapp). Red for the fail
    // state per the user's explicit ask ("In Red Color"); which state
    // gets which color is still decided by report.pass, the real
    // decision -- not a hardcoded color applied regardless of outcome.
    if (report.pass) {
        statusLabel_->setText(QStringLiteral("Sonication Online"));
        statusLabel_->setStyleSheet(QStringLiteral("color: #1a8a1a;"));  // green
    } else {
        statusLabel_->setText(QStringLiteral("Not ready — safety checks failed"));
        statusLabel_->setStyleSheet(QStringLiteral("color: #ff0000;"));  // red
    }

    // White, not red -- reversed from an earlier pass per the user's
    // follow-up ask ("Change the message box message to White but bigger
    // font size. So that they stand out"). statusLabel_ above keeps its
    // red/green pass-state color; only these per-message rows changed.
    QFont messageFont = messageList_->font();
    messageFont.setPointSize(messageFont.pointSize() + 3);
    messageList_->clear();
    for (const std::string& msg : report.messages) {
        auto* item = new QListWidgetItem(QString::fromStdString(msg));
        item->setFont(messageFont);
        item->setForeground(QColor(0xff, 0xff, 0xff));
        messageList_->addItem(item);
    }
    messageList_->setVisible(!report.messages.empty());
}

}  // namespace beam::gui_qt
