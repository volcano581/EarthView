#version 330 core

layout(location = 0) in vec2 a_mercator;

uniform vec2 u_viewportSize;
uniform vec2 u_centerMercator;
uniform float u_pixelsPerMercator;
uniform float u_worldOffset;

void main()
{
    vec2 screen = vec2(
        u_viewportSize.x * 0.5 + ((a_mercator.x + u_worldOffset) - u_centerMercator.x) * u_pixelsPerMercator,
        u_viewportSize.y * 0.5 - (a_mercator.y - u_centerMercator.y) * u_pixelsPerMercator);

    vec2 ndc = vec2(
        screen.x * (2.0 / u_viewportSize.x) - 1.0,
        1.0 - screen.y * (2.0 / u_viewportSize.y));

    gl_Position = vec4(ndc, 0.0, 1.0);
}
