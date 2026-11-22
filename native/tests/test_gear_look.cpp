// PHASE 9 -- WHAT THE WORN ROWS LOOK LIKE (include/granadad/render3d/gear_look.hpp,
// figure.hpp's kit).
//
// The plan's appearance claim (OBLIVION-PLAN 8.3): a pure function of the
// Kit's worn rows reaches the scene description -- the cap on the head, the
// coat, the blade in hand -- and the scene hash says so. These cases pin the
// function (gearLookOf), the table it reads (model key, then id, then class
// or slot), the key it packs to, the kit mesh the figure wears, and THE
// APPEARANCE HASH: the player's own figure in one loadout is a different
// digest from the same figure in another, the same loadout twice is the same
// digest, and a body that wears nothing of the Kit's -- every body of the
// ward -- adds no byte to the description.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <set>
#include <string>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/scene_types.hpp"
#include "granadad/render/session.hpp"
#include "granadad/render3d/actor_instances.hpp"
#include "granadad/render3d/figure.hpp"
#include "granadad/render3d/gear_look.hpp"
#include "granadad/render3d/scene.hpp"
#include "granadad/sim/brawl.hpp"
#include "granadad/sim/items.hpp"
#include "granadad/sim/tavern.hpp"

using namespace granadad::render3d;
namespace content = granadad::content;
namespace render = granadad::render;
namespace sim = granadad::sim;

namespace {

const sim::ItemRegistry& registry() {
    static const sim::ItemRegistry items = sim::ItemRegistry::load(content::contentDir());
    return items;
}

/// A Kit carrying one of each id and wearing them, in order, through the
/// Kit's own wear rule (so the two-hand rule applies as the sim applies it).
sim::Kit wearing(const sim::ItemRegistry& items, std::initializer_list<const char*> ids) {
    sim::Kit kit(items.size());
    for (const char* id : ids) {
        const std::int32_t index = items.indexOf(id);
        INFO(id);
        REQUIRE(index >= 0);
        kit.add(index, 1);
        REQUIRE(kit.wear(items, index));
    }
    return kit;
}

GearLook lookOf(std::initializer_list<const char*> ids) {
    return gearLookOf(wearing(registry(), ids), registry());
}

std::size_t vertsOnBone(const MeshData& mesh, FigureBone bone) {
    return static_cast<std::size_t>(
        std::count(mesh.bones.begin(), mesh.bones.end(), static_cast<std::uint8_t>(bone)));
}

render::Camera eyeAt(float x, float y, float yaw) {
    render::Camera eye;
    eye.x = x;
    eye.y = y;
    eye.yaw = yaw;
    return eye;
}

}  // namespace

TEST_CASE("gearLookOf is a pure function of the worn rows") {
    const sim::ItemRegistry& items = registry();
    REQUIRE(items.loaded());
    // Bare: nothing, and the only key a bare body has is zero.
    const sim::Kit bare(items.size());
    CHECK(gearLookOf(bare, items).key() == 0U);
    CHECK_FALSE(gearLookOf(bare, items).hasKit());
    // CARRIED IS NOT WORN: a sack full of gear and nothing on.
    sim::Kit carried(items.size());
    for (const char* id : {"wool_cap", "gambeson", "cutlass", "buckler", "gauntlets"}) {
        REQUIRE(items.indexOf(id) >= 0);
        carried.add(items.indexOf(id), 1);
    }
    CHECK(gearLookOf(carried, items).key() == 0U);
    // The same rows worn twice, in two Kits, in two orders: the same look.
    const sim::Kit a = wearing(items, {"wool_cap", "gambeson", "cutlass", "buckler", "gauntlets"});
    const sim::Kit b = wearing(items, {"gauntlets", "buckler", "cutlass", "gambeson", "wool_cap"});
    CHECK(gearLookOf(a, items) == gearLookOf(b, items));
    CHECK(gearLookOf(a, items).key() == gearLookOf(a, items).key());
    // And it reads, never writes: the Kit is what it was.
    CHECK(a.worn(sim::ItemSlot::Head) == items.indexOf("wool_cap"));
    // The trinket is never drawn: a purse at the belt adds nothing.
    if (items.indexOf("purse") >= 0) {
        CHECK(lookOf({"purse"}).key() == 0U);
    }
}

