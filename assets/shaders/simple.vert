#version 450

layout(set = 0, binding = 0) uniform global_uniform_object {
    mat4 view;
    mat4 projection;
    float time;
} global_ubo;

void main() {
    const vec2 vertices[3] = vec2[](
        vec2(0.0, -0.5),
        vec2(0.5, 0.5),
        vec2(-0.5, 0.5)
    );
    gl_Position = global_ubo.projection * global_ubo.view * vec4(vertices[gl_VertexIndex], 0.0, 1.0);
}