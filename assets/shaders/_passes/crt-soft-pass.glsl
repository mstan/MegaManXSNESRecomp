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
    float y = fract(vTexCoord.y * InputSize.y);
    float scan = mix(0.72, 1.08, smoothstep(0.18, 0.50, y) * (1.0 - smoothstep(0.55, 0.92, y)));
    vec2 triad = fract(vTexCoord * InputSize * vec2(0.75, 1.0));
    float mask = mix(0.92, 1.04, step(triad.x, 0.33));
    vec3 warm = c.rgb * vec3(1.05, 1.00, 0.94);
    FragColor = vec4(clamp(warm * scan * mask, 0.0, 1.0), c.a);
}
#endif
