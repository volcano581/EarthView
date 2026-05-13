#include <QApplication>
#include <QByteArray>
#include <QSurfaceFormat>
#include <QCoreApplication>
#include <QDebug>
#include "MainWindow.h"

namespace {
QSurfaceFormat earthViewOpenGLFormat()
{
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setSwapInterval(0);
    return format;
}

bool environmentFlagEnabled(const char* name)
{
    const QByteArray value = qgetenv(name).trimmed().toLower();
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

bool softwareOpenGLRequested()
{
    return environmentFlagEnabled("EARTHVIEW_FORCE_SOFTWARE_OPENGL")
        || qgetenv("QT_OPENGL").trimmed().toLower() == "software";
}
}

int main(int argc, char* argv[])
{
    if (softwareOpenGLRequested()) {
        QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
    }

    QSurfaceFormat::setDefaultFormat(earthViewOpenGLFormat());

    QApplication app(argc, argv);

    qDebug() << "Requested OpenGL format:" << QSurfaceFormat::defaultFormat();
    if (softwareOpenGLRequested()) {
        qDebug() << "Software OpenGL rendering requested.";
    }

    MainWindow window;
    window.show();

    return app.exec();
}