TEST_CASE("a cap changes the head gear, and a coat or a jack changes the coat") {
    const ActorLook base = playerFigureLook();
    CHECK(static_cast<HeadGear>(base.headGear) == HeadGear::None);
    // THE CAP. The wool cap is the knit cap on the look, the iron pot the
    // helmet, the hood (an items.json row, no model: by its id) the hood.
    const GearLook cap = lookOf({"wool_cap"});
    CHECK(cap.head == GearHead::WoolCap);
    CHECK(static_cast<HeadGear>(dressedLook(base, cap).headGear) == HeadGear::KnitCap);
    CHECK(dressedLook(base, cap).key() != base.key());
    CHECK_FALSE(cap.hasKit());  // a cap is the look's, not the kit's
    const GearLook pot = lookOf({"iron_pot"});
    CHECK(pot.head == GearHead::IronPot);
    CHECK(static_cast<HeadGear>(dressedLook(base, pot).headGear) == HeadGear::Helmet);
    CHECK(lookOf({"hood"}).head == GearHead::Hood);
    // Taking it off is the bare look again.
    CHECK(dressedLook(base, GearLook{}).key() == base.key());

    // THE COAT. The base raws' coat is the knee coat in the body's own
    // colour; the gambeson and the brigandine are jacks over the torso, in
    // their own cloth, weighted to the torso, standing clear of the chest.
    const GearLook coat = lookOf({"coat"});
    CHECK(coat.body == GearBody::Coat);
    CHECK(static_cast<CoatStyle>(dressedLook(base, coat).coatStyle) == CoatStyle::Coat);
    const GearLook gambeson = lookOf({"gambeson"});
    const GearLook brigandine = lookOf({"brigandine"});
    CHECK(gambeson.body == GearBody::Gambeson);
    CHECK(brigandine.body == GearBody::Brigandine);
    CHECK(gambeson.hasKit());
    const MeshData quilted = buildFigureKit(dressedLook(base, gambeson), gambeson);
    const MeshData riveted = buildFigureKit(dressedLook(base, brigandine), brigandine);
    REQUIRE(quilted.vertexCount() > 0U);
    CHECK(vertsOnBone(quilted, FigureBone::Torso) == quilted.vertexCount());
    CHECK(quilted.colours != riveted.colours);
    CHECK(quilted.id != riveted.id);
    // Clear of the body: the jack's front at the chest stands proud of the
    // body's own front there by more than the depth buffer can lose.
    const MeshData body = buildFigure(dressedLook(base, gambeson));
    float bodyFront = 0.0F;
    float jackFront = 0.0F;
    for (std::size_t v = 0; v < body.vertexCount(); ++v) {
        const float y = body.positions[v * 3 + 1];
        if (body.bones[v] == static_cast<std::uint8_t>(FigureBone::Torso) && y > 1.35F && y < 1.43F) {
            bodyFront = std::min(bodyFront, body.positions[v * 3 + 2]);
        }
    }
    for (std::size_t v = 0; v < quilted.vertexCount(); ++v) {
        const float y = quilted.positions[v * 3 + 1];
        if (y > 1.35F && y < 1.43F) {
            jackFront = std::min(jackFront, quilted.positions[v * 3 + 2]);
        }
    }
    CHECK(bodyFront < -0.05F);
    CHECK(jackFront < bodyFront - 0.02F);
}

