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
uniform vec2 InputSize;
in vec2 vTexCoord;
out vec4 FragColor;

void main() {
    vec4 c = texture(Texture, vTexCoord);
    vec2 cell = fract(vTexCoord * InputSize);
    float line = min(smoothstep(0.04, 0.16, cell.x), smoothstep(0.04, 0.16, cell.y));
    float edge = min(line, min(smoothstep(0.84, 0.96, cell.x), smoothstep(0.84, 0.96, cell.y)));
    vec3 color = c.rgb * mix(0.72, 1.05, edge);
    FragColor = vec4(clamp(color, 0.0, 1.0), c.a);
}
#endif
