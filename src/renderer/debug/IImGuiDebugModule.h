#pragma once
#include <flecs.h>
#include <string>

class IImGuiDebugModule {
public:
    virtual ~IImGuiDebugModule() = default;

    virtual void register_ecs(flecs::world& ecs) = 0;

    virtual const char* get_name() const = 0;
    virtual const char* get_category() const = 0;

    virtual bool is_visible() const = 0;
    virtual void set_visible(bool visible) = 0;
};