TEST_CASE("a cutlass puts a blade in the right hand, and a shield goes on the left arm") {
    const ActorLook base = playerFigureLook();
    // The base raws' cutlass carries no model: it is found by its id.
    const sim::ItemDef* cutlassRow = registry().find("cutlass");
    REQUIRE(cutlassRow != nullptr);
    CHECK(cutlassRow->model.empty());
    const GearLook cutlass = lookOf({"cutlass"});
    CHECK(cutlass.prop == GearProp::Cutlass);
    const MeshData blade = buildFigureKit(base, cutlass);
    REQUIRE(blade.vertexCount() > 0U);
    CHECK(vertsOnBone(blade, FigureBone::HandR) == blade.vertexCount());
    // It leaves the hand forward of the body.
    float front = 0.0F;
    for (std::size_t v = 0; v < blade.vertexCount(); ++v) {
        front = std::min(front, blade.positions[v * 3 + 2]);
    }
    CHECK(front < -0.35F);
    // The phase 9 rows by their model, the old ones by their id.
    CHECK(lookOf({"hatchet"}).prop == GearProp::Hatchet);
    CHECK(lookOf({"maul"}).prop == GearProp::Maul);
    CHECK(lookOf({"boarding_axe"}).prop == GearProp::BoardingAxe);
    CHECK(lookOf({"mace"}).prop == GearProp::Mace);
    CHECK(lookOf({"knife"}).prop == GearProp::Knife);
    CHECK(lookOf({"cudgel"}).prop == GearProp::Cudgel);
    CHECK(gearPropTwoHanded(GearProp::Maul));
    CHECK_FALSE(gearPropTwoHanded(GearProp::Cutlass));
    // A two-hander's head rests by the foot, over the floor, not under it.
    const MeshData maul = buildFigureKit(base, lookOf({"maul"}));
    float lowest = 10.0F;
    for (std::size_t v = 0; v < maul.vertexCount(); ++v) {
        lowest = std::min(lowest, maul.positions[v * 3 + 1]);
    }
    CHECK(lowest > 0.02F);
    CHECK(lowest < 0.3F);

    // THE SHIELD. A buckler in the left fist, a round shield strapped to
    // the left forearm, both on the left of the body, face out.
    const GearLook buckler = lookOf({"buckler"});
    const GearLook round = lookOf({"round_shield"});
    CHECK(buckler.shield == GearShield::Buckler);
    CHECK(round.shield == GearShield::RoundShield);
    const MeshData small = buildFigureKit(base, buckler);
    const MeshData big = buildFigureKit(base, round);
    CHECK(vertsOnBone(small, FigureBone::HandL) == small.vertexCount());
    CHECK(vertsOnBone(big, FigureBone::ForearmL) == big.vertexCount());
    const auto rest = figureRestJoints(base);
    const float shoulder = rest[static_cast<std::size_t>(FigureBone::UpperArmL)].x;
    for (std::size_t v = 0; v < big.vertexCount(); ++v) {
        CHECK(big.positions[v * 3] < shoulder - 0.05F);
    }
    // A shield and a blade together: both, one mesh.
    const GearLook both = lookOf({"cutlass", "round_shield"});
    CHECK(both.prop == GearProp::Cutlass);
    CHECK(both.shield == GearShield::RoundShield);
    // The two-hand rule is the sim's, and the look follows it: a maul put
    // in the hand bares the shield.
    const GearLook maulAfterShield = lookOf({"round_shield", "maul"});
    CHECK(maulAfterShield.prop == GearProp::Maul);
    CHECK(maulAfterShield.shield == GearShield::None);
    // Gloves and boots.
    CHECK(lookOf({"work_gloves"}).hands == GearHands::WorkGloves);
    CHECK(lookOf({"gauntlets"}).hands == GearHands::Gauntlets);
    CHECK(lookOf({"boots"}).feet == GearFeet::Boots);
    const MeshData gloves = buildFigureKit(base, lookOf({"gauntlets"}));
    CHECK(vertsOnBone(gloves, FigureBone::HandL) > 0U);
    CHECK(vertsOnBone(gloves, FigureBone::HandR) > 0U);
    const MeshData boots = buildFigureKit(base, lookOf({"boots"}));
    CHECK(vertsOnBone(boots, FigureBone::FootL) > 0U);
    CHECK(vertsOnBone(boots, FigureBone::ShinR) > 0U);
}

