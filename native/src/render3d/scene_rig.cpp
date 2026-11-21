#include "granadad/render3d/scene_rig.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/session.hpp"
#include "granadad/render3d/actor_instances.hpp"
#include "granadad/render3d/ground_items.hpp"
#include "granadad/render3d/light_model.hpp"
#include "granadad/render3d/viewmodel.hpp"

namespace granadad::render3d {

namespace sim = granadad::sim;

std::string rigModelDir() {
    return (content::contentDir() / "art" / "lot-3d" / "characters").string();
}

std::string weaponModelDir() {
    return (content::contentDir() / "art" / "lot-3d" / "static").string();
}

std::string staticModelDir() { return weaponModelDir(); }

namespace {

/// PHASE 6, VERIFICATION ONLY (--sheet=): a row of figures standing in front
/// of the eye, facing it, in place of the ward's people. `mode` 1 is one
/// figure per animation clip, each caught on the frame that shows it best;
/// 2 is one per kind of body, alternately a man and a woman, standing.
std::vector<ActorInstance> figure_sheet(const render::Session& session, int mode) {
    std::vector<ActorInstance> out;
    const render::Camera cam = session.camera();
    const float fx = std::sin(cam.yaw);
    const float fy = -std::cos(cam.yaw);
    const float rx = std::cos(cam.yaw);
    const float ry = std::sin(cam.yaw);
    const float up = render::bandSurface(session.body().band());
    struct Pose {
        ActorClip clip;
        std::uint32_t frame;
    };
    const Pose clips[] = {{ActorClip::Idle, 30},       {ActorClip::Walk, 13},
                          {ActorClip::PunchRight, 17}, {ActorClip::PunchLeft, 17},
                          {ActorClip::Block, 10},      {ActorClip::Talk, 30},
                          {ActorClip::Hit, 6},         {ActorClip::Recover, 9999},
                          {ActorClip::Death, 9999}};
    const sim::WardType kinds[] = {
        sim::WardType::Serf,   sim::WardType::Shopkeeper,        sim::WardType::Sailor,
        sim::WardType::Fisher, sim::WardType::MilitiaWatch,      sim::WardType::Wastrel,
        sim::WardType::Urchin, sim::WardType::Thief,             sim::WardType::PriestOfTheFlame,
        sim::WardType::DiscipleOfTheFlame, sim::WardType::AnimalKeeper, sim::WardType::Carter};
    const int count = mode == 1 ? 9 : 12;
    for (int i = 0; i < count; ++i) {
        const float across = (static_cast<float>(i) - static_cast<float>(count - 1) * 0.5F) * 0.8F;
        const float depth = mode == 1 ? 4.4F : 5.0F;
        const float px = cam.x + fx * depth + rx * across;
        const float py = cam.y + fy * depth + ry * across;
        ActorInstance body;
        sim::WardType type = mode == 1 ? sim::WardType::Sailor : kinds[i];
        const std::int32_t id = mode == 1 ? 21 : 40 + i * 7;
        const bool female = mode == 1 ? false : (i % 2 == 1);
        const ActorLook look = actorLookFor(type, id, female);
        body.lookKey = look.key();
        body.instance.meshId = figureMeshId(look);
        body.instance.position = toScene(px, py, up);
        body.instance.yaw = cam.yaw + 3.14159265F;
        body.instance.scale = actorInstanceScale(type) * actorHeightJitter(id);
        body.instance.tint = Rgba8{255, 255, 255, 255};
        body.skinned = true;
        body.clip = mode == 1 ? clips[i].clip : ActorClip::Idle;
        body.clipFrame = mode == 1 ? clips[i].frame : 30U;
        out.push_back(body);
    }
    return out;
}

}  // namespace

void SceneRig::refresh(const render::Session& session, float aspect) {
    if (world == nullptr) {
        catalogue = StaticCatalogue::load(
            staticCataloguePath(content::contentDir()));
        if (!catalogue.error().empty()) {
            std::printf("granadad: render3d: piece catalogue: %s -- the chunk mesh stands\n",
                        catalogue.error().c_str());
        }
        for (const std::string& warning : catalogue.warnings()) {
            std::printf("granadad: render3d: piece catalogue: %s\n", warning.c_str());
        }
        world = std::make_unique<WorldScene>(
            session.tiles(), session.atlas(), &session.glow(), &catalogue,
            &session.lamps());
    }
    WorldSceneParams params;
    params.timeOfDaySeconds = session.timeOfDay();
    // WEATHER: the session's sky -- live off the seed, the day and the
    // clock, or pinned by --weather. Render-only; the sim never sees it.
    params.weather = session.weather();
    params.dynamicLamps = session.tavernLights();
    params.perPixel = perPixel;
    // The render clock the flames waver by: the body's own step count at
    // sixty a second, so a scripted capture is the same frame twice. It
    // is read by the flicker and by nothing in the sim.
    params.renderClock = static_cast<float>(session.body().stepCount() % 360000) / 60.0F;
    world->refresh(scene, session.camera(), aspect, params);
    // KIT BUILD: the things on the tiles -- the room's hashed ground
    // list, the four strongboxes and the snug's bale -- as Item pieces
    // off the same catalogue, appended after the placed pieces (whose
    // table they index). Per frame, off sim state, never written back:
    // TAKE removes a thing in the sim and the next frame has one fewer.
    {
        const std::vector<StaticInstance> things = groundItemInstances(
            session, catalogue, session.camera(), 40.0F, world->pool(), &world->spill(), perPixel);
        scene.statics.insert(scene.statics.end(), things.begin(), things.end());
        // And a lantern on a table burns like one on a wall: its halo,
        // drawn last with every other (the adapter defers kDrawHalo).
        const std::vector<StaticInstance> glows =
            groundItemHalos(session, catalogue, session.camera(), 40.0F);
        scene.statics.insert(scene.statics.end(), glows.begin(), glows.end());
    }
    // A LANE: the people. Sixteen placeholder rigs put once (the adapter
    // swaps in the glb by name where it has one), and the instances
    // rewritten every frame off the roster with wardSprites' own slide.
    putActorRigs(scene);
    // The bodies stand in the same pool the walls and cobbles wear
    // (WorldScene::pool), so a figure beside a lantern at night is lit
    // by that lantern and not drawn in its daylight coat.
    ActorSceneParams actorParams;
    actorParams.pool = world->pool();
    actorParams.spill = &world->spill();
    actorParams.perPixel = perPixel;
    scene.actors = actorInstances(session, session.camera(), actorParams);
    // PHASE 6: and the figure of every person in sight (one mesh a look).
    if (sheet != 0) {
        scene.actors = figure_sheet(session, sheet);
    }
    putActorFigures(scene, scene.actors);
    // V LANE: the player's own hands. The placeholder parts put once
    // (the adapter swaps in the arms glb by name where it has one), the
    // pose rewritten every frame off Session::viewmodel() -- the machine
    // step() runs beside the sim, so a scripted swing photographs the
    // same frame on every machine.
    putViewmodelMeshes(scene);
    scene.viewmodel = viewmodelInstance(session, handsKind);
    if (perPixel) {
        // The hands are drawn in their own pass, in view space, where the
        // per-pixel light cannot reach: they wear the light at the eye, by
        // the same model (litSurface), clipped at one.
        const Vec3 light = litSurface(
            scene.shading, scene.lights, scene.camera.position, Vec3{0.0F, 0.6F, 0.8F});
        const auto channel = [](float l) {
            return static_cast<std::uint8_t>(std::clamp(l, 0.0F, 1.0F) * 255.0F + 0.5F);
        };
        scene.viewmodel.tint =
            Rgba8{channel(light.x), channel(light.y), channel(light.z), 255};
    }
}

}  // namespace granadad::render3d
