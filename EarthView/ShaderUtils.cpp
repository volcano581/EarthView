#include "ShaderUtils.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QOpenGLShaderProgram>
#include <QStringList>

namespace {
QStringList shaderSearchRoots()
{
    QStringList roots;

    const QString appPath = QCoreApplication::applicationDirPath();
    if (!appPath.isEmpty()) {
        roots << QDir(appPath).filePath("shaders")
              << QDir(appPath).filePath("../shaders");
    }

    roots << QDir::current().filePath("shaders")
          << QDir::current().filePath("../shaders");

    const QFileInfo sourceFileInfo(QStringLiteral(__FILE__));
    const QDir sourceDir = sourceFileInfo.isAbsolute()
        ? sourceFileInfo.dir()
        : QDir(QDir::current().absoluteFilePath(sourceFileInfo.path()));
    roots << sourceDir.filePath("shaders");

    roots.removeDuplicates();
    return roots;
}
}

QString ShaderUtils::shaderPath(const QString& fileName)
{
    for (const QString& root : shaderSearchRoots()) {
        const QString candidate = QDir(root).filePath(fileName);
        if (QFileInfo::exists(candidate)) {
            return QFileInfo(candidate).absoluteFilePath();
        }
    }

    return QString();
}

bool ShaderUtils::loadProgram(
    QOpenGLShaderProgram* program,
    const QString& vertexShaderFile,
    const QString& fragmentShaderFile,
    QString* errorMessage)
{
    if (!program) {
        if (errorMessage) {
            *errorMessage = "No shader program was provided.";
        }
        return false;
    }

    const QString vertexPath = shaderPath(vertexShaderFile);
    const QString fragmentPath = shaderPath(fragmentShaderFile);
    if (vertexPath.isEmpty() || fragmentPath.isEmpty()) {
        const QString message = QString("Could not find shader files %1 and %2.")
            .arg(vertexShaderFile, fragmentShaderFile);
        if (errorMessage) {
            *errorMessage = message;
        }
        qWarning() << message;
        return false;
    }

    if (!program->addShaderFromSourceFile(QOpenGLShader::Vertex, vertexPath)) {
        const QString message = QString("Vertex shader %1 failed: %2")
            .arg(vertexShaderFile, program->log());
        if (errorMessage) {
            *errorMessage = message;
        }
        qWarning() << message;
        return false;
    }

    if (!program->addShaderFromSourceFile(QOpenGLShader::Fragment, fragmentPath)) {
        const QString message = QString("Fragment shader %1 failed: %2")
            .arg(fragmentShaderFile, program->log());
        if (errorMessage) {
            *errorMessage = message;
        }
        qWarning() << message;
        return false;
    }

    if (!program->link()) {
        const QString message = QString("Shader program link failed: %1").arg(program->log());
        if (errorMessage) {
            *errorMessage = message;
        }
        qWarning() << message;
        return false;
    }

    return true;
}
