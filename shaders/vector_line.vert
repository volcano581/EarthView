#version 330 core

// Thick vector-map lines (roads, rivers, boundaries): one instance per segment, expanded here
// into a quad around it in pixels, so widths stay in pixels. vector_line.frag rounds the ends
// (smooth joins), antialiases the edges and cuts dashes. In the globe texture (a Mercator image)
// widths and dashes are scaled so they come out in screen pixels once on the globe.

layout(location = 0) in vec2 a_corner;     // x: 0 at the start, 1 at the end; y: -1 / +1 side
layout(location = 1) in vec4 a_segment;    // start.xy, end.xy (Mercator)
layout(location = 2) in vec3 a_width;      // half width (px at the tile's zoom), growth per zoom level, distance at start (tile px)
layout(location = 3) in vec2 a_dash;       // dash, gap (px); 0 = solid
layout(location = 4) in vec4 a_color;

uniform vec2 u_viewportSize;
uniform vec2 u_centerMercator;
uniform float u_pixelsPerMercator;
uniform float u_zoomDelta;        // view zoom - the tile's style zoom
uniform float u_tilePixelScale;   // target px per tile px (dash phase)
uniform float u_globeRadius;      // globe texture: the globe's radius on screen (px); 0 on the flat map

out vec4 v_color;
out vec2 v_local;       // along, across (px)
out float v_halfWidth;
out float v_length;
out float v_distance;
out vec2 v_dash;

vec2 projectToPixels(vec2 mercator)
{
    return vec2(
        u_viewportSize.x * 0.5 + (mercator.x - u_centerMercator.x) * u_pixelsPerMercator,
        u_viewportSize.y * 0.5 - (mercator.y - u_centerMercator.y) * u_pixelsPerMercator);
}

void main()
{
    vec2 a = projectToPixels(a_segment.xy);
    vec2 b = projectToPixels(a_segment.zw);
    vec2 direction = b - a;
    float len = length(direction);
    vec2 d = len > 1e-5 ? direction / len : vec2(1.0, 0.0);
    vec2 n = vec2(-d.y, d.x);

    // Target px per screen px: on the globe a Mercator unit at Mercator y is
    // u_globeRadius / cosh(y) screen px.
    float scale = u_globeRadius > 0.0
        ? u_pixelsPerMercator * cosh(0.5 * (a_segment.y + a_segment.w)) / u_globeRadius
        : 1.0;
    float halfWidth = a_width.x * pow(a_width.y, u_zoomDelta) * scale;
    float extent = max(halfWidth, 0.5) + 1.0;
    float along = mix(-extent, len + extent, a_corner.x);
    vec2 pixel = a + d * along + n * (a_corner.y * extent);

    gl_Position = vec4(pixel.x * (2.0 / u_viewportSize.x) - 1.0, 1.0 - pixel.y * (2.0 / u_viewportSize.y), 0.0, 1.0);
    v_local = vec2(along, a_corner.y * extent);
    v_halfWidth = halfWidth;
    v_length = len;
    v_distance = a_width.z * u_tilePixelScale + along;
    v_dash = a_dash * scale;
    v_color = a_color;
}
