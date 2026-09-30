#version 450

layout(location = 0) flat in uint inFace;
layout(location = 1) flat in uint inTextureSlot;
layout(location = 2) in vec2 inUv;
layout(location = 3) flat in vec3 inNormal;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform texture2DArray textures;
layout(set = 1, binding = 1) uniform sampler s;

void main() {
    vec4 k= texture(sampler2DArray(textures, s), vec3(inUv, float(inTextureSlot)));
    outColor = vec4(inNormal, 0.0);
}
