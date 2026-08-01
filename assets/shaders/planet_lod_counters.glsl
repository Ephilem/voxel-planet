#ifndef PLANET_LOD_COUNTERS_GLSL
#define PLANET_LOD_COUNTERS_GLSL

// Indices into the shared counters buffer, in uint units.
// Mirror of the LOD_COUNTER_OFFSET_* byte offsets in planet_render_types.h, divided by 4.
#define LOD_COUNTER_REQUEST          0u
#define LOD_COUNTER_REQUEST_OVERFLOW 1u
#define LOD_COUNTER_QUEUE_0          4u
#define LOD_COUNTER_QUEUE_1          8u
#define LOD_COUNTER_RENDER           12u

// The queue capacities used to live here as defines that had to be kept in step with
// PlanetLodGpuBuffers by hand. They drifted, the shader capped the render queue at half what the
// buffer could hold, and everything past the cap was dropped at random every frame.
//
// They now travel in PlanetLodUBO, so there is one definition and it is the C++ one. Shaders with
// no access to that UBO derive the bound from their own dispatch size instead.

#endif
