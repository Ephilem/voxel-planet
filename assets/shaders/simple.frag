#version 450

layout(location = 0) in vec3 fragWorldPos;
layout(location = 0) out vec4 fragColor;

void main() {
    // Generate color based on position for face visualization
    vec3 baseColor = fract(fragWorldPos * 0.5) * 0.5 + 0.3;

    // Add face orientation color (based on which axis is integer-ish)
    vec3 normal = normalize(cross(dFdx(fragWorldPos), dFdy(fragWorldPos)));
    vec3 faceColor = abs(normal);

    // Create grid lines at voxel boundaries
    vec3 grid = abs(fract(fragWorldPos) - 0.5);
    float minGrid = min(min(grid.x, grid.y), grid.z);
    float edgeFactor = smoothstep(0.45, 0.5, minGrid);

    // Mix base color with face orientation color
    vec3 color = mix(faceColor * 0.7, baseColor, 0.5);

    // Add dark edges at voxel boundaries
    color = mix(vec3(0.1), color, edgeFactor);

    fragColor = vec4(color, 1.0);
}