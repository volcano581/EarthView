#version 330 core

// For each screen pixel on the orthographic globe: the point of the sphere it shows, then
// where that point lies in the globe texture. The texture is a Mercator image of the visible
// part of the world in latitude bands, stacked, each at its own scale (VectorTileRenderer::
// renderGlobe()). Offsets from the view centre are computed directly, so it stays exact when
// zoomed in.

const int kMaxBands = 12;

uniform sampler2D u_texture;            // premultiplied colour
uniform vec2 u_globeCenter;             // screen px
uniform float u_radius;                 // globe radius, px
uniform vec2 u_centerLatLon;            // radians
uniform int u_bandCount;
uniform float u_bandTop[kMaxBands];     // Mercator y where each band ends, relative to the view centre
uniform vec4 u_bandExtent[kMaxBands];   // Mercator extent drawn, relative to the view centre: x0, y0, x1, y1
uniform vec4 u_bandRect[kMaxBands];     // where it is in the texture: u0, v0, u1, v1
uniform vec2 u_texelSize;
uniform vec4 u_outsideColor;            // beyond the Mercator limit (85 deg): premultiplied

in vec2 v_screen;

out vec4 fragColor;

float atanh(float x)
{
    return 0.5 * log((1.0 + x) / (1.0 - x));
}

void main()
{
    vec2 o = vec2(v_screen.x - u_globeCenter.x, u_globeCenter.y - v_screen.y) / u_radius;
    float rho = length(o);
    if (rho >= 1.0)
        discard;
    float limb = clamp((1.0 - rho) * u_radius, 0.0, 1.0);

    // Inverse orthographic: longitude and latitude offsets from the centre.
    float sin0 = sin(u_centerLatLon.x);
    float cos0 = cos(u_centerLatLon.x);
    float cosC = sqrt(max(0.0, 1.0 - rho * rho));
    float dLon = atan(o.x, cos0 * cosC - o.y * sin0);
    // sin(lat) - sin(lat0), without cancellation: cosC - 1 = -rho^2 / (1 + cosC).
    float dSin = -rho * rho / (1.0 + cosC) * sin0 + o.y * cos0;
    float sinLat = sin0 + dSin;
    if (abs(sinLat) > 0.99863) {  // beyond 85.05 degrees: no Mercator data
        fragColor = u_outsideColor * limb;
        return;
    }
    // Mercator y offset: atanh(sin lat) - atanh(sin lat0) = atanh(dSin / (1 - sinLat sin0)).
    vec2 p = vec2(dLon, atanh(dSin / (1.0 - sinLat * sin0)));

    int band = u_bandCount - 1;
    for (int i = 0; i < u_bandCount - 1; ++i) {
        if (p.y < u_bandTop[i]) {
            band = i;
            break;
        }
    }
    vec4 extent = u_bandExtent[band];
    vec2 t = (p - extent.xy) / (extent.zw - extent.xy);
    if (t.x < 0.0 || t.x > 1.0 || t.y < 0.0 || t.y > 1.0) {
        fragColor = u_outsideColor * limb;
        return;
    }
    // Filtering stays inside the band.
    vec4 rect = u_bandRect[band];
    vec2 uv = clamp(mix(rect.xy, rect.zw, t), rect.xy + 0.5 * u_texelSize, rect.zw - 0.5 * u_texelSize);
    fragColor = texture(u_texture, uv) * limb;
}
