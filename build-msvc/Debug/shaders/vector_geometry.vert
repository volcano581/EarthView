#version 330 core

// Vector map areas and hairlines, in Mercator coordinates, to the screen or (on the globe) to
// the globe texture: a Mercator image of the visible part of the world that
// globe_composite.frag maps onto the orthographic globe.

layout(location = 0) in vec2 a_mercator;
layout(location = 1) in vec4 a_color;

uniform vec2 u_viewportSize;
uniform vec2 u_centerMercator;
uniform float u_pixelsPerMercator;

out vec4 v_color;

void main()
{
    vec2 pixel = vec2(
        u_viewportSize.x * 0.5 + (a_mercator.x - u_centerMercator.x) * u_pixelsPerMercator,
        u_viewportSize.y * 0.5 - (a_mercator.y - u_centerMercator.y) * u_pixelsPerMercator);
    gl_Position = vec4(pixel.x * (2.0 / u_viewportSize.x) - 1.0, 1.0 - pixel.y * (2.0 / u_viewportSize.y), 0.0, 1.0);
    v_color = a_color;
}
