#version 450

layout(location = 0) in vec2 inGridUV;
layout(location = 1) flat in uint inLevel;
layout(location = 2) in float inHeight;

layout(location = 0) out vec4 outColor;

vec3 level_color(uint level) {
    const vec3 COLORS[8] = vec3[](
        vec3(0.35, 0.35, 0.40),
        vec3(0.20, 0.45, 0.95),
        vec3(0.15, 0.80, 0.85),
        vec3(0.20, 0.90, 0.35),
        vec3(0.75, 0.95, 0.20),
        vec3(1.00, 0.85, 0.15),
        vec3(1.00, 0.55, 0.10),
        vec3(1.00, 0.25, 0.15)
    );
    return COLORS[min(level, 7u)];
}

void main() {
    vec2 cell = floor(inGridUV * 8.0);
    float checker = mod(cell.x + cell.y, 2.0);

    float h = clamp(inHeight / 4000.0 * 0.5 + 0.5, 0.0, 1.0);

    float shade = mix(0.35, 1.25, h);

    vec3 base = inHeight < 0.0
        ? vec3(0.10, 0.25, 0.55)                       // sous le niveau 0
        : level_color(inLevel);

    vec3 color = base * mix(0.55, 1.0, checker) * shade;
    outColor = vec4(color, 1.0);
}
