#version 450

layout(location = 0) in vec2 fragScreenUV;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform sky_ubo {
    mat4 inverseView;
    mat4 inverseProjection;
    float time;
} ubo;

// Reconstruct the world-space ray direction for this pixel
vec3 compute_ray_direction(vec2 screenUV) {
    vec4 ndc = vec4(screenUV * 2.0 - 1.0, 1.0, 1.0);

    vec4 viewDir = ubo.inverseProjection * ndc;
    viewDir.w = 0.0;

    // Rotate from view space to world space (no translation)
    vec3 worldDir = normalize((ubo.inverseView * viewDir).xyz);
    return worldDir;
}

void main() {
    vec3 ray = compute_ray_direction(fragScreenUV);

    // y component: -1 = nadir (down), 0 = horizon, 1 = zenith (up)
    float t = ray.y * 0.5 + 0.5; // remap to [0, 1]
    float horizon = smoothstep(0.0, 0.15, t); // soft horizon blend

    // Sky gradient: horizon color -> zenith color
    vec3 horizonColor = vec3(0.65, 0.82, 1.0);
    vec3 zenithColor  = vec3(0.15, 0.40, 0.85);
    vec3 sky = mix(horizonColor, zenithColor, horizon);

    // Sun
    vec3 sunDir = normalize(vec3(0.5, 1.0, 0.3)); // matches light in simple.frag
    float sunDot = max(dot(ray, sunDir), 0.0);
    float sunDisc = smoothstep(0.9997, 1.0, sunDot);          // sharp disc
    float sunGlow = pow(sunDot, 16.0) * 0.3;                  // wide soft glow
    vec3 sunColor = vec3(1.0, 0.95, 0.8);
    sky += sunColor * (sunDisc + sunGlow);

    fragColor = vec4(sky, 1.0);
}
