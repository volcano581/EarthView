#version 450 core

layout(binding = 0) uniform sampler2D u_texture;

in vec2 v_texCoord;

out vec4 fragColor;

void main()
{
    fragColor = texture(u_texture, v_texCoord);
}
