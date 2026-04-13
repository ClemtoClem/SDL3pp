#version 450

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec4 a_color;

layout(location = 0) out vec4 v_color;

// Uniform buffer at vertex slot 0 (PushVertexUniformData(0, ...))
layout(set = 1, binding = 0) uniform UBO {
    mat4 mvp;
} ubo;

void main() {
    gl_Position = ubo.mvp * vec4(a_pos, 1.0);
    v_color     = a_color;
}
