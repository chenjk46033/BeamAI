#include <QApplication>

#include "workflow_window.hpp"

#include <QAbstractItemModel>
#include <QListWidget>
#include <QTableView>
#include <QTimer>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("BeamAI"));
    beam::app::WorkflowWindow window;
    if (qgetenv("BEAM_PROBE").isEmpty()) {
        window.showMaximized();
        return app.exec();
    }
    window.setGeometry(-4200, -2200, 1700, 1000);
    window.show();
    QTimer::singleShot(1400, &window, [&window] {
        window.findChild<QListWidget*>(QStringLiteral("stageList"))->setCurrentRow(6);
        auto* plan = window.findChild<QTableView*>(QStringLiteral("treatmentPlanView"));
        // Edit Duration on the current row (7) from 180 to 5, nothing else.
        plan->model()->setData(plan->model()->index(6, 3), 5.0, Qt::EditRole);
    });
    QTimer::singleShot(2400, &window, [] { qApp->quit(); });
    return app.exec();
}
