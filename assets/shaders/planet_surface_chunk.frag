#version 450

layout(location = 0) flat in uint inFace;
layout(location = 1) flat in uint inTextureSlot;
layout(location = 2) in vec2 inUv;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform texture2DArray textures;
layout(set = 1, binding = 1) uniform sampler s;

void main() {
    outColor = texture(sampler2DArray(textures, s), vec3(inUv, float(inTextureSlot)));
}
