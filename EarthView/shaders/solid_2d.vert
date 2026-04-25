#version 450 core

layout(location = 0) in vec2 a_position;

uniform vec2 u_viewportSize;

void main()
{
    vec2 ndc = vec2(
        a_position.x * (2.0 / u_viewportSize.x) - 1.0,
        1.0 - a_position.y * (2.0 / u_viewportSize.y));

    gl_Position = vec4(ndc, 0.0, 1.0);
}
