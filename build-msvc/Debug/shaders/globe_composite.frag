#version 330 core

// For each screen pixel on the orthographic globe: the point of the sphere it shows, then
// where that point lies in the globe texture (a Mercator image of the visible part of the
// world). Offsets from the view centre are computed directly, so it stays exact when zoomed in.

uniform sampler2D u_texture;    // premultiplied colour
uniform vec2 u_globeCenter;     // screen px
uniform float u_radius;         // globe radius, px
uniform vec2 u_centerLatLon;    // radians
uniform vec4 u_textureExtent;   // Mercator extent drawn, relative to the view centre: x0, y0, x1, y1
uniform vec2 u_texScale;        // part of the texture drawn into (it is reused while it fits)
uniform vec4 u_outsideColor;    // beyond the Mercator limit (85 deg): premultiplied

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
    float dY = atanh(dSin / (1.0 - sinLat * sin0));

    vec2 texCoord = vec2(
        (dLon - u_textureExtent.x) / (u_textureExtent.z - u_textureExtent.x),
        (dY - u_textureExtent.y) / (u_textureExtent.w - u_textureExtent.y));
    if (texCoord.x < 0.0 || texCoord.x > 1.0 || texCoord.y < 0.0 || texCoord.y > 1.0) {
        fragColor = u_outsideColor * limb;
        return;
    }
    fragColor = texture(u_texture, texCoord * u_texScale) * limb;
}
