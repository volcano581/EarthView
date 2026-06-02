#version 330 core

layout(location = 0) in vec2 a_mercator;
layout(location = 1) in vec4 a_color;

uniform vec2 u_viewportSize;
uniform vec2 u_centerMercator;
uniform float u_pixelsPerMeter;
uniform float u_earthRadius;
uniform float u_pitchRadians;
uniform float u_yawRadians;
uniform vec2 u_screenAnchor;
uniform float u_focalPixels;
uniform float u_viewDistanceMeters;

out vec4 v_color;

void main()
{
    vec2 localMeters = (a_mercator - u_centerMercator) * u_earthRadius;
    float cosYaw = cos(u_yawRadians);
    float sinYaw = sin(u_yawRadians);
    vec2 viewMeters = vec2(
        localMeters.x * cosYaw - localMeters.y * sinYaw,
        localMeters.x * sinYaw + localMeters.y * cosYaw);
    float cosPitch = cos(u_pitchRadians);
    float sinPitch = sin(u_pitchRadians);
    float tiltedForwardMeters = viewMeters.y * cosPitch;
    float depthMeters = viewMeters.y * sinPitch;
    float perspective = clamp(
        u_focalPixels / max(u_focalPixels + depthMeters * u_pixelsPerMeter, 1.0),
        0.35,
        3.0);

    vec2 screen = vec2(
        u_screenAnchor.x + viewMeters.x * u_pixelsPerMeter * perspective,
        u_screenAnchor.y - tiltedForwardMeters * u_pixelsPerMeter * perspective);

    vec2 ndc = vec2(
        screen.x * (2.0 / u_viewportSize.x) - 1.0,
        1.0 - screen.y * (2.0 / u_viewportSize.y));

    float z = clamp(depthMeters / max(u_viewDistanceMeters, 1.0), -0.95, 0.95);
    gl_Position = vec4(ndc, z, 1.0);
    v_color = a_color;
}
