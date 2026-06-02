#version 330 core

uniform float u_opacity;
uniform int u_wireMode;

in float v_heightMix;
in float v_heightMeters;

out vec4 fragColor;

void main()
{
    if (u_wireMode == 1) {
        fragColor = vec4(0.03, 0.04, 0.035, 0.42);
        return;
    }

    vec3 lowColor = vec3(0.12, 0.34, 0.17);
    vec3 midColor = vec3(0.55, 0.48, 0.30);
    vec3 highColor = vec3(0.88, 0.86, 0.78);
    vec3 snowColor = vec3(0.96, 0.97, 0.94);

    vec3 terrainColor = v_heightMix < 0.50
        ? mix(lowColor, midColor, v_heightMix / 0.50)
        : mix(midColor, highColor, (v_heightMix - 0.50) / 0.50);
    terrainColor = mix(terrainColor, snowColor, smoothstep(2600.0, 4200.0, v_heightMeters));

    float slope = clamp(length(vec2(dFdx(v_heightMix), dFdy(v_heightMix))) * 26.0, 0.0, 0.55);
    float shade = clamp(0.78 + v_heightMix * 0.28 - slope, 0.35, 1.15);
    vec3 color = terrainColor * shade;

    fragColor = vec4(color, u_opacity);
}
