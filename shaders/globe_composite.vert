#version 330 core

// The pass that puts the globe texture (a Mercator image of the visible part of the world)
// onto the orthographic globe; see globe_composite.frag.

layout(location = 0) in vec2 a_position;   // screen px

uniform vec2 u_viewportSize;

out vec2 v_screen;

void main()
{
    gl_Position = vec4(a_position.x * (2.0 / u_viewportSize.x) - 1.0, 1.0 - a_position.y * (2.0 / u_viewportSize.y), 0.0, 1.0);
    v_screen = a_position;
}
