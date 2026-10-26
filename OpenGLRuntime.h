#pragma once
#ifndef OPENGLRUNTIME_H
#define OPENGLRUNTIME_H

#include <QSurfaceFormat>
#include <QString>

class QOpenGLContext;

namespace OpenGLRuntime {

bool environmentFlagEnabled(const char* name);
bool softwareOpenGLRequested();
bool forceOpenGL33Requested();
QSurfaceFormat defaultSurfaceFormat();

bool isAtLeast(const QSurfaceFormat& format, int major, int minor);
QString rendererString(QOpenGLContext* context);
bool isSoftwareRenderer(QOpenGLContext* context);
bool supportsOpenGL45Core(QOpenGLContext* context);
bool supportsPersistentMappedBuffers(QOpenGLContext* context);
QString capabilitySummary(QOpenGLContext* context);

}

#endif // OPENGLRUNTIME_H
