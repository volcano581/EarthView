#pragma once
#ifndef BORDERRENDERER_H
#define BORDERRENDERER_H

#include <QObject>
#include <QOpenGLFunctions>
#include <QOpenGLExtraFunctions>
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

    void loadWorldBorders();
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
    Camera* m_camera;
    QVector<BorderPolygon> m_borders;
    GLuint m_vbo;
    GLuint m_vao;
    bool m_initialized;
};

#endif // BORDERRENDERER_H