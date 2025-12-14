#version 450

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec2 fragUV;
layout(location = 2) flat in uint fragTextureSlot;
layout(location = 3) flat in vec3 fragNormal;

layout(location = 0) out vec4 fragColor;

layout(set = 2, binding = 0) uniform texture2DArray voxelTextures;
layout(set = 2, binding = 1) uniform sampler voxelSampler;

void main() {
    vec3 texCoord = vec3(fragUV, float(fragTextureSlot));
    vec4 texColor = texture(sampler2DArray(voxelTextures, voxelSampler), texCoord);

    vec3 normal = normalize(fragNormal);

    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    float diffuse = max(dot(normal, lightDir), 0.0);
    float ambient = 0.3;
    float lighting = ambient + diffuse * 0.7;

    vec3 finalColor = texColor.rgb;


//    vec3 debugColor = vec3(0.0);
//    if (fragUV.x > 1.0 || fragUV.y > 1.0) {
//        debugColor = vec3(1.0, 0.0, 1.0); // Magenta if out of range
//    } else if (fragUV.x < 0.0 || fragUV.y < 0.0) {
//        debugColor = vec3(0.0, 1.0, 1.0); // Cyan if negative
//    } else {
//        debugColor = vec3(fragUV, 0.0);
//    }
//
//    fragColor = vec4(debugColor, 1.0);

    fragColor = vec4(finalColor, texColor.a);
//    fragColor = vec4(fragUV/2, 0.0, 1.0);

//    fragColor = vec4(debugLocalPos / 16.0, 1.0);
}