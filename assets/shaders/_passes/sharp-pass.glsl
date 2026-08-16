// Original Mega Man X Recomp shader preset. Public domain / CC0-1.0.

#ifdef VERTEX
in vec2 VertexCoord;
in vec2 TexCoord;
out vec2 vTexCoord;

void main() {
    vTexCoord = TexCoord;
    gl_Position = vec4(VertexCoord * 2.0 - 1.0, 0.0, 1.0);
}
#endif

#ifdef FRAGMENT
uniform sampler2D Texture;
uniform vec2 TextureSize;
in vec2 vTexCoord;
out vec4 FragColor;

void main() {
    vec2 texel = 1.0 / TextureSize;
    vec4 c = texture(Texture, vTexCoord);
    vec4 blur = (
        texture(Texture, vTexCoord + vec2(texel.x, 0.0)) +
        texture(Texture, vTexCoord - vec2(texel.x, 0.0)) +
        texture(Texture, vTexCoord + vec2(0.0, texel.y)) +
        texture(Texture, vTexCoord - vec2(0.0, texel.y))
    ) * 0.25;
    FragColor = vec4(clamp(c.rgb + (c.rgb - blur.rgb) * 0.35, 0.0, 1.0), c.a);
}
#endif
