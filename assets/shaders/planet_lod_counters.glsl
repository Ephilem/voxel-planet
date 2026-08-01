#ifndef PLANET_LOD_COUNTERS_GLSL
#define PLANET_LOD_COUNTERS_GLSL

// Indices into the shared counters buffer, in uint units.
// Mirror of the LOD_COUNTER_OFFSET_* byte offsets in planet_render_types.h, divided by 4.
#define LOD_COUNTER_REQUEST          0u
#define LOD_COUNTER_REQUEST_OVERFLOW 1u
#define LOD_COUNTER_QUEUE_0          4u
#define LOD_COUNTER_QUEUE_1          8u
#define LOD_COUNTER_RENDER           12u

// Must match PlanetLodGpuBuffers::MAX_REQUESTS and MAX_RENDER.
#define LOD_MAX_REQUESTS 2048u
#define LOD_MAX_RENDER   4096u

#endif
