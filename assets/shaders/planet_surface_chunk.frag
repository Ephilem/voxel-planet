#version 450

layout(location = 0) flat in uint inFace;
layout(location = 0) out vec4 outColor;

const vec3 kFaceColors[6] = vec3[](
    vec3(0.62, 0.45, 0.30), vec3(0.50, 0.36, 0.24),
    vec3(0.56, 0.40, 0.27), vec3(0.44, 0.32, 0.21),
    vec3(0.40, 0.65, 0.30), vec3(0.30, 0.22, 0.15)
);

void main() {
    outColor = vec4(kFaceColors[min(inFace, 5u)], 1.0);
}
