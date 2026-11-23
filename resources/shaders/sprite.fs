in vec2 TexCoords;

out vec4 FragColor;

struct Material
{
    sampler2D diffuse;
};

uniform Material uMaterial;
uniform vec4 uTint;
// 0..1: mixes the colour toward white (hit flash).
uniform float uFlash;
// 0..1: global brightness, used for fades.
uniform float uBright;
// Window pixels per game pixel, so the translucency dither is laid out in game pixels.
uniform float uPixelScale;

// 4x4 ordered dither threshold. The opaque pass has no blending, so translucency
// (shadows, fades, overlays) is drawn as a retro screen-door pattern.
float Bayer4(vec2 p)
{
    int x = int(mod(p.x, 4.0));
    int y = int(mod(p.y, 4.0));
    int index = x + y * 4;
    float m[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0,
                            3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
    return (m[index] + 0.5) / 16.0;
}

void main()
{
    vec4 color = texture(uMaterial.diffuse, TexCoords);
    if (color.a < 0.5)
    {
        discard;
    }
    color *= uTint;
    if (color.a < 0.999 && color.a <= Bayer4(floor(gl_FragCoord.xy / uPixelScale)))
    {
        discard;
    }
    vec3 rgb = mix(color.rgb, vec3(1.0), uFlash) * uBright;
    FragColor = vec4(rgb, 1.0);
}
