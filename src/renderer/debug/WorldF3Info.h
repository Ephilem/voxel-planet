#pragma once

#include "IImGuiDebugModule.h"

class WorldF3Info : public IImGuiDebugModule {
public:
    void register_ecs(flecs::world &ecs) override;
    const char* get_name() const override { return "World F3 Info"; }
    const char* get_category() const override { return "Debug"; }
    bool is_visible() const override { return m_visible; }
    void set_visible(bool visible) override { m_visible = visible; }

private:
    bool m_visible = true;
};
