#include <QApplication>
#include <QSurfaceFormat>
#include <QCoreApplication>
#include <QDebug>
#include "MainWindow.h"



int main(int argc, char* argv[])
{


  

    QApplication app(argc, argv);

    qDebug() << "Requested OpenGL format:" << QSurfaceFormat::defaultFormat();

    MainWindow window;
    window.show();

    return app.exec();
}