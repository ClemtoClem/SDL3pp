#version 450

layout(location = 0) in vec2  frag_uv;
layout(location = 1) in flat float frag_layer;
layout(location = 2) in float frag_light;

layout(set = 2, binding = 0) uniform sampler2DArray tex_array;

layout(location = 0) out vec4 out_color;

void main() {
    vec4 color = texture(tex_array, vec3(frag_uv, frag_layer));
    if (color.a < 0.1) discard;
    out_color = vec4(color.rgb * frag_light, color.a);
}
