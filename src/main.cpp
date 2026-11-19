#include <QApplication>

#include "MainWindow.h"
#include "Theme.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("MCDebugger"));
    QApplication::setOrganizationName(QStringLiteral("MCDebugger"));
    QApplication::setApplicationVersion(QStringLiteral("1.0.0"));

    Theme::apply(app);

    MainWindow w;
    w.show();
    return app.exec();
}
