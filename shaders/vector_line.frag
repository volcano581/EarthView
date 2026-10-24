#version 330 core

in vec4 v_color;
in vec2 v_local;
in float v_halfWidth;
in float v_length;
in float v_distance;
in vec2 v_dash;

out vec4 fragColor;

void main()
{
    // Distance to the segment: round ends, so consecutive segments join without notches.
    float across = abs(v_local.y);
    float distanceToLine = across;
    if (v_local.x < 0.0)
        distanceToLine = length(vec2(v_local.x, across));
    else if (v_local.x > v_length)
        distanceToLine = length(vec2(v_local.x - v_length, across));

    float drawnHalf = max(v_halfWidth, 0.5);
    float coverage = clamp(drawnHalf + 0.5 - distanceToLine, 0.0, 1.0);
    // Lines thinner than a pixel fade instead of thinning further.
    coverage *= clamp(v_halfWidth * 2.0, 0.15, 1.0);

    if (v_dash.x > 0.0) {
        float phase = mod(max(v_distance, 0.0), v_dash.x + v_dash.y);
        coverage *= clamp(v_dash.x - phase + 0.5, 0.0, 1.0);
    }
    if (coverage <= 0.0)
        discard;
    fragColor = vec4(v_color.rgb, v_color.a * coverage);
}
