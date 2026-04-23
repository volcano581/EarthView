#include "borderrenderer.h"
#include "camera.h"
#include "mercatorprojection.h"
#include <QOpenGLExtraFunctions>

BorderRenderer::BorderRenderer(Camera* camera, QObject* parent)
    : QObject(parent)
    , m_camera(camera)
    , m_vbo(0)
    , m_vao(0)
    , m_initialized(false)
{
    initializeOpenGLFunctions();
}

BorderRenderer::~BorderRenderer()
{
    if (m_initialized) {
        if (m_vbo) glDeleteBuffers(1, &m_vbo);
        if (m_vao) {
            QOpenGLExtraFunctions* f = QOpenGLContext::currentContext()->extraFunctions();
            if (f) {
                f->glDeleteVertexArrays(1, &m_vao);
            }
        }
    }
}

void BorderRenderer::loadWorldBorders()
{
    // Major world borders (simplified polygons)
    QVector<QVector<QPointF>> continents = {
        // North America (simplified)
        {{-130.0, 30.0}, {-120.0, 25.0}, {-100.0, 25.0}, {-95.0, 30.0}, {-90.0, 35.0},
         {-85.0, 40.0}, {-80.0, 45.0}, {-75.0, 45.0}, {-70.0, 45.0}, {-65.0, 45.0},
         {-65.0, 50.0}, {-70.0, 55.0}, {-80.0, 60.0}, {-90.0, 60.0}, {-100.0, 60.0},
         {-110.0, 60.0}, {-120.0, 55.0}, {-130.0, 50.0}, {-140.0, 50.0}, {-150.0, 50.0},
         {-160.0, 55.0}, {-170.0, 60.0}, {180.0, 65.0}, {170.0, 60.0}, {160.0, 55.0},
         {-130.0, 30.0}},

         // South America
         {{-80.0, -10.0}, {-75.0, -15.0}, {-70.0, -20.0}, {-65.0, -25.0}, {-60.0, -30.0},
          {-55.0, -35.0}, {-50.0, -40.0}, {-45.0, -40.0}, {-40.0, -35.0}, {-35.0, -30.0},
          {-40.0, -25.0}, {-45.0, -20.0}, {-50.0, -15.0}, {-55.0, -10.0}, {-60.0, -5.0},
          {-65.0, 0.0}, {-70.0, 5.0}, {-75.0, 10.0}, {-80.0, 15.0}, {-80.0, -10.0}},

          // Africa
          {{-20.0, 35.0}, {-15.0, 30.0}, {-10.0, 25.0}, {-5.0, 20.0}, {0.0, 15.0},
           {5.0, 10.0}, {10.0, 5.0}, {15.0, 0.0}, {20.0, -5.0}, {25.0, -10.0},
           {30.0, -15.0}, {35.0, -20.0}, {40.0, -25.0}, {45.0, -20.0}, {50.0, -15.0},
           {55.0, -10.0}, {60.0, -5.0}, {55.0, 0.0}, {50.0, 5.0}, {45.0, 10.0},
           {40.0, 15.0}, {35.0, 20.0}, {30.0, 25.0}, {25.0, 30.0}, {20.0, 35.0},
           {15.0, 35.0}, {10.0, 30.0}, {5.0, 30.0}, {0.0, 35.0}, {-5.0, 35.0},
           {-10.0, 35.0}, {-15.0, 35.0}, {-20.0, 35.0}},

           // Eurasia
           {{-10.0, 35.0}, {0.0, 40.0}, {10.0, 45.0}, {20.0, 45.0}, {30.0, 45.0},
            {40.0, 45.0}, {50.0, 45.0}, {60.0, 50.0}, {70.0, 55.0}, {80.0, 55.0},
            {90.0, 55.0}, {100.0, 55.0}, {110.0, 55.0}, {120.0, 50.0}, {130.0, 45.0},
            {140.0, 45.0}, {150.0, 50.0}, {160.0, 55.0}, {170.0, 60.0}, {180.0, 65.0},
            {170.0, 70.0}, {160.0, 75.0}, {150.0, 70.0}, {140.0, 65.0}, {130.0, 60.0},
            {120.0, 60.0}, {110.0, 60.0}, {100.0, 60.0}, {90.0, 60.0}, {80.0, 60.0},
            {70.0, 60.0}, {60.0, 60.0}, {50.0, 60.0}, {40.0, 55.0}, {30.0, 50.0},
            {20.0, 45.0}, {10.0, 40.0}, {0.0, 40.0}, {-10.0, 40.0}, {-10.0, 35.0}},

            // Australia
            {{110.0, -10.0}, {115.0, -15.0}, {120.0, -20.0}, {125.0, -25.0}, {130.0, -25.0},
             {135.0, -25.0}, {140.0, -20.0}, {145.0, -15.0}, {150.0, -10.0}, {155.0, -10.0},
             {150.0, -15.0}, {145.0, -20.0}, {140.0, -25.0}, {135.0, -30.0}, {130.0, -35.0},
             {125.0, -35.0}, {120.0, -30.0}, {115.0, -25.0}, {110.0, -20.0}, {110.0, -10.0}}
    };

    for (const auto& continent : continents) {
        BorderPolygon polygon;
        for (const auto& point : continent) {
            QPointF mercator = MercatorProjection::latLonToMercator(point.y(), point.x());
            BorderPoint bp;
            bp.x = mercator.x();
            bp.y = mercator.y();
            polygon.points.append(bp);
        }
        polygon.isLand = true;
        m_borders.append(polygon);
    }
}

void BorderRenderer::render()
{
    glDisable(GL_TEXTURE_2D);
    glLineWidth(2.0f);
    glColor4f(0.2f, 0.2f, 0.8f, 0.8f);  // Blue borders

    for (const auto& polygon : m_borders) {
        if (polygon.points.size() < 2)
            continue;

        glBegin(GL_LINE_STRIP);
        for (const auto& point : polygon.points) {
            QPointF screen = m_camera->mercatorToScreen(QPointF(point.x, point.y));
            glVertex2f(screen.x(), screen.y());
        }
        glEnd();
    }

    glEnable(GL_TEXTURE_2D);
}