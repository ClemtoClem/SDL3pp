#version 450

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in float in_layer;
layout(location = 3) in float in_light;

layout(set = 1, binding = 0) uniform UBO {
    mat4 mvp;
};

layout(location = 0) out vec2 frag_uv;
layout(location = 1) out flat float frag_layer;
layout(location = 2) out float frag_light;

void main() {
    gl_Position = mvp * vec4(in_pos, 1.0);
    frag_uv    = in_uv;
    frag_layer = in_layer;
    frag_light = in_light;
}
