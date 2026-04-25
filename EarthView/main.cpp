#include <QApplication>
#include <QSurfaceFormat>
#include <QCoreApplication>
#include <QDebug>
#include "MainWindow.h"



int main(int argc, char* argv[])
{
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setVersion(4, 5);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSwapInterval(0);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);

    qDebug() << "Requested OpenGL format:" << QSurfaceFormat::defaultFormat();

    MainWindow window;
    window.show();

    return app.exec();
}
