#version 330 core

// DEM overlay, drawn flat in Mercator like every other 2D layer (roadmap E9). The pseudo-3D
// projection this used to carry — pitch, yaw, focal length, vertical exaggeration — went with
// the rest of that path; elevation now only colours the surface (terrain_height.frag), and
// relief in three dimensions is the osgEarth view's job.

layout(location = 0) in vec2 a_mercator;
layout(location = 1) in vec2 a_texCoord;

uniform vec2 u_viewportSize;
uniform vec2 u_centerMercator;
uniform float u_pixelsPerMercator;
uniform float u_worldOffset;      // horizontal wrap: repeat of the world, in Mercator units
uniform sampler2D u_heightTexture;
uniform float u_minHeight;
uniform float u_maxHeight;
uniform int u_projection;         // 0 Mercator, 1 orthographic globe
uniform vec2 u_centerLatLon;      // globe: radians
uniform float u_radius;           // globe: px

out float v_heightMix;
out float v_heightMeters;
out float v_facing;

void main()
{
    float height = texture(u_heightTexture, a_texCoord).r;
    float heightRange = max(u_maxHeight - u_minHeight, 1.0);
    v_heightMix = clamp((height - u_minHeight) / heightRange, 0.0, 1.0);
    v_heightMeters = height;

    vec2 mercator = vec2(a_mercator.x + u_worldOffset, a_mercator.y);
    vec2 screen = vec2(
        u_viewportSize.x * 0.5 + (mercator.x - u_centerMercator.x) * u_pixelsPerMercator,
        u_viewportSize.y * 0.5 - (mercator.y - u_centerMercator.y) * u_pixelsPerMercator);
    v_facing = 1.0;
    if (u_projection == 1) {
        float lat = atan(sinh(mercator.y));
        float dLon = mercator.x - u_centerLatLon.y;
        float sinLat = sin(lat);
        float cosLat = cos(lat);
        float sin0 = sin(u_centerLatLon.x);
        float cos0 = cos(u_centerLatLon.x);
        float cosDLon = cos(dLon);
        screen = vec2(
            u_viewportSize.x * 0.5 + cosLat * sin(dLon) * u_radius,
            u_viewportSize.y * 0.5 - (cos0 * sinLat - sin0 * cosLat * cosDLon) * u_radius);
        v_facing = sin0 * sinLat + cos0 * cosLat * cosDLon;
    }

    vec2 ndc = vec2(
        screen.x * (2.0 / u_viewportSize.x) - 1.0,
        1.0 - screen.y * (2.0 / u_viewportSize.y));

    gl_Position = vec4(ndc, 0.0, 1.0);
}
