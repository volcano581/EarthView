#pragma once
#ifndef SHADERUTILS_H
#define SHADERUTILS_H

#include <QString>

class QOpenGLShaderProgram;

namespace ShaderUtils {
QString shaderPath(const QString& fileName);
bool loadProgram(
    QOpenGLShaderProgram* program,
    const QString& vertexShaderFile,
    const QString& fragmentShaderFile,
    QString* errorMessage = nullptr);
}

#endif // SHADERUTILS_H
