#version 330 core

uniform sampler2D u_texture;

in vec2 v_texCoord;
in float v_facing;

out vec4 fragColor;

void main()
{
    if (v_facing < 0.0)
        discard;
    fragColor = texture(u_texture, v_texCoord);
}
