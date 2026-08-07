#version 450

layout(location = 0) in vec2 inGridUV;
layout(location = 1) in float inLevel;

layout(location = 0) out vec4 outColor;

vec3 level_color(float level) {
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
    return COLORS[int(clamp(level, 0.0, 7.0))];
}

void main() {
    vec2 cell = floor(inGridUV * 8.0);
    float checker = mod(cell.x + cell.y, 2.0);

    vec3 color = level_color(inLevel) * mix(0.55, 1.0, checker);
    outColor = vec4(color, 1.0);
}
