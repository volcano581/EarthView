#version 450 core

in vec4 v_color;

out vec4 fragColor;

void main()
{
    vec2 centered = gl_PointCoord * 2.0 - 1.0;
    float distanceFromCenter = length(centered);
    float alpha = 1.0 - smoothstep(0.82, 1.0, distanceFromCenter);

    if (alpha <= 0.0) {
        discard;
    }

    vec3 rimColor = vec3(0.04, 0.09, 0.13);
    float rim = smoothstep(0.62, 0.78, distanceFromCenter);
    fragColor = vec4(mix(v_color.rgb, rimColor, rim * 0.45), v_color.a * alpha);
}
