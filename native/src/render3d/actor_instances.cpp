#include "granadad/render3d/actor_instances.hpp"

#include "granadad/render/session.hpp"

namespace granadad::render3d {

std::vector<ActorInstance> actorInstances(const render::Session& session,
                                          const render::Camera& view) {
    // STUB -- see the header. The A lane reads session.people() and
    // session.tavern() here, with wardSprites' own interpolation.
    (void)session;
    (void)view;
    return {};
}

}  // namespace granadad::render3d
