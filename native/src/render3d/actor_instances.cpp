#include "granadad/render3d/actor_instances.hpp"

#include <algorithm>
#include <cmath>

#include "granadad/render/lighting.hpp"
#include "granadad/render/session.hpp"
#include "granadad/render/vertical.hpp"
#include "granadad/render/world_renderer.hpp"
#include "granadad/sim/human_scale.hpp"

namespace granadad::render3d {

namespace {

constexpr float kPi = 3.14159265358979323846F;
/// The reference person: kStandingHeightTilesQ8 (480 Q8) in tiles, the same
/// height figureScaleOf's default row carries.
constexpr float kPersonHeightTiles = 1.875F;

/// A placeholder's coat, trousers and skin per rig: the sprite sheet's own
/// reading of each kind (a helmet-blue Watch, white-and-red clergy, grey
/// thieves, brown beasts), so a box crowd already sorts by silhouette AND
/// colour at eight tiles. Nothing here is art; it is what stands in for it.
struct Palette {
    Rgba8 coat;
    Rgba8 legs;
    Rgba8 skin;
};

[[nodiscard]] Palette paletteOf(sim::WardType type) noexcept {
    switch (type) {
        case sim::WardType::Shopkeeper: return {{96, 64, 40, 255}, {60, 48, 40, 255}, {214, 170, 130, 255}};
        case sim::WardType::Sailor: return {{52, 72, 110, 255}, {70, 60, 50, 255}, {200, 160, 120, 255}};
        case sim::WardType::Fisher: return {{70, 90, 80, 255}, {60, 60, 50, 255}, {200, 160, 120, 255}};
        case sim::WardType::Carter: return {{110, 84, 50, 255}, {70, 56, 40, 255}, {208, 165, 125, 255}};
        case sim::WardType::MilitiaWatch: return {{40, 60, 130, 255}, {50, 50, 60, 255}, {200, 160, 120, 255}};
        case sim::WardType::Wastrel: return {{80, 76, 66, 255}, {64, 60, 54, 255}, {190, 150, 115, 255}};
        case sim::WardType::Urchin: return {{120, 90, 60, 255}, {80, 66, 50, 255}, {210, 168, 128, 255}};
        case sim::WardType::Thief: return {{72, 72, 78, 255}, {56, 56, 60, 255}, {180, 145, 110, 255}};
        case sim::WardType::PriestOfTheFlame: return {{226, 222, 210, 255}, {170, 40, 40, 255}, {214, 170, 130, 255}};
        case sim::WardType::DiscipleOfTheFlame: return {{220, 216, 204, 255}, {150, 60, 50, 255}, {214, 170, 130, 255}};
        case sim::WardType::AnimalKeeper: return {{100, 90, 60, 255}, {70, 60, 44, 255}, {204, 162, 122, 255}};
        case sim::WardType::Dog: return {{120, 92, 60, 255}, {96, 72, 48, 255}, {130, 100, 66, 255}};
        case sim::WardType::Stray: return {{104, 96, 84, 255}, {80, 74, 64, 255}, {110, 100, 88, 255}};
        case sim::WardType::Cat: return {{70, 66, 62, 255}, {60, 56, 52, 255}, {76, 72, 68, 255}};
        case sim::WardType::Mouse: return {{120, 112, 100, 255}, {110, 100, 90, 255}, {150, 130, 120, 255}};
        case sim::WardType::Serf:
        default: return {{104, 84, 60, 255}, {66, 56, 46, 255}, {204, 162, 122, 255}};
    }
}

/// Per-face shade of the placeholder: a fixed key light from above and a
/// little from the east, baked into the vertex colours (rlsw has no
/// shader). The light where the body STANDS goes into Instance::tint.
[[nodiscard]] Rgba8 shade(const Rgba8& base, float factor) noexcept {
    const auto ch = [factor](std::uint8_t c) {
        return static_cast<std::uint8_t>(std::clamp(static_cast<float>(c) * factor, 0.0F, 255.0F));
    };
    return Rgba8{ch(base.r), ch(base.g), ch(base.b), base.a};
}

void pushVertex(MeshData& mesh, const Vec3& p, const Rgba8& c) {
    mesh.positions.push_back(p.x);
    mesh.positions.push_back(p.y);
    mesh.positions.push_back(p.z);
    mesh.texcoords.push_back(0.0F);
    mesh.texcoords.push_back(0.0F);
    mesh.colours.push_back(c.r);
    mesh.colours.push_back(c.g);
    mesh.colours.push_back(c.b);
    mesh.colours.push_back(c.a);
}

/// A quad from four corners counter-clockwise as seen from outside.
void pushQuad(MeshData& mesh, const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d,
              const Rgba8& colour) {
    const auto base = static_cast<std::uint16_t>(mesh.vertexCount());
    pushVertex(mesh, a, colour);
    pushVertex(mesh, b, colour);
    pushVertex(mesh, c, colour);
    pushVertex(mesh, d, colour);
    mesh.indices.push_back(base);
    mesh.indices.push_back(static_cast<std::uint16_t>(base + 1));
    mesh.indices.push_back(static_cast<std::uint16_t>(base + 2));
    mesh.indices.push_back(base);
    mesh.indices.push_back(static_cast<std::uint16_t>(base + 2));
    mesh.indices.push_back(static_cast<std::uint16_t>(base + 3));
}

/// A closed axis-aligned box [x0,x1] x [y0,y1] x [z0,z1], six faces, the
/// starter cube's own winding, shaded top 1.0 / north-south 0.85 / east-west
/// 0.7 / bottom 0.45.
void pushBox(MeshData& mesh, float x0, float y0, float z0, float x1, float y1, float z1,
             const Rgba8& base) {
    const Vec3 p000{x0, y0, z0}, p100{x1, y0, z0}, p010{x0, y1, z0}, p110{x1, y1, z0};
    const Vec3 p001{x0, y0, z1}, p101{x1, y0, z1}, p011{x0, y1, z1}, p111{x1, y1, z1};
    pushQuad(mesh, p011, p111, p110, p010, shade(base, 1.0F));   // +Y top
    pushQuad(mesh, p000, p100, p101, p001, shade(base, 0.45F));  // -Y bottom
    pushQuad(mesh, p001, p101, p111, p011, shade(base, 0.85F));  // +Z south
    pushQuad(mesh, p100, p000, p010, p110, shade(base, 0.85F));  // -Z north
    pushQuad(mesh, p101, p100, p110, p111, shade(base, 0.72F));  // +X east
    pushQuad(mesh, p000, p001, p011, p010, shade(base, 0.62F));  // -X west
}

/// The light where a body stands, the way the sprite paths compute it:
/// ambient + max(baked, dynamic), clamped near 1 so a figure in a lamp pool
/// is lit rather than blown out. Folded into a tint (255 = 1.0).
[[nodiscard]] Rgba8 tintFor(const render::SkyState& sky, const render::Rgb& baked,
                            const render::Rgb& dynamic) noexcept {
    const auto ch = [](float ambient, float b, float d) {
        const float light = std::min(1.15F, ambient + std::max(b, d));
        return static_cast<std::uint8_t>(std::clamp(light * 255.0F, 0.0F, 255.0F));
    };
    return Rgba8{ch(sky.ambient.r, baked.r, dynamic.r), ch(sky.ambient.g, baked.g, dynamic.g),
                 ch(sky.ambient.b, baked.b, dynamic.b), 255};
}

[[nodiscard]] float yawOf(sim::Angle facing) noexcept {
    // BAM 0 is north, increasing clockwise (sim/angle.hpp) -- the scene's
    // own convention, so this is a scale and nothing else.
    return static_cast<float>(facing) * (2.0F * kPi / 65536.0F);
}

[[nodiscard]] float planarDistance(const render::Camera& view, float px, float py) noexcept {
    const float dx = px - view.x;
    const float dy = py - view.y;
    return std::sqrt(dx * dx + dy * dy);
}

}  // namespace

std::string_view actorRigFile(std::uint8_t rig) noexcept {
    if (rig >= kActorRigCount) {
        return {};
    }
    // The variant looks first: no WardType has these values.
    if (rig == kActorRigTownswoman) {
        // The Generic peasant woman: the other half of the working ward.
        return "townswoman.glb";
    }
    switch (static_cast<sim::WardType>(rig)) {
        // The Knights soldier: helmet and blue. The bouncer borrows it too.
        // (knight.glb is exported and unmapped: the sim has no Watch rank.)
        case sim::WardType::MilitiaWatch:
            return "watchman.glb";
        // The FantasyHero preset: the working quay's own build.
        case sim::WardType::Sailor:
        case sim::WardType::Fisher:
        case sim::WardType::Carter:
            return "dockhand.glb";
        // The Generic prisoner: rags for the ward's poor and its thieves.
        case sim::WardType::Wastrel:
        case sim::WardType::Thief:
            return "wastrel.glb";
        // The Generic peasant: everybody else who walks on two legs. The
        // clergy too, until a robed preset is exported (none is).
        case sim::WardType::Serf:
        case sim::WardType::Shopkeeper:
        case sim::WardType::Urchin:
        case sim::WardType::PriestOfTheFlame:
        case sim::WardType::DiscipleOfTheFlame:
        case sim::WardType::AnimalKeeper:
            return "townsman.glb";
        // No beast rig has been exported; the placeholder stands.
        case sim::WardType::Dog:
        case sim::WardType::Stray:
        case sim::WardType::Cat:
        case sim::WardType::Mouse:
        default:
            return {};
    }
}

namespace {

/// The women's and the men's given names of content/raws/names/names.json
/// (the serf, shopkeeper, wastrel and disciple pools, as anybody reads
/// them), the Gull's roster and the Forty of notables.json, plus the titles
/// a notable carries that say it for them. Sorted once, searched by word.
/// A pool name that reads neither way (Wenn, Joss, the animal keepers'
/// Drover and Fodder, most wastrel nicknames) is on neither list.
constexpr std::string_view kWomensWords[] = {
    "Aldis",    "Annis",    "Berta",    "Bettrys",  "Birdie",   "Bodil",     "Brigga",   "Ceffa",
    "Cressida", "Dagny",    "Dessa",    "Ditta",    "Ebba",     "Edda",      "Elsba",    "Elspet",
    "Emmeline", "Frieda",   "Gerta",    "Grandmother", "Grette", "Haddie",   "Hedda",    "Herdis",
    "Hestia",   "Ilsa",     "Inka",     "Isolde",   "Jessa",    "Jocosa",    "Katrin",   "Kessa",
    "Lavinia",  "Lisbet",   "Mabet",    "Mag",      "Maren",    "Marta",     "Mirabel",  "Mistress",
    "Moll",     "Mother",   "Nedda",    "Nessa",    "Odalys",   "Odda",      "Odile",    "Onna",
    "Pernilla", "Petra",    "Quenna",   "Redda",    "Rilla",    "Rosamund",  "Sabeth",   "Salla",
    "Sanna",    "Sella",    "Sethra",   "Sigga",    "Tama",     "Ulla",      "Ulrika",   "Unna",
    "Ursel",    "Ursuline", "Vanna",    "Vespera",  "Vetta",    "Wendeline", "Widow",    "Willa",
    "Winnifred", "Withy",   "Yette",    "Yseult",   "Ysolt",
};
constexpr std::string_view kMensWords[] = {
    "Adric",    "Aldous",   "Ambrus",   "Ansgar",   "Arno",     "Askel",     "Askold",   "Baker",
    "Benedar",  "Berthold", "Bondsman", "Brakk",    "Bram",     "Brann",     "Bront",    "Calixt",
    "Captain",  "Carter",   "Casker",   "Caspar",   "Cathal",   "Cobb",      "Colm",     "Cooper",
    "Cort",     "Corvin",   "Crell",    "Cull",     "Cutter",   "Dain",      "Delvin",   "Demetrian",
    "Dormund",  "Dovric",   "Dray",     "Elior",    "Evrard",   "Ewald",     "Falk",     "Farold",
    "Father",   "Fenner",   "Fenwick",  "Ferrin",   "Finch",    "Fintan",    "Flint",    "Folke",
    "Foreman",  "Garrin",   "Godric",   "Goodman",  "Gorm",     "Gregor",    "Grieve",   "Haldan",
    "Halvor",   "Harl",     "Hask",     "Hobb",     "Hobbin",   "Hyacinth",  "Ingmar",   "Innocens",
    "Ivo",      "Jarrick",  "Jek",      "Jerome",   "Jorun",    "Jorvath",   "Kettil",   "Kled",
    "Kort",     "Kyrill",   "Leofric",  "Lom",      "Luff",     "Lunt",      "Mace",     "Malachy",
    "Marek",    "Master",   "Merle",    "Mordo",    "Neddry",   "Nils",      "Norrick",  "Norvin",
    "Ondrey",   "Orin",     "Osric",    "Oswin",    "Ottavan",  "Ottmar",    "Ox",       "Perrin",
    "Pettar",   "Piet",     "Pike",     "Quarrel",  "Quintus",  "Ragvald",   "Ranulf",   "Rasp",
    "Rolf",     "Sergeant", "Slavik",   "Squall",   "Stannic",  "Sten",      "Tancred",  "Tarl",
    "Tarn",     "Theodric", "Tobbin",   "Tolley",   "Tolliver", "Torvald",   "Ulf",      "Ulfric",
    "Ulwer",    "Valdo",    "Varn",     "Varric",   "Venn",     "Vess",      "Vetch",    "Vidar",
    "Vinzenz",  "Watchman", "Wick",     "Wilm",     "Wold",     "Wull",      "Yarrow",   "Yohan",
    "Yorrel",
};

[[nodiscard]] bool listed(const std::string_view* words, std::size_t count,
                          std::string_view word) noexcept {
    // Sorted in the source; a binary search per word.
    std::size_t lo = 0;
    std::size_t hi = count;
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        if (words[mid] < word) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo < count && words[lo] == word;
}

}  // namespace

int actorNameSays(std::string_view name) noexcept {
    std::size_t at = 0;
    while (at < name.size()) {
        std::size_t end = name.find(' ', at);
        if (end == std::string_view::npos) {
            end = name.size();
        }
        const std::string_view word = name.substr(at, end - at);
        if (!word.empty()) {
            if (listed(kWomensWords, sizeof(kWomensWords) / sizeof(kWomensWords[0]), word)) {
                return 1;
            }
            if (listed(kMensWords, sizeof(kMensWords) / sizeof(kMensWords[0]), word)) {
                return -1;
            }
        }
        at = end + 1;
    }
    return 0;
}

std::uint8_t actorRigFor(sim::WardType type, std::int32_t actorId, std::string_view name) noexcept {
    if (!actorKindSplits(type)) {
        return actorRigOf(type);
    }
    const int says = actorNameSays(name);
    if (says > 0) {
        return kActorRigTownswoman;
    }
    if (says < 0) {
        return actorRigOf(type);
    }
    return actorRigFor(type, actorId);
}

float actorInstanceScale(sim::WardType type) noexcept {
    if (!sim::isPerson(type)) {
        return 1.0F;
    }
    return render::figureScaleOf(type).heightTiles / kPersonHeightTiles;
}

std::string_view actorClipName(ActorClip clip) noexcept {
    switch (clip) {
        case ActorClip::Idle: return "idle";
        case ActorClip::Walk: return "walk";
        case ActorClip::PunchLeft: return "punch_l";
        case ActorClip::PunchRight: return "punch_r";
        case ActorClip::Block: return "block";
        case ActorClip::Hit: return "hit";
        case ActorClip::Recover: return "recover";
        case ActorClip::Death: return "death";
    }
    return "idle";
}

ActorClip clipForActivity(sim::Activity activity, std::int32_t swingSeq) noexcept {
    switch (activity) {
        case sim::Activity::Walking:
            return ActorClip::Walk;
        case sim::Activity::Brawling:
        case sim::Activity::Ejecting:
            return (swingSeq & 1) == 0 ? ActorClip::PunchRight : ActorClip::PunchLeft;
        case sim::Activity::Downed:
            return ActorClip::Recover;
        case sim::Activity::Dead:
            return ActorClip::Death;
        case sim::Activity::Away:
        case sim::Activity::Working:
        case sim::Activity::Drinking:
        case sim::Activity::Watching:
        case sim::Activity::Warning:
        default:
            return ActorClip::Idle;
    }
}

MeshData buildActorPlaceholder(std::uint8_t kind) {
    MeshData mesh;
    mesh.id = actorRigMeshId(kind);
    mesh.version = 1;
    const auto type = static_cast<sim::WardType>(kind < kActorKindCount ? kind : 0);
    const Palette palette = paletteOf(type);
    const render::FigureScale figure = render::figureScaleOf(type);
    if (sim::isPerson(type)) {
        // The reference person, 1.875 tall; the instance scales it to the
        // type. Width is the figure table's shoulder width at that height.
        const float h = kPersonHeightTiles;
        const float w = figure.widthTiles * (kPersonHeightTiles / figure.heightTiles);
        const float legTop = h * 0.47F;
        const float torsoTop = h * 0.82F;
        const float headTop = h;
        const float depth = w * 0.5F;
        // Legs: two, so a walker has a gap of daylight between them.
        const float legW = w * 0.22F;
        pushBox(mesh, -w * 0.28F - legW * 0.5F, 0.0F, -depth * 0.4F, -w * 0.28F + legW * 0.5F,
                legTop, depth * 0.4F, palette.legs);
        pushBox(mesh, w * 0.28F - legW * 0.5F, 0.0F, -depth * 0.4F, w * 0.28F + legW * 0.5F,
                legTop, depth * 0.4F, palette.legs);
        // Torso.
        pushBox(mesh, -w * 0.5F, legTop, -depth * 0.5F, w * 0.5F, torsoTop, depth * 0.5F,
                palette.coat);
        // Head.
        const float headW = w * 0.42F;
        pushBox(mesh, -headW * 0.5F, torsoTop, -headW * 0.5F, headW * 0.5F, headTop,
                headW * 0.5F, palette.skin);
        // The nose: a block on the -Z face of the head. This is the facing
        // marker -- what a case checks sits in FRONT of the body.
        const float noseW = headW * 0.3F;
        pushBox(mesh, -noseW * 0.5F, torsoTop + (headTop - torsoTop) * 0.35F,
                -headW * 0.5F - noseW * 0.8F, noseW * 0.5F,
                torsoTop + (headTop - torsoTop) * 0.6F, -headW * 0.5F, shade(palette.skin, 0.8F));
    } else {
        // A beast: a body long along Z with the head forward (-Z), at its
        // own size (no glb to scale against).
        const float h = figure.heightTiles;
        const float length = figure.widthTiles;
        const float w = length * 0.36F;
        const float legTop = h * 0.4F;
        const float legW = w * 0.3F;
        for (const float sx : {-1.0F, 1.0F}) {
            for (const float sz : {-1.0F, 1.0F}) {
                const float cx = sx * (w * 0.5F - legW * 0.5F);
                const float cz = sz * (length * 0.32F);
                pushBox(mesh, cx - legW * 0.5F, 0.0F, cz - legW * 0.5F, cx + legW * 0.5F, legTop,
                        cz + legW * 0.5F, palette.legs);
            }
        }
        pushBox(mesh, -w * 0.5F, legTop, -length * 0.42F, w * 0.5F, h * 0.85F, length * 0.42F,
                palette.coat);
        const float headW = w * 0.8F;
        pushBox(mesh, -headW * 0.5F, h * 0.55F, -length * 0.5F - headW * 0.6F, headW * 0.5F, h,
                -length * 0.42F + headW * 0.1F, palette.skin);
    }
    return mesh;
}

void putActorRigs(SceneDescription& scene) {
    for (std::uint32_t kind = 0; kind < kActorKindCount; ++kind) {
        const MeshData* present = scene.findMesh(actorRigMeshId(kind));
        if (present != nullptr && present->version == 1) {
            continue;
        }
        scene.putMesh(buildActorPlaceholder(static_cast<std::uint8_t>(kind)));
    }
}

std::vector<ActorInstance> actorInstances(const render::Session& session,
                                          const render::Camera& view,
                                          const ActorSceneParams& params) {
    std::vector<ActorInstance> out;
    const render::SkyState sky = render::skyAt(session.timeOfDay());
    const render::LampGlow& glow = session.renderer().glow();
    const std::int64_t stepCount = session.body().stepCount();

    // --- the ward ----------------------------------------------------------
    //
    // WHERE BETWEEN THE TWO TILES: the same slide wardSprites computes, off
    // the same counter, so the 2D and the 3D figure stand on the same spot.
    const float slide = static_cast<float>(session.stepsThisSecond()) /
                        static_cast<float>(sim::kStepsPerSecond);
    const sim::WardPopulation& people = session.people();
    out.reserve(people.actors().size() / 4 + 32);
    for (const sim::WardActor& actor : people.actors()) {
        // STREET SENSES leg (b): a person struck down on the brawl floor, or
        // slain, is off the board (visible false) and DRAWN where he fell --
        // floored(), the one other predicate wardSprites reads -- with the
        // Gull's own clips for it: Recover on the floor, Death for a corpse.
        // A caught mouse is neither and stays undrawn.
        const bool down = actor.floored();
        if (!actor.visible() && !down) {
            continue;
        }
        // A floored body lies where it fell: no slide off the tile it was
        // walking from when the blow landed.
        const float px = down ? static_cast<float>(actor.x) + 0.5F
                              : static_cast<float>(actor.prevX) +
                                    static_cast<float>(actor.x - actor.prevX) * slide + 0.5F;
        const float py = down ? static_cast<float>(actor.y) + 0.5F
                              : static_cast<float>(actor.prevY) +
                                    static_cast<float>(actor.y - actor.prevY) * slide + 0.5F;
        const float distance = planarDistance(view, px, py);
        if (distance > params.maxDistance) {
            continue;
        }
        ActorInstance body;
        // The look by kind, name and id; the placeholder by kind alone.
        body.rig = actorRigFor(actor.type, actor.id, people.identity(actor.id).name);
        body.instance.meshId = actorRigMeshId(actorRigOf(actor.type));
        body.instance.textureId = 0;
        body.instance.position = toScene(px, py, render::bandSurface(actor.band));
        body.instance.yaw = yawOf(actor.facing);
        body.instance.scale = actorInstanceScale(actor.type);
        body.instance.tint = tintFor(sky, glow.at(actor.x, actor.y, actor.band), render::Rgb{});
        body.clip = actor.slain ? ActorClip::Death
                    : down     ? ActorClip::Recover
                    : actor.dead ? ActorClip::Death
                                 : wardClip(actor.x != actor.prevX || actor.y != actor.prevY);
        body.clipFrame = actorClipFrame(stepCount, actor.id);
        body.skinned = distance <= params.skinDistance;
        out.push_back(body);
    }

    // --- the Gilded Gull -----------------------------------------------------
    const std::vector<render::Lamp> live = session.tavernLights();
    for (const sim::Actor& actor : session.tavern().actors()) {
        if (!actor.present()) {
            continue;
        }
        // Sub-tile Q8 straight out of the simulation (actor.hpp: the position
        // between two tiles is the sim's, not the renderer's).
        const float px = static_cast<float>(actor.x()) / 256.0F;
        const float py = static_cast<float>(actor.y()) / 256.0F;
        const float distance = planarDistance(view, px, py);
        if (distance > params.maxDistance) {
            continue;
        }
        const sim::WardType type = render::figureForRole(actor.role(), actor.id());
        ActorInstance body;
        // The Gull's roster: named, so the name's say first, the id draw
        // after (Gerta a woman, Tarn a man, whatever their ids draw).
        body.rig = actorRigFor(type, actor.id(), actor.name());
        body.instance.meshId = actorRigMeshId(actorRigOf(type));
        body.instance.textureId = 0;
        body.instance.position = toScene(px, py, render::bandSurface(actor.band()));
        body.instance.yaw = yawOf(actor.facing());
        body.instance.scale = actorInstanceScale(type);
        body.instance.tint =
            tintFor(sky, glow.at(actor.tileX(), actor.tileY(), actor.band()),
                    render::dynamicGlowAt(live, actor.tileX(), actor.tileY(), actor.band()));
        body.clip = clipForActivity(actor.activity(), actor.npcSwingSeq());
        body.clipFrame = actorClipFrame(stepCount, actor.id());
        body.skinned = distance <= params.skinDistance;
        out.push_back(body);
    }
    return out;
}

}  // namespace granadad::render3d
