#version 330 core

layout(location = 0) in vec2 a_mercator;

uniform vec2 u_viewportSize;
uniform vec2 u_centerMercator;
uniform float u_pixelsPerMeter;
uniform float u_earthRadius;
uniform float u_pitchRadians;
uniform vec2 u_screenAnchor;
uniform float u_focalPixels;
uniform float u_viewDistanceMeters;
uniform float u_worldOffset;

void main()
{
    vec2 mercator = vec2(a_mercator.x + u_worldOffset, a_mercator.y);
    vec2 localMeters = (mercator - u_centerMercator) * u_earthRadius;
    float cosPitch = cos(u_pitchRadians);
    float sinPitch = sin(u_pitchRadians);
    float tiltedNorthMeters = localMeters.y * cosPitch;
    float depthMeters = localMeters.y * sinPitch;
    float perspective = clamp(
        u_focalPixels / max(u_focalPixels + depthMeters * u_pixelsPerMeter, 1.0),
        0.35,
        3.0);

    vec2 screen = vec2(
        u_screenAnchor.x + localMeters.x * u_pixelsPerMeter * perspective,
        u_screenAnchor.y - tiltedNorthMeters * u_pixelsPerMeter * perspective);

    vec2 ndc = vec2(
        screen.x * (2.0 / u_viewportSize.x) - 1.0,
        1.0 - screen.y * (2.0 / u_viewportSize.y));

    float z = clamp(depthMeters / max(u_viewDistanceMeters, 1.0), -0.95, 0.95);
    gl_Position = vec4(ndc, z, 1.0);
}
