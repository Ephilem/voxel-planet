#pragma once

#include <flecs.h>
#include <string>

class IDebugPanel {
public:
    virtual ~IDebugPanel() = default;

    /**
     * Render the debug panel. Called every frame when the panel is open.
     * Can use the provided ECS reference to query for debug information or modify debug components.
     * @param ecs Reference to the ECS world, can be used to query for debug information or modify debug components.
     */
    virtual void render(flecs::world& ecs) = 0;

    virtual const std::string name() const = 0;
    virtual const std::string category() const = 0;

    bool is_visible = true;
};