TEST_CASE("the table falls back by class and by slot, and the Gull's own hold their weapon's prop") {
    // A row the table has never heard of is still drawn as a thing of its
    // kind: a blunt hand row is a cudgel, an off-hand row a round shield, a
    // head row a cap, a back row a coat.
    sim::ItemDef stranger;
    stranger.id = "belaying_pin";
    stranger.slot = sim::ItemSlot::Hand;
    stranger.weaponClass = sim::Weapon::Blunt;
    CHECK(gearLookOfItem(&stranger).prop == GearProp::Cudgel);
    stranger.weaponClass = sim::Weapon::Edged;
    CHECK(gearLookOfItem(&stranger).prop == GearProp::Knife);
    // A model on the wrong slot is not obeyed: a "cutlass" worn on the head
    // is a cap, not a blade through somebody's skull.
    sim::ItemDef odd;
    odd.id = "odd";
    odd.model = "cutlass";
    odd.slot = sim::ItemSlot::Head;
    CHECK(gearLookOfItem(&odd).head == GearHead::Cap);
    CHECK(gearLookOfItem(&odd).prop == GearProp::None);
    odd.slot = sim::ItemSlot::OffHand;
    CHECK(gearLookOfItem(&odd).shield == GearShield::RoundShield);
    odd.slot = sim::ItemSlot::Body;
    CHECK(gearLookOfItem(&odd).body == GearBody::Coat);
    odd.slot = sim::ItemSlot::Trinket;
    CHECK(gearLookOfItem(&odd).key() == 0U);
    CHECK(gearLookOfItem(nullptr).key() == 0U);
    CHECK(gearPropOfModel("maul") == GearProp::Maul);
    CHECK(gearPropOfModel("buckler") == GearProp::None);
    // The room's Weapon byte: the bouncer's Blunt is a cudgel.
    CHECK(gearPropOfWeapon(sim::Weapon::Fists) == GearProp::None);
    CHECK(gearPropOfWeapon(sim::Weapon::Blunt) == GearProp::Cudgel);
    CHECK(gearPropOfWeapon(sim::Weapon::Evictor) == GearProp::Evictor);
    CHECK(gearPropOfWeapon(sim::Weapon::Improvised) == GearProp::Bottle);
    CHECK(gearPropOfWeapon(sim::Weapon::Edged) == GearProp::Knife);
    for (std::size_t p = 0; p < kGearPropCount; ++p) {
        CHECK_FALSE(gearPropName(static_cast<GearProp>(p)).empty());
    }
}

TEST_CASE("the key is lossless over the fields, and unique over the authored set") {
    // Every combination of every field packs and unpacks to itself, and no
    // two pack to one key.
    std::set<std::uint32_t> keys;
    std::size_t combos = 0;
    for (std::uint32_t prop = 0; prop < kGearPropCount; ++prop) {
        for (std::uint32_t shield = 0; shield < 3; ++shield) {
            for (std::uint32_t head = 0; head < 5; ++head) {
                for (std::uint32_t body = 0; body < 4; ++body) {
                    for (std::uint32_t hands = 0; hands < 3; ++hands) {
                        for (std::uint32_t feet = 0; feet < 3; ++feet) {
                            GearLook g;
                            g.prop = static_cast<GearProp>(prop);
                            g.shield = static_cast<GearShield>(shield);
                            g.head = static_cast<GearHead>(head);
                            g.body = static_cast<GearBody>(body);
                            g.hands = static_cast<GearHands>(hands);
                            g.feet = static_cast<GearFeet>(feet);
                            const GearLook back = GearLook::fromKey(g.key());
                            CHECK(back.prop == g.prop);
                            CHECK(back.shield == g.shield);
                            CHECK(back.head == g.head);
                            CHECK(back.body == g.body);
                            CHECK(back.hands == g.hands);
                            CHECK(back.feet == g.feet);
                            CHECK(g.key() < (1U << kGearKeyBits));
                            keys.insert(g.key());
                            ++combos;
                        }
                    }
                }
            }
        }
    }
    CHECK(keys.size() == combos);
    CHECK(GearLook{}.key() == 0U);
    // THE AUTHORED SET: every row the raws let a body wear somewhere that is
    // seen is drawn as something, and no two rows are drawn as one thing.
    std::set<std::uint32_t> drawn;
    std::size_t worn = 0;
    for (const sim::ItemDef& item : registry().items()) {
        if (item.slot == sim::ItemSlot::None || item.slot == sim::ItemSlot::Trinket || item.fixed) {
            continue;
        }
        INFO(item.id);
        const GearLook g = gearLookOfItem(&item);
        CHECK(g.key() != 0U);
        CHECK(drawn.insert(g.key()).second);
        ++worn;
    }
    CHECK(worn >= 15U);  // items.json's worn rows and items_gear.json's eleven
}

