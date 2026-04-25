#version 450 core

layout(location = 0) in vec2 a_position;
layout(location = 1) in vec4 a_color;
layout(location = 2) in float a_size;

uniform vec2 u_viewportSize;

out vec4 v_color;

void main()
{
    vec2 ndc = vec2(
        a_position.x * (2.0 / u_viewportSize.x) - 1.0,
        1.0 - a_position.y * (2.0 / u_viewportSize.y));

    gl_Position = vec4(ndc, 0.0, 1.0);
    gl_PointSize = a_size;
    v_color = a_color;
}
