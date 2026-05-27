#ifndef RIDUX_COMPOSITOR_HPP
#define RIDUX_COMPOSITOR_HPP

#include "injury_shell.hpp"

#include <stdint.h>

namespace riduxcompositor {

struct SceneNode {
    const injury::Surface *surface = nullptr;
    float opacity = 1.0f;
    uint32_t z = 0;
};

class SceneGraph {
public:
    static constexpr int MAX_NODES = 64;

    void clear() {
        count_ = 0;
    }

    bool push(const injury::Surface *surface, float opacity, uint32_t z) {
        if (!surface || !surface->visible || count_ >= MAX_NODES) return false;
        nodes_[count_].surface = surface;
        nodes_[count_].opacity = opacity;
        nodes_[count_].z = z;
        ++count_;
        return true;
    }

    int count() const { return count_; }

    const SceneNode &node(int index) const {
        return nodes_[index];
    }

private:
    SceneNode nodes_[MAX_NODES];
    int count_ = 0;
};

struct InputRoute {
    uint32_t target_surface = 0;
    bool pointer_grab = false;
};

} // namespace riduxcompositor

#endif