TEST_CASE("the kit mesh is rigged to the body's skeleton, in its own id range, unique per shape and gear") {
    const ActorLook base = playerFigureLook();
    const GearLook heavy = lookOf({"iron_pot", "brigandine", "gauntlets", "boots", "boarding_axe"});
    const ActorLook dressed = dressedLook(base, heavy);
    const MeshData kit = buildFigureKit(dressed, heavy);
    CHECK(kit.id == figureKitMeshId(dressed, heavy));
    CHECK(isFigureKitMeshId(kit.id));
    CHECK_FALSE(isFigureMeshId(kit.id));
    CHECK(kit.id >= kFigureKitMeshIdBase);
    CHECK(kit.id < kFigureMeshIdBase);
    CHECK(kit.id > kViewmodelShieldMeshIdBase + kViewmodelShieldMeshIdCount);
    CHECK(kit.version == 1U);
    REQUIRE(kit.vertexCount() > 0U);
    CHECK(kit.vertexCount() < 65535U);
    CHECK(kit.triangleCount() <= kFigureKitTriangleBudget);
    CHECK(kit.bones.size() == kit.vertexCount());
    CHECK(kit.normals.size() == kit.positions.size());
    CHECK(kit.colours.size() == kit.vertexCount() * 4U);
    REQUIRE(kit.boneRest.size() == (kFigureBoneCount + 1U) * 3U);
    for (const std::uint8_t bone : kit.bones) {
        CHECK(bone < kFigureBoneCount);
    }
    for (const std::uint16_t index : kit.indices) {
        CHECK(index < kit.vertexCount());
    }
    // The body's own rest joints: it skins with the body's pose.
    const auto joints = figureRestJoints(dressed);
    for (std::size_t j = 0; j < joints.size(); ++j) {
        CHECK(kit.boneRest[j * 3] == joints[j].x);
        CHECK(kit.boneRest[j * 3 + 1] == joints[j].y);
    }
    // The rest pose skins to the rest mesh.
    std::vector<float> posed;
    std::vector<float> normals;
    skinFigure(kit, figureRestPose(joints.data()), posed, normals);
    REQUIRE(posed.size() == kit.positions.size());
    for (std::size_t i = 0; i < posed.size(); ++i) {
        CHECK(std::fabs(posed[i] - kit.positions[i]) < 1e-4F);
    }
    // The far kit: fewer triangles, no rig, its own id.
    const MeshData far = buildFigureKit(dressed, heavy, true);
    CHECK(far.id == figureKitMeshId(dressed, heavy, true));
    CHECK(far.id != kit.id);
    CHECK(far.bones.empty());
    CHECK(far.triangleCount() < kit.triangleCount());
    // Deterministic to the byte.
    const MeshData again = buildFigureKit(dressed, heavy);
    CHECK(again.positions == kit.positions);
    CHECK(again.colours == kit.colours);
    CHECK(again.indices == kit.indices);
    // Another shape is another mesh; a bare gear is no mesh at all.
    ActorLook slight = dressed;
    slight.build = 0;
    CHECK(figureKitMeshId(slight, heavy) != kit.id);
    CHECK(buildFigureKit(slight, heavy).positions != kit.positions);
    CHECK(buildFigureKit(dressed, GearLook{}).vertexCount() == 0U);
    SceneDescription scene;
    putFigureKit(scene, dressed, GearLook{});
    CHECK(scene.meshes.empty());
    putFigureKit(scene, dressed, heavy);
    putFigureKit(scene, dressed, heavy);  // idempotent
    CHECK(scene.meshes.size() == 1U);
}

