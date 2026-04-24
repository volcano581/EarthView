#include "BorderRenderer.h"
#include "Camera.h"
#include "MercatorProjection.h"
#include "Constants.h"
#include <QFile>
#include <QFileInfo>
#include <QOpenGLExtraFunctions>
#include <QString>
#include <cmath>
#include <cstring>

namespace {
enum class ShapeCoordinateMode {
    GeographicDegrees,
    WebMercatorMeters,
    MercatorRadians
};

bool hasBytes(const QByteArray& data, int offset, int count)
{
    return offset >= 0 && count >= 0 && offset <= data.size() - count;
}

qint32 readInt32BE(const QByteArray& data, int offset)
{
    const auto* bytes = reinterpret_cast<const unsigned char*>(data.constData() + offset);
    return (static_cast<qint32>(bytes[0]) << 24)
        | (static_cast<qint32>(bytes[1]) << 16)
        | (static_cast<qint32>(bytes[2]) << 8)
        | static_cast<qint32>(bytes[3]);
}

qint32 readInt32LE(const QByteArray& data, int offset)
{
    const auto* bytes = reinterpret_cast<const unsigned char*>(data.constData() + offset);
    return static_cast<qint32>(bytes[0])
        | (static_cast<qint32>(bytes[1]) << 8)
        | (static_cast<qint32>(bytes[2]) << 16)
        | (static_cast<qint32>(bytes[3]) << 24);
}

double readDoubleLE(const QByteArray& data, int offset)
{
    double value = 0.0;
    char bytes[sizeof(double)];
    for (int i = 0; i < 8; ++i) {
        bytes[i] = data[offset + i];
    }
    std::memcpy(&value, bytes, sizeof(double));
    return value;
}

ShapeCoordinateMode detectCoordinateMode(const QString& filePath, const QRectF& bounds)
{
    QFile prjFile(QFileInfo(filePath).path() + "/" + QFileInfo(filePath).completeBaseName() + ".prj");
    if (prjFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString projection = QString::fromUtf8(prjFile.readAll()).toLower();
        if (projection.contains("projcs") || projection.contains("mercator")) {
            return ShapeCoordinateMode::WebMercatorMeters;
        }
        if (projection.contains("geogcs") || projection.contains("degree")) {
            return ShapeCoordinateMode::GeographicDegrees;
        }
    }

    const double maxAbsX = qMax(std::abs(bounds.left()), std::abs(bounds.right()));
    const double maxAbsY = qMax(std::abs(bounds.top()), std::abs(bounds.bottom()));

    if (maxAbsX <= 180.0 && maxAbsY <= 90.0) {
        return ShapeCoordinateMode::GeographicDegrees;
    }
    if (maxAbsX <= M_PI * 2.0 && maxAbsY <= M_PI * 2.0) {
        return ShapeCoordinateMode::MercatorRadians;
    }
    return ShapeCoordinateMode::WebMercatorMeters;
}

QPointF projectPoint(double x, double y, ShapeCoordinateMode mode)
{
    switch (mode) {
    case ShapeCoordinateMode::GeographicDegrees:
        return MercatorProjection::latLonToMercator(y, x);
    case ShapeCoordinateMode::WebMercatorMeters:
        return QPointF(x / GIS::EARTH_RADIUS, y / GIS::EARTH_RADIUS);
    case ShapeCoordinateMode::MercatorRadians:
        return QPointF(x, y);
    }

    return QPointF(x, y);
}

bool isSupportedShapeType(int shapeType)
{
    return shapeType == 3 || shapeType == 5
        || shapeType == 13 || shapeType == 15
        || shapeType == 23 || shapeType == 25;
}
}

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

void BorderRenderer::clearBorders()
{
    m_borders.clear();
}

