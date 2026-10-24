#version 330 core

layout(location = 0) in vec2 a_corner;
layout(location = 1) in vec4 a_rect;
layout(location = 2) in vec4 a_color;

uniform vec2 u_viewportSize;
uniform vec2 u_centerMercator;
uniform float u_pixelsPerMercator;

out vec4 v_color;

void main()
{
    vec2 mercator = vec2(
        mix(a_rect.x, a_rect.z, a_corner.x),
        mix(a_rect.y, a_rect.w, a_corner.y));

    vec2 screen = vec2(
        u_viewportSize.x * 0.5 + (mercator.x - u_centerMercator.x) * u_pixelsPerMercator,
        u_viewportSize.y * 0.5 - (mercator.y - u_centerMercator.y) * u_pixelsPerMercator);

    vec2 ndc = vec2(
        screen.x * (2.0 / u_viewportSize.x) - 1.0,
        1.0 - screen.y * (2.0 / u_viewportSize.y));

    gl_Position = vec4(ndc, 0.0, 1.0);
    v_color = a_color;
}
