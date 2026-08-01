#ifndef PLANET_LOD_COMMON_GLSL
#define PLANET_LOD_COMMON_GLSL

#include "planet_utils.glsl"
#include "planet_lod_node.glsl"
#include "planet_lod_counters.glsl"

// Mirror of vp::LodRequestType.
#define LOD_REQ_MESH     0u
#define LOD_REQ_CHILDREN 1u
#define LOD_REQ_MERGE    2u

layout (set = 0, binding = 0, std140) uniform PlanetLodUBO {
    mat4 viewProj;

    vec3 cameraWorldPos;
    float planetRadius;

    vec2 viewportSize;
    float subdivisionThreshold;
    float maxRenderDistance;

    float mergeThreshold;

    // Queue capacities, owned by the C++ side. See PlanetLodUBO in PlanetLodTraverser.h for why
    // they are not #defines here
    uint maxRenderEntries;
    uint maxRequestEntries;

    float _pad0;
} ubo;

layout (set = 1, binding = 0, std430) buffer NodeBuffer {
    LodNode nodes[];
};

layout (set = 1, binding = 1, std430) buffer CounterBuffer {
    uint counters[];
};

layout (set = 1, binding = 2, std430) writeonly buffer RequestBuffer {
    uvec4 requests[];
};

layout (set = 1, binding = 3, std430) writeonly buffer RenderQueueBuffer {
    uint renderQueue[];
};

layout (set = 1, binding = 4, std430) readonly buffer InQueueBuffer {
    uint inQueue[];
};

layout (set = 1, binding = 5, std430) writeonly buffer OutQueueBuffer {
    uint outQueue[];
};

/**
 * World space position of one corner of a node
 *
 * Flat terrain phase: a single cube face is treated as a plain grid, with no cube to sphere
 * mapping at all. Node coordinates map straight to world axes, u to x, alt to y, v to z, scaled
 * by the node size at its level
 *
 * When curvature comes back, this is the one function to change, together with the matching one
 * in the vertex shader. planet__local_to_world() in planet_utils.glsl is the drop in equiangular
 * version. Keeping both sides on a single shared function is what prevents the traversal from
 * culling against positions the vertex shader does not actually draw
 *
 * @param level LOD level of the node
 * @param coord Node coordinate, x = u, y = v, z = alt, in node units at that level
 * @param cornerIndex 0 to 7, bit 0 selects u, bit 1 selects v, bit 2 selects alt
 */
vec3 lod_node_corner(int level, ivec3 coord, uint cornerIndex) {
    float nodeSize = CHUNK_SIZE_F * float(1 << level);

    vec3 corner = vec3(
    float(coord.x + int(cornerIndex & 1u)),
    float(coord.z + int((cornerIndex >> 2u) & 1u)),
    float(coord.y + int((cornerIndex >> 1u) & 1u))
    );

    return corner * nodeSize;
}

void lod_extract_frustum_planes(mat4 vp, out vec4 planes[6]) {
    planes[0] = vec4(vp[0][3] + vp[0][0], vp[1][3] + vp[1][0], vp[2][3] + vp[2][0], vp[3][3] + vp[3][0]); // left
    planes[1] = vec4(vp[0][3] - vp[0][0], vp[1][3] - vp[1][0], vp[2][3] - vp[2][0], vp[3][3] - vp[3][0]); // right
    planes[2] = vec4(vp[0][3] + vp[0][1], vp[1][3] + vp[1][1], vp[2][3] + vp[2][1], vp[3][3] + vp[3][1]); // bottom
    planes[3] = vec4(vp[0][3] - vp[0][1], vp[1][3] - vp[1][1], vp[2][3] - vp[2][1], vp[3][3] - vp[3][1]); // top
    planes[4] = vec4(vp[0][3] + vp[0][2], vp[1][3] + vp[1][2], vp[2][3] + vp[2][2], vp[3][3] + vp[3][2]); // near
    planes[5] = vec4(vp[0][3] - vp[0][2], vp[1][3] - vp[1][2], vp[2][3] - vp[2][2], vp[3][3] - vp[3][2]); // far
}

bool lod_aabb_visible(vec3 boundsMin, vec3 boundsMax, vec4 planes[6]) {
    for (int i = 0; i < 6; i++) {
        vec3 n = planes[i].xyz;
        vec3 positive = vec3(
        n.x >= 0.0 ? boundsMax.x : boundsMin.x,
        n.y >= 0.0 ? boundsMax.y : boundsMin.y,
        n.z >= 0.0 ? boundsMax.z : boundsMin.z
        );
        if (dot(n, positive) + planes[i].w < 0.0) return false;
    }
    return true;
}

/**
 * Append a node index to the render queue. Silently drops the node when the queue is full,
 * which only costs one frame of popping rather than corrupting the draw
 */
void lod_push_render(uint nodeIndex) {
    uint slot = atomicAdd(counters[LOD_COUNTER_RENDER], 1u);
    if (slot < ubo.maxRenderEntries) renderQueue[slot] = nodeIndex;
}

/**
 * Ask the CPU for the data this node is missing.
 *
 * The node is claimed first, so a request is emitted once and not on every frame for as long as
 * the job takes. If the queue turns out to be full the claim is rolled back: leaving the flag
 * set would mean the CPU never hears about this node, and it would stay coarse forever.
 */
void lod_emit_request(uint nodeIndex, uint type, uint priority) {
    uint previous = atomicOr(nodes[nodeIndex].x, NODE_REQUESTED_BIT);
    if ((previous & NODE_REQUESTED_BIT) != 0u) return;

    uint slot = atomicAdd(counters[LOD_COUNTER_REQUEST], 1u);
    if (slot >= ubo.maxRequestEntries) {
        atomicAnd(nodes[nodeIndex].x, ~NODE_REQUESTED_BIT);
        atomicAdd(counters[LOD_COUNTER_REQUEST_OVERFLOW], 1u);
        return;
    }

    requests[slot] = uvec4(nodeIndex, type, priority, 0u);
}

#endif