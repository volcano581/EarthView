#pragma once
#ifndef BORDERRENDERER_H
#define BORDERRENDERER_H

#include <QObject>
#include <QOpenGLFunctions>
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <QString>
#include <QVector>

class Camera;

/**
 * @brief BorderRenderer renders world borders to OpenGL
 *
 * Responsibilities:
 * - Load border data
 * - Render borders as line strips
 */
class BorderRenderer : public QObject, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    explicit BorderRenderer(Camera* camera, QObject* parent = nullptr);
    ~BorderRenderer();

    bool loadShapefile(const QString& filePath, QString* errorMessage = nullptr);
    void clearBorders();
    int borderCount() const { return m_borders.size(); }
    void render();

private:
    struct BorderPoint {
        double x;  // Mercator X
        double y;  // Mercator Y
    };

    struct BorderPolygon {
        QVector<BorderPoint> points;
        bool isLand;
    };

private:
    void initializeGpuResources();

private:
    Camera* m_camera;
    QVector<BorderPolygon> m_borders;
    QOpenGLShaderProgram m_lineProgram;
    GLuint m_vbo;
    GLuint m_vao;
    bool m_initialized;
};

#endif // BORDERRENDERER_H