bool BorderRenderer::loadShapefile(const QString& filePath, QString* errorMessage)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QString("Could not open %1").arg(filePath);
        }
        return false;
    }

    QByteArray data = file.readAll();
    if (data.size() < 100) {
        if (errorMessage) {
            *errorMessage = "The shapefile is too small to contain a valid header.";
        }
        return false;
    }

    if (readInt32BE(data, 0) != 9994) {
        if (errorMessage) {
            *errorMessage = "The file is not an ESRI shapefile.";
        }
        return false;
    }

    int shapeType = readInt32LE(data, 32);
    if (!isSupportedShapeType(shapeType)) {
        if (errorMessage) {
            *errorMessage = QString("Unsupported shapefile type %1. Expected polygon or polyline data.").arg(shapeType);
        }
        return false;
    }

    QRectF fileBounds(
        QPointF(readDoubleLE(data, 36), readDoubleLE(data, 44)),
        QPointF(readDoubleLE(data, 52), readDoubleLE(data, 60)));
    ShapeCoordinateMode coordinateMode = detectCoordinateMode(filePath, fileBounds);

    QVector<BorderPolygon> loadedBorders;
    int offset = 100;

    while (hasBytes(data, offset, 8)) {
        int contentLengthBytes = readInt32BE(data, offset + 4) * 2;
        int recordOffset = offset + 8;
        int nextRecordOffset = recordOffset + contentLengthBytes;

        if (contentLengthBytes < 4 || !hasBytes(data, recordOffset, contentLengthBytes)) {
            break;
        }

        int recordShapeType = readInt32LE(data, recordOffset);
        if (recordShapeType == 0) {
            offset = nextRecordOffset;
            continue;
        }

        if (!isSupportedShapeType(recordShapeType) || contentLengthBytes < 44) {
            offset = nextRecordOffset;
            continue;
        }

        int numParts = readInt32LE(data, recordOffset + 36);
        int numPoints = readInt32LE(data, recordOffset + 40);
        int partsOffset = recordOffset + 44;
        int pointsOffset = partsOffset + numParts * 4;

        if (numParts <= 0 || numPoints <= 0
            || !hasBytes(data, partsOffset, numParts * 4)
            || !hasBytes(data, pointsOffset, numPoints * 16)) {
            offset = nextRecordOffset;
            continue;
        }

        QVector<int> parts;
        parts.reserve(numParts + 1);
        for (int i = 0; i < numParts; ++i) {
            parts.append(readInt32LE(data, partsOffset + i * 4));
        }
        parts.append(numPoints);

        for (int partIndex = 0; partIndex < numParts; ++partIndex) {
            int startPoint = parts[partIndex];
            int endPoint = parts[partIndex + 1];

            if (startPoint < 0 || endPoint > numPoints || endPoint - startPoint < 2) {
                continue;
            }

            BorderPolygon polygon;
            polygon.points.reserve(endPoint - startPoint);
            polygon.isLand = recordShapeType == 5 || recordShapeType == 15 || recordShapeType == 25;

            for (int pointIndex = startPoint; pointIndex < endPoint; ++pointIndex) {
                int pointOffset = pointsOffset + pointIndex * 16;
                QPointF mercator = projectPoint(
                    readDoubleLE(data, pointOffset),
                    readDoubleLE(data, pointOffset + 8),
                    coordinateMode);

                BorderPoint point;
                point.x = mercator.x();
                point.y = mercator.y();
                polygon.points.append(point);
            }

            loadedBorders.append(polygon);
        }

        offset = nextRecordOffset;
    }

    if (loadedBorders.isEmpty()) {
        if (errorMessage) {
            *errorMessage = "No drawable polygon or polyline parts were found in the shapefile.";
        }
        return false;
    }

    m_borders = loadedBorders;
    if (errorMessage) {
        *errorMessage = QString("Loaded %1 border parts from %2.")
            .arg(m_borders.size())
            .arg(QFileInfo(filePath).fileName());
    }
    return true;
}

void BorderRenderer::render()
{
    glDisable(GL_TEXTURE_2D);
    glLineWidth(2.0f);
    glColor4f(0.2f, 0.2f, 0.8f, 0.8f);  // Blue borders

    if (m_camera->isOrthographic()) {
        for (const auto& polygon : m_borders) {
            if (polygon.points.size() < 2)
                continue;

            bool drawing = false;
            for (const auto& point : polygon.points) {
                QPointF screen;
                if (m_camera->projectMercatorToScreen(QPointF(point.x, point.y), &screen)) {
                    if (!drawing) {
                        glBegin(GL_LINE_STRIP);
                        drawing = true;
                    }
                    glVertex2f(screen.x(), screen.y());
                }
                else if (drawing) {
                    glEnd();
                    drawing = false;
                }
            }

            if (drawing) {
                glEnd();
            }
        }

        glEnable(GL_TEXTURE_2D);
        return;
    }

    const double worldWidth = GIS::MAX_MERCATOR_X - GIS::MIN_MERCATOR_X;
    int firstCopy = 0;
    int lastCopy = 0;

    if (m_camera->isHorizontalWrapEnabled()) {
        QRectF extent = m_camera->getVisibleMercatorExtent();
        firstCopy = static_cast<int>(std::ceil((extent.left() - GIS::MAX_MERCATOR_X) / worldWidth));
        lastCopy = static_cast<int>(std::floor((extent.right() - GIS::MIN_MERCATOR_X) / worldWidth));
    }

    for (const auto& polygon : m_borders) {
        if (polygon.points.size() < 2)
            continue;

        for (int copy = firstCopy; copy <= lastCopy; ++copy) {
            const double xOffset = copy * worldWidth;

            glBegin(GL_LINE_STRIP);
            for (const auto& point : polygon.points) {
                QPointF screen = m_camera->mercatorToScreen(QPointF(point.x + xOffset, point.y));
                glVertex2f(screen.x(), screen.y());
            }
            glEnd();
        }
    }

    glEnable(GL_TEXTURE_2D);
}
