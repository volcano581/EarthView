#version 330 core

layout(location = 0) in vec2 a_mercator;
layout(location = 1) in vec2 a_texCoord;

uniform vec2 u_viewportSize;
uniform vec2 u_centerMercator;
uniform float u_pixelsPerMercator;
uniform float u_pixelsPerMeter;
uniform float u_earthRadius;
uniform float u_pitchRadians;
uniform float u_yawRadians;
uniform float u_verticalExaggeration;
uniform vec2 u_screenAnchor;
uniform float u_focalPixels;
uniform float u_viewDistanceMeters;
uniform sampler2D u_heightTexture;
uniform float u_minHeight;
uniform float u_maxHeight;

out float v_heightMix;
out float v_heightMeters;

void main()
{
    float height = texture(u_heightTexture, a_texCoord).r;
    float heightRange = max(u_maxHeight - u_minHeight, 1.0);
    v_heightMix = clamp((height - u_minHeight) / heightRange, 0.0, 1.0);
    v_heightMeters = height;

    vec2 localMeters = (a_mercator - u_centerMercator) * u_earthRadius;
    float cosYaw = cos(u_yawRadians);
    float sinYaw = sin(u_yawRadians);
    vec2 viewMeters = vec2(
        localMeters.x * cosYaw - localMeters.y * sinYaw,
        localMeters.x * sinYaw + localMeters.y * cosYaw);
    float elevationMeters = max(height, 0.0) * u_verticalExaggeration;
    float cosPitch = cos(u_pitchRadians);
    float sinPitch = sin(u_pitchRadians);

    float tiltedForwardMeters = viewMeters.y * cosPitch + elevationMeters * sinPitch;
    float depthMeters = viewMeters.y * sinPitch - elevationMeters * cosPitch;
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
}