TEST_CASE("THE APPEARANCE HASH: the player's figure hashes by what he wears, and no gear adds no byte") {
    const render::Camera eye = eyeAt(150.5F, 60.5F, 0.0F);
    const auto described = [&eye](const GearLook& gear) {
        SceneDescription scene;
        scene.actors.push_back(playerFigureInstance(eye, 0.0F, gear));
        putActorFigures(scene, scene.actors);
        return scene;
    };
    const GearLook light = lookOf({"wool_cap", "gambeson", "cutlass"});
    const GearLook heavy = lookOf({"iron_pot", "brigandine", "gauntlets", "maul"});
    // LOADOUT A IS NOT LOADOUT B.
    const SceneDescription a = described(light);
    const SceneDescription b = described(heavy);
    CHECK(sceneHash(a) != sceneHash(b));
    // THE SAME LOADOUT TWICE IS THE SAME DIGEST, off two Kits built apart.
    CHECK(sceneHash(described(lookOf({"cutlass", "gambeson", "wool_cap"}))) == sceneHash(a));
    // One piece more is a different picture, and so is one piece less.
    CHECK(sceneHash(described(lookOf({"wool_cap", "gambeson", "cutlass", "buckler"}))) != sceneHash(a));
    CHECK(sceneHash(described(lookOf({"wool_cap", "gambeson"}))) != sceneHash(a));
    // The figure stands in front of the eye and faces it, in the idle.
    const ActorInstance& figure = a.actors[0];
    CHECK(figure.instance.position.z == doctest::Approx(toScene(eye.x, eye.y, 0.0F).z - 2.5F));
    CHECK(figure.instance.position.x == doctest::Approx(eye.x));
    CHECK(figure.instance.yaw == doctest::Approx(eye.yaw + 3.14159265F));
    CHECK(figure.clip == ActorClip::Idle);
    CHECK(figure.skinned);
    CHECK(figure.rig == kActorRigProcedural);
    CHECK(actorRigFile(figure.rig).empty());
    CHECK(figure.gearKey == light.key());
    CHECK(figure.lookKey == dressedLook(playerFigureLook(), light).key());
    // Its meshes: the dressed body and the kit, both in the description.
    CHECK(a.findMesh(figure.instance.meshId) != nullptr);
    CHECK(a.findMesh(figureKitMeshId(ActorLook::fromKey(figure.lookKey), light)) != nullptr);
    CHECK(a.meshes.size() == 2U);

    // NO GEAR, NO BYTE. A bare figure carries a zero key and no kit mesh, so
    // the description is the body's alone -- the bytes a phase 6 body was.
    const SceneDescription bare = described(GearLook{});
    CHECK(bare.actors[0].gearKey == 0U);
    CHECK(bare.actors[0].lookKey == playerFigureLook().key());
    CHECK(bare.meshes.size() == 1U);
    // The key is mixed only when it is there: set it and the digest moves,
    // clear it and the digest is the bare one again to the byte.
    SceneDescription toggled = bare;
    toggled.actors[0].gearKey = light.key();
    CHECK(sceneHash(toggled) != sceneHash(bare));
    toggled.actors[0].gearKey = 0U;
    CHECK(sceneHash(toggled) == sceneHash(bare));
    // A cap alone is the look's: no kit mesh, but a different body.
    const SceneDescription capped = described(lookOf({"wool_cap"}));
    CHECK(capped.meshes.size() == 1U);
    CHECK(sceneHash(capped) != sceneHash(bare));
}

TEST_CASE("the ward wears nothing of the Kit's, and the Gull's own hold what the sim says they hold") {
    render::SessionConfig config;
    config.width = 160;
    config.height = 90;
    config.timeOfDay = 20 * 3600;
    config.timeOfDayGiven = true;
    render::Session session(config);
    const render::Camera eye = session.camera();
    std::vector<ActorInstance> actors = actorInstances(session, eye);
    // Every geared body is one of the Gull's own, holding its Weapon's prop.
    std::size_t armed = 0;
    for (const sim::Actor& actor : session.tavern().actors()) {
        if (!actor.present() || gearPropOfWeapon(actor.weapon()) == GearProp::None) {
            continue;
        }
        const float dx = static_cast<float>(actor.x()) / 256.0F - eye.x;
        const float dy = static_cast<float>(actor.y()) / 256.0F - eye.y;
        if (std::sqrt(dx * dx + dy * dy) <= 64.0F) {
            ++armed;
        }
    }
    std::size_t geared = 0;
    for (const ActorInstance& body : actors) {
        if (body.gearKey == 0U) {
            continue;
        }
        ++geared;
        const GearLook gear = GearLook::fromKey(body.gearKey);
        CHECK(gear.prop != GearProp::None);
        CHECK(gear.shield == GearShield::None);
        CHECK(gear.head == GearHead::None);
        CHECK(gear.body == GearBody::None);
    }
    CHECK(geared == armed);
    MESSAGE("armed bodies of the Gull in range: " << armed);
    // The description puts a kit per geared body and nothing for the rest.
    SceneDescription scene;
    putActorFigures(scene, actors);
    std::size_t kits = 0;
    for (const MeshData& mesh : scene.meshes) {
        kits += isFigureKitMeshId(mesh.id) ? 1U : 0U;
    }
    CHECK((kits > 0U) == (geared > 0U));
    CHECK(kits <= geared);
    // The same session described twice: the same bytes.
    SceneDescription twice;
    twice.actors = actorInstances(session, eye);
    putActorFigures(twice, twice.actors);
    scene.actors = actors;
    CHECK(sceneHash(scene) == sceneHash(twice));
}
