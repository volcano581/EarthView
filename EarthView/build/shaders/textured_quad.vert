#version 450 core

layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_texCoord;

uniform vec2 u_viewportSize;

out vec2 v_texCoord;

void main()
{
    vec2 ndc = vec2(
        a_position.x * (2.0 / u_viewportSize.x) - 1.0,
        1.0 - a_position.y * (2.0 / u_viewportSize.y));

    gl_Position = vec4(ndc, 0.0, 1.0);
    v_texCoord = a_texCoord;
}
