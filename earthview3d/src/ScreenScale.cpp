#include "earthview3d/ScreenScale.h"

#include <cmath>

namespace earthview3d::screenscale {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

double metresPerPixel(double distanceM, double verticalFovDeg, int viewportHeightPx)
{
    if (!(distanceM > 0.0) || !(verticalFovDeg > 0.0) || verticalFovDeg >= 180.0 || viewportHeightPx <= 0)
        return 0.0;
    // Height of the view frustum at distanceM, divided by the pixels it is drawn into.
    const double frustumHeightM = 2.0 * distanceM * std::tan(0.5 * verticalFovDeg * kPi / 180.0);
    return frustumHeightM / double(viewportHeightPx);
}

double markerScale(double markerSizeM, double metresPerPixel, double minPixels, double maxMarkerSizeM)
{
    if (!(markerSizeM > 0.0) || !(metresPerPixel > 0.0) || !(minPixels > 0.0))
        return 1.0;
    double scale = minPixels * metresPerPixel / markerSizeM;
    if (maxMarkerSizeM > 0.0)
        scale = std::fmin(scale, maxMarkerSizeM / markerSizeM);
    return scale > 1.0 ? scale : 1.0;
}

double pixelSize(double sizeM, double metresPerPixel)
{
    if (!(sizeM > 0.0) || !(metresPerPixel > 0.0))
        return 0.0;
    return sizeM / metresPerPixel;
}

} // namespace earthview3d::screenscale
