#version 450

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec2 fragUV;
layout(location = 2) flat in uint fragTextureSlot;
layout(location = 3) flat in vec3 fragNormal;
layout(location = 4) flat in vec3 debugFragLocalPos;

layout(location = 0) out vec4 fragColor;

layout(set = 3, binding = 0) uniform texture2DArray voxelTextures;
layout(set = 3, binding = 1) uniform sampler voxelSampler;

void main() {
    vec3 texCoord = vec3(fragUV, float(fragTextureSlot));
    vec4 texColor = texture(sampler2DArray(voxelTextures, voxelSampler), texCoord);

    vec3 normal = normalize(fragNormal);

    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    float diffuse = max(dot(normal, lightDir), 0.0);
    float ambient = 0.3;
    float lighting = ambient + diffuse * 0.7;

    vec3 finalColor = texColor.rgb * lighting;

    // debug: if localpos is a the edge of the chunk, tint the color red
//    if (debugFragLocalPos.x == 0.0 || debugFragLocalPos.x == 15.0 ||
//        debugFragLocalPos.y == 0.0 || debugFragLocalPos.y == 15.0 ||
//        debugFragLocalPos.z == 0.0 || debugFragLocalPos.z == 15.0) {
//        finalColor = vec3(1.0, 0.0, 0.0);
//    }

    fragColor = vec4(finalColor, texColor.a);

    // fragcolor depend of the normal direction
//    fragColor = vec4(normal * 0.5 + 0.5, 1.0);
}