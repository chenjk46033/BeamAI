#include <QApplication>

#include "workflow_window.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("BeamAI"));
    beam::app::WorkflowWindow window;
    window.show();
    return app.exec();
}
