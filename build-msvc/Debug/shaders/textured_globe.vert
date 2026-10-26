#version 330 core

// A raster tile on the orthographic globe: a grid over the tile (a_uv 0..1) projected here,
// on the GPU. textured_globe.frag drops what is on the far side.

layout(location = 0) in vec2 a_uv;

uniform vec2 u_viewportSize;
uniform vec4 u_tileBounds;      // Mercator: left, top, right, bottom
uniform vec2 u_centerLatLon;    // radians
uniform float u_radius;         // px

out vec2 v_texCoord;
out float v_facing;

void main()
{
    vec2 mercator = vec2(mix(u_tileBounds.x, u_tileBounds.z, a_uv.x), mix(u_tileBounds.y, u_tileBounds.w, a_uv.y));
    float lat = atan(sinh(mercator.y));
    float dLon = mercator.x - u_centerLatLon.y;
    float sinLat = sin(lat);
    float cosLat = cos(lat);
    float sin0 = sin(u_centerLatLon.x);
    float cos0 = cos(u_centerLatLon.x);
    float cosDLon = cos(dLon);
    vec2 xy = vec2(cosLat * sin(dLon), cos0 * sinLat - sin0 * cosLat * cosDLon);
    vec2 pixel = vec2(u_viewportSize.x * 0.5 + xy.x * u_radius, u_viewportSize.y * 0.5 - xy.y * u_radius);

    gl_Position = vec4(pixel.x * (2.0 / u_viewportSize.x) - 1.0, 1.0 - pixel.y * (2.0 / u_viewportSize.y), 0.0, 1.0);
    v_texCoord = a_uv;
    v_facing = sin0 * sinLat + cos0 * cosLat * cosDLon;
}
