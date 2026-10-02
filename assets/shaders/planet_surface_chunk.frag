#version 450

layout(location = 0) flat in uint inFace;
layout(location = 1) flat in uint inTextureSlot;
layout(location = 2) in vec2 inUv;
layout(location = 3) flat in vec3 inNormal;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform texture2DArray textures;
layout(set = 1, binding = 1) uniform sampler s;

const float FACE_SHADE[6] = float[6](
    0.80, // X+
    0.62, // X-
    0.88, // Y+
    0.70, // Y-
    1.00, // Z+  dessus
    0.45  // Z-  dessous
);

void main() {
    vec4 k = texture(sampler2DArray(textures, s), vec3(inUv, float(inTextureSlot)));
    float lighting = FACE_SHADE[inFace];
    outColor = vec4(k.rgb * lighting, k.a);
}
