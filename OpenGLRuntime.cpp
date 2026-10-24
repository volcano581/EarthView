#include "OpenGLRuntime.h"

#include <QByteArray>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QStringList>

namespace {
bool rendererLooksSoftware(const QString& renderer)
{
    const QString value = renderer.toLower();
    return value.contains(QStringLiteral("software"))
        || value.contains(QStringLiteral("llvmpipe"))
        || value.contains(QStringLiteral("softpipe"))
        || value.contains(QStringLiteral("swiftshader"))
        || value.contains(QStringLiteral("opengl32sw"))
        || value.contains(QStringLiteral("gdi generic"))
        || value.contains(QStringLiteral("warp"));
}
}

namespace OpenGLRuntime {

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

bool forceOpenGL33Requested()
{
    return environmentFlagEnabled("EARTHVIEW_FORCE_OPENGL_33");
}

QSurfaceFormat defaultSurfaceFormat()
{
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setSwapInterval(0);

    if (softwareOpenGLRequested() || forceOpenGL33Requested()) {
        format.setVersion(3, 3);
    }
    else {
        format.setVersion(4, 5);
    }

    return format;
}

bool isAtLeast(const QSurfaceFormat& format, int major, int minor)
{
    return format.majorVersion() > major
        || (format.majorVersion() == major && format.minorVersion() >= minor);
}

QString rendererString(QOpenGLContext* context)
{
    if (!context || !context->functions())
        return QString();

    const GLubyte* renderer = context->functions()->glGetString(GL_RENDERER);
    return renderer ? QString::fromLatin1(reinterpret_cast<const char*>(renderer)) : QString();
}

bool isSoftwareRenderer(QOpenGLContext* context)
{
    return softwareOpenGLRequested() || rendererLooksSoftware(rendererString(context));
}

bool supportsOpenGL45Core(QOpenGLContext* context)
{
    if (!context)
        return false;

    return context->format().profile() == QSurfaceFormat::CoreProfile
        && isAtLeast(context->format(), 4, 5);
}

bool supportsPersistentMappedBuffers(QOpenGLContext* context)
{
    if (!supportsOpenGL45Core(context) || isSoftwareRenderer(context))
        return false;

    return true;
}

QString capabilitySummary(QOpenGLContext* context)
{
    if (!context)
        return QStringLiteral("No OpenGL context");

    const QSurfaceFormat format = context->format();
    QStringList parts;
    parts << QStringLiteral("actual=%1.%2").arg(format.majorVersion()).arg(format.minorVersion())
          << QStringLiteral("profile=%1").arg(format.profile())
          << QStringLiteral("renderer=%1").arg(rendererString(context))
          << QStringLiteral("software=%1").arg(isSoftwareRenderer(context) ? QStringLiteral("yes") : QStringLiteral("no"))
          << QStringLiteral("gl45=%1").arg(supportsOpenGL45Core(context) ? QStringLiteral("yes") : QStringLiteral("no"))
          << QStringLiteral("persistentBuffers=%1").arg(supportsPersistentMappedBuffers(context) ? QStringLiteral("yes") : QStringLiteral("no"));
    return parts.join(QStringLiteral(", "));
}

}
