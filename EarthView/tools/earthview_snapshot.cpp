#include "Camera.h"
#include "MapWidget.h"
#include "MercatorProjection.h"
#include "OpenGLRuntime.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QImage>
#include <QSurfaceFormat>
#include <QTextStream>

/**
 * @brief earthview_snapshot renders one frame of MapWidget offscreen and writes a PNG.
 *
 * Intended for CI artifacts and smoke tests on machines without a display or GPU:
 *   EARTHVIEW_FORCE_SOFTWARE_OPENGL=1 xvfb-run -a \
 *       earthview_snapshot --mode orthographic --lat 30 --lon 70 --zoom 3 -o out.png
 * (Distro Qt's "offscreen" platform plugin has no GL context support; Xvfb + Mesa
 * llvmpipe works.)
 *
 * QOpenGLWidget already renders into an FBO, so grabFramebuffer() gives the frame
 * without a visible window. Network tiles are not awaited; the frame shows whatever
 * is available after the first paint (backdrop, grid, borders).
 */
int main(int argc, char* argv[])
{
    if (OpenGLRuntime::softwareOpenGLRequested()) {
        QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
    }
    QSurfaceFormat::setDefaultFormat(OpenGLRuntime::defaultSurfaceFormat());

    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("earthview_snapshot"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Render one EarthView frame to a PNG."));
    parser.addHelpOption();
    const QCommandLineOption outOpt({QStringLiteral("o"), QStringLiteral("output")},
        QStringLiteral("Output PNG path."), QStringLiteral("file"), QStringLiteral("snapshot.png"));
    const QCommandLineOption modeOpt(QStringLiteral("mode"),
        QStringLiteral("mercator | orthographic."), QStringLiteral("mode"), QStringLiteral("mercator"));
    const QCommandLineOption latOpt(QStringLiteral("lat"), QStringLiteral("Center latitude (deg)."),
        QStringLiteral("deg"), QStringLiteral("0"));
    const QCommandLineOption lonOpt(QStringLiteral("lon"), QStringLiteral("Center longitude (deg)."),
        QStringLiteral("deg"), QStringLiteral("0"));
    const QCommandLineOption zoomOpt(QStringLiteral("zoom"), QStringLiteral("Zoom level."),
        QStringLiteral("z"), QStringLiteral("2"));
    const QCommandLineOption widthOpt(QStringLiteral("width"), QStringLiteral("Image width."),
        QStringLiteral("px"), QStringLiteral("800"));
    const QCommandLineOption heightOpt(QStringLiteral("height"), QStringLiteral("Image height."),
        QStringLiteral("px"), QStringLiteral("600"));
    parser.addOptions({outOpt, modeOpt, latOpt, lonOpt, zoomOpt, widthOpt, heightOpt});
    parser.process(app);

    QTextStream err(stderr);
    const QString mode = parser.value(modeOpt).toLower();
    if (mode != QLatin1String("mercator") && mode != QLatin1String("orthographic")) {
        err << "Unknown --mode: " << mode << '\n';
        return 2;
    }

    MapWidget widget;
    widget.setAttribute(Qt::WA_DontShowOnScreen);
    widget.resize(parser.value(widthOpt).toInt(), parser.value(heightOpt).toInt());
    widget.show();

    // grabFramebuffer() initializes GL on first use; configure the camera afterwards
    // so MapWidget's own initialization cannot override it.
    widget.grabFramebuffer();
    Camera* camera = widget.camera();
    camera->setProjectionMode(mode == QLatin1String("orthographic")
                                  ? Camera::ProjectionMode::Orthographic
                                  : Camera::ProjectionMode::Mercator);
    camera->setZoomLevel(parser.value(zoomOpt).toDouble());
    camera->setCenter(MercatorProjection::latLonToMercator(parser.value(latOpt).toDouble(),
                                                           parser.value(lonOpt).toDouble()));
    QCoreApplication::processEvents();

    const QImage image = widget.grabFramebuffer();
    if (image.isNull()) {
        err << "Framebuffer grab failed (no OpenGL context?).\n";
        return 1;
    }
    const QString outPath = parser.value(outOpt);
    if (!image.save(outPath, "PNG")) {
        err << "Could not write " << outPath << '\n';
        return 1;
    }
    QTextStream(stdout) << "Wrote " << outPath << " (" << image.width() << 'x' << image.height() << ")\n";
    return 0;
}
