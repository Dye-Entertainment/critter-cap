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
void main()
{
    vec4 color = texture(uMaterial.diffuse, TexCoords);
    if (color.a < 0.5)
    {
        discard;
    }
    color *= uTint;
    vec3 rgb = mix(color.rgb, vec3(1.0), uFlash) * uBright;
    // Alpha only matters for blend materials (translucent tints); opaque ones ignore it.
    FragColor = vec4(rgb, color.a);
}
