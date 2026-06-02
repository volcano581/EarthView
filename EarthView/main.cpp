#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include "MainWindow.h"
#include "OpenGLRuntime.h"

int main(int argc, char* argv[])
{
    if (OpenGLRuntime::softwareOpenGLRequested()) {
        QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
    }

    QSurfaceFormat::setDefaultFormat(OpenGLRuntime::defaultSurfaceFormat());

    QApplication app(argc, argv);

    qDebug() << "Requested OpenGL format:" << QSurfaceFormat::defaultFormat();
    if (OpenGLRuntime::softwareOpenGLRequested()) {
        qDebug() << "Software OpenGL rendering requested.";
    }
    if (OpenGLRuntime::forceOpenGL33Requested()) {
        qDebug() << "OpenGL 3.3 compatibility context requested.";
    }

    MainWindow window;
    window.show();

    return app.exec();
}
