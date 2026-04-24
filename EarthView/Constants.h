#pragma once
#ifndef CONSTANTS_H
#define CONSTANTS_H
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
namespace GIS {
    // Mathematical constants
    constexpr double EARTH_RADIUS = 6378137.0;
    constexpr double EARTH_CIRCUMFERENCE = 40075016.686;
    
    // Mercator projection bounds
    constexpr double MIN_MERCATOR_X = -M_PI;
    constexpr double MAX_MERCATOR_X = M_PI;
    constexpr double MIN_MERCATOR_Y = -M_PI/2;
    constexpr double MAX_MERCATOR_Y = M_PI/2;
    
    // Camera zoom bounds
    constexpr double MIN_ZOOM = 0.0;
    constexpr double MAX_ZOOM = 18.0;
    
    // Tile settings
    constexpr int MAX_TILE_ZOOM = 18;
    constexpr int TEXTURE_CACHE_SIZE_MB = 200;
}

#endif // CONSTANTS_H
