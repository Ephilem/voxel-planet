#version 450

layout(location = 0) flat in uint inFace;
layout(location = 1) flat in uint inTextureSlot;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec3 inNormal;
layout(location = 4) in float inDistance;
layout(location = 5) in vec3 inPos;
layout(location = 6) flat in vec3 inUp;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform texture2DArray textures;
layout(set = 1, binding = 1) uniform sampler s;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;

    float startFade;
    float endFade;
} pc;

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
    // vec4 k = vec4(1.0, 0.5, 0.0, 1.0);
    // float lighting = FACE_SHADE[inFace];
    vec3 N = normalize(inNormal); // per vertex normal, interpolated
    vec3 sunDir = normalize(inUp + vec3(0.4, 0.3, 0.0));   // soleil bidon, incliné
    float lighting = 0.35 + 0.65 * max(dot(N, sunDir), 0.0);

    if (inDistance > pc.startFade) {
        discard;
    }

    outColor = vec4(k.rgb * lighting, k.a);
}
