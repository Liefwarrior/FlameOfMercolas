#include "granadad/render3d/figure.hpp"

#include <algorithm>
#include <cmath>

#include "granadad/sim/human_scale.hpp"

namespace granadad::render3d {

namespace {

using shapes::Builder;
using shapes::Mat3;
using shapes::Ring;

constexpr float kPi = 3.14159265358979323846F;
constexpr float kTwoPi = 2.0F * kPi;

[[nodiscard]] constexpr Rgba8 rgb(int r, int g, int b) noexcept {
    return Rgba8{static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b), 255};
}

// ---------------------------------------------------------------------------
// THE TABLES. Muddy and saturated, a 2006 palette; none of it is art, it is
// what the parameter set indexes.
// ---------------------------------------------------------------------------

constexpr Rgba8 kSkin[8] = {rgb(246, 218, 194), rgb(236, 196, 164), rgb(222, 172, 134), rgb(204, 150, 110),
                            rgb(178, 124, 88),  rgb(150, 98, 68),   rgb(116, 76, 54),   rgb(84, 54, 40)};
constexpr Rgba8 kHair[8] = {rgb(24, 20, 20),   rgb(52, 36, 26),   rgb(86, 58, 36),  rgb(112, 52, 30),
                            rgb(190, 160, 92), rgb(150, 70, 32),  rgb(132, 130, 128), rgb(216, 214, 208)};
constexpr Rgba8 kTrousers[4] = {rgb(62, 50, 42), rgb(76, 76, 72), rgb(38, 36, 36), rgb(106, 90, 64)};

/// The coat tables, eight a kind. Civilians share the earth palette; the
/// Watch is blue, the clergy white and cream, the sailors navy and sea grey,
/// the thieves and the poor drab.
constexpr Rgba8 kCivilian[8] = {rgb(112, 82, 54), rgb(130, 64, 46), rgb(90, 94, 58), rgb(72, 86, 106),
                                rgb(152, 120, 58), rgb(98, 98, 94), rgb(64, 90, 72), rgb(100, 60, 78)};
constexpr Rgba8 kWatchBlue[8] = {rgb(40, 60, 130), rgb(36, 54, 118), rgb(48, 68, 140), rgb(34, 50, 104),
                                 rgb(44, 64, 124), rgb(38, 58, 136), rgb(50, 70, 128), rgb(32, 48, 112)};
constexpr Rgba8 kClergy[8] = {rgb(228, 224, 212), rgb(220, 216, 204), rgb(232, 228, 218), rgb(214, 208, 196),
                              rgb(226, 220, 208), rgb(222, 218, 206), rgb(230, 226, 214), rgb(218, 212, 200)};
constexpr Rgba8 kSailor[8] = {rgb(52, 72, 110), rgb(44, 62, 98),  rgb(64, 84, 118), rgb(80, 96, 112),
                              rgb(58, 66, 84),  rgb(48, 76, 96),  rgb(70, 78, 98),  rgb(40, 58, 86)};
constexpr Rgba8 kThief[8] = {rgb(72, 72, 78), rgb(56, 58, 64), rgb(64, 60, 54), rgb(44, 50, 46),
                             rgb(78, 70, 64), rgb(50, 48, 56), rgb(66, 74, 70), rgb(58, 54, 50)};
constexpr Rgba8 kPoor[8] = {rgb(80, 76, 66), rgb(94, 84, 68), rgb(70, 72, 66), rgb(104, 90, 70),
                            rgb(86, 70, 60), rgb(74, 80, 72), rgb(96, 92, 80), rgb(68, 62, 56)};

[[nodiscard]] Rgba8 coatOf(sim::WardType type, int variant) noexcept {
    const std::size_t i = static_cast<std::size_t>(variant & 7);
    switch (type) {
        case sim::WardType::MilitiaWatch: return kWatchBlue[i];
        case sim::WardType::PriestOfTheFlame:
        case sim::WardType::DiscipleOfTheFlame: return kClergy[i];
        case sim::WardType::Sailor: return kSailor[i];
        case sim::WardType::Thief: return kThief[i];
        case sim::WardType::Wastrel:
        case sim::WardType::Urchin: return kPoor[i];
        default: return kCivilian[i];
    }
}

constexpr Rgba8 kLeather = rgb(58, 40, 28);
constexpr Rgba8 kBootLeather = rgb(46, 34, 26);
constexpr Rgba8 kWood = rgb(112, 80, 48);
constexpr Rgba8 kIron = rgb(150, 156, 168);
constexpr Rgba8 kLinen = rgb(214, 206, 188);
constexpr Rgba8 kShirt = rgb(204, 196, 174);
constexpr Rgba8 kApron = rgb(196, 184, 156);
constexpr Rgba8 kEyeWhite = rgb(226, 222, 212);
constexpr Rgba8 kEyeDark = rgb(30, 24, 22);

// ---------------------------------------------------------------------------
// BODY DIMENSIONS, from the build and the sex. Tiles; the person is 1.875
// tall at scale 1 (actor_instances.hpp, kPersonHeightTiles).
// ---------------------------------------------------------------------------

struct Dims {
    float chestRx, chestRz, waistRx, waistRz, hipRx, hipRz;
    float shoulderX, hipX;
    float armR, thighR, shinR;
};

[[nodiscard]] Dims dimsOf(const ActorLook& look) noexcept {
    constexpr float kBuild[4] = {0.88F, 1.0F, 1.12F, 1.26F};
    const float bf = kBuild[look.build & 3U];
    const bool f = look.sex != 0;
    Dims d{};
    d.chestRx = 0.178F * bf * (f ? 0.92F : 1.0F);
    d.chestRz = 0.100F + 0.012F * static_cast<float>(look.build & 3U);
    d.waistRx = 0.145F * bf * (f ? 0.86F : 1.0F);
    d.waistRz = 0.095F + 0.016F * static_cast<float>(look.build & 3U);
    d.hipRx = 0.158F * bf * (f ? 1.12F : 1.0F);
    d.hipRz = 0.105F + 0.010F * static_cast<float>(look.build & 3U);
    d.shoulderX = d.chestRx + 0.045F;
    d.hipX = d.hipRx * 0.52F;
    d.armR = 0.046F + 0.004F * static_cast<float>(look.build & 3U);
    d.thighR = 0.074F + 0.009F * static_cast<float>(look.build & 3U);
    d.shinR = 0.052F + 0.005F * static_cast<float>(look.build & 3U);
    return d;
}

// Heights of the joints, tiles, feet on 0.
constexpr float kHipY = 0.9375F;
constexpr float kWaistY = 1.02F;
constexpr float kNeckY = 1.60F;
constexpr float kShoulderY = 1.565F;
constexpr float kElbowY = 1.255F;
constexpr float kWristY = 0.985F;
constexpr float kKneeY = 0.515F;
constexpr float kAnkleY = 0.085F;

}  // namespace

FigureBone figureParentOf(FigureBone bone) noexcept {
    switch (bone) {
        case FigureBone::Pelvis: return FigureBone::Pelvis;
        case FigureBone::Torso: return FigureBone::Pelvis;
        case FigureBone::Head: return FigureBone::Torso;
        case FigureBone::UpperArmL: return FigureBone::Torso;
        case FigureBone::ForearmL: return FigureBone::UpperArmL;
        case FigureBone::HandL: return FigureBone::ForearmL;
        case FigureBone::UpperArmR: return FigureBone::Torso;
        case FigureBone::ForearmR: return FigureBone::UpperArmR;
        case FigureBone::HandR: return FigureBone::ForearmR;
        case FigureBone::ThighL: return FigureBone::Pelvis;
        case FigureBone::ShinL: return FigureBone::ThighL;
        case FigureBone::FootL: return FigureBone::ShinL;
        case FigureBone::ThighR: return FigureBone::Pelvis;
        case FigureBone::ShinR: return FigureBone::ThighR;
        case FigureBone::FootR: return FigureBone::ShinR;
    }
    return FigureBone::Pelvis;
}

// ---------------------------------------------------------------------------
// WHO. A pure function of kind, id and the sex the name said.
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kSaltBuild = 0x42554C44U;   // "BULD"
constexpr std::uint32_t kSaltSkin = 0x534B494EU;    // "SKIN"
constexpr std::uint32_t kSaltHair = 0x48414952U;    // "HAIR"
constexpr std::uint32_t kSaltCoat = 0x434F4154U;    // "COAT"
constexpr std::uint32_t kSaltHat = 0x48415421U;     // "HAT!"
constexpr std::uint32_t kSaltStyle = 0x5354594CU;   // "STYL"
constexpr std::uint32_t kSaltHeight = 0x48454954U;  // "HEIT"

}  // namespace

ActorLook actorLookFor(sim::WardType type, std::int32_t actorId, bool female) noexcept {
    ActorLook look;
    look.type = static_cast<std::uint8_t>(type);
    look.sex = female ? 1U : 0U;

    // BUILD: slight, average, broad, heavy by an eight-way draw; the quay's
    // working men run broad, a child and the ward's poor run slight.
    {
        constexpr std::uint8_t kBuilds[8] = {0, 0, 1, 1, 1, 2, 2, 3};
        std::uint32_t v = figureDraw(actorId, kSaltBuild) & 7U;
        std::uint8_t b = kBuilds[v];
        switch (type) {
            case sim::WardType::Urchin: b = 0; break;
            case sim::WardType::Wastrel: b = static_cast<std::uint8_t>(std::min<int>(b, 1)); break;
            case sim::WardType::Sailor:
            case sim::WardType::Carter:
            case sim::WardType::MilitiaWatch: b = static_cast<std::uint8_t>(std::max<int>(b, female ? 1 : 2)); break;
            default: break;
        }
        look.build = b;
    }
    // SKIN and HAIR: a sixteen-way draw into a weighted list, so the middle
    // of each table is the common case.
    {
        constexpr std::uint8_t kSkins[16] = {1, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5, 0, 6, 2, 3, 7};
        look.skin = kSkins[figureDraw(actorId, kSaltSkin) & 15U];
        constexpr std::uint8_t kHairs[16] = {1, 1, 2, 2, 2, 2, 0, 0, 3, 4, 5, 6, 6, 2, 1, 7};
        look.hairColour = kHairs[(figureDraw(actorId, kSaltHair) >> 4U) & 15U];
    }
    const std::uint32_t style = figureDraw(actorId, kSaltStyle);
    // HAIR STYLE: a woman wears it long, up or cropped; a man cropped, short
    // or shaved, now and then long.
    if (female) {
        constexpr std::uint8_t kStyles[8] = {2, 2, 3, 3, 3, 1, 2, 3};
        look.hairStyle = kStyles[style & 7U];
    } else {
        constexpr std::uint8_t kStyles[8] = {1, 1, 2, 2, 2, 0, 1, 3};
        look.hairStyle = kStyles[style & 7U];
    }
    look.coatColour = static_cast<std::uint8_t>(figureDraw(actorId, kSaltCoat) & 7U);
    look.legColour = static_cast<std::uint8_t>((figureDraw(actorId, kSaltCoat) >> 8U) & 3U);

    const std::uint32_t hat = figureDraw(actorId, kSaltHat) & 7U;
    const std::uint32_t cut = (style >> 8U) & 7U;
    const auto gear = [&look](HeadGear g) { look.headGear = static_cast<std::uint8_t>(g); };
    const auto coat = [&look](CoatStyle c) { look.coatStyle = static_cast<std::uint8_t>(c); };
    // GARMENTS AND HEADGEAR BY KIND: what the ward's people wear to the work
    // they do. The same kind never looks the same twice (the tables above),
    // but a Watchman is always a Watchman at a glance.
    switch (type) {
        case sim::WardType::MilitiaWatch:
            gear(HeadGear::Helmet);
            coat(CoatStyle::Coat);
            look.legColour = 2;
            break;
        case sim::WardType::PriestOfTheFlame:
            gear(HeadGear::None);
            coat(CoatStyle::Robe);
            look.hairStyle = female ? 2U : 0U;
            look.hairColour = 6;
            look.apron = 0;
            break;
        case sim::WardType::DiscipleOfTheFlame:
            gear(hat < 5U ? HeadGear::Hood : HeadGear::Coif);
            coat(CoatStyle::Robe);
            break;
        case sim::WardType::Sailor:
            gear(hat < 5U ? HeadGear::KnitCap : HeadGear::None);
            coat(cut < 5U ? CoatStyle::Shirt : CoatStyle::Tunic);
            break;
        case sim::WardType::Fisher:
            gear(hat < 4U ? HeadGear::BrimHat : HeadGear::None);
            coat(female ? (cut < 4U ? CoatStyle::Robe : CoatStyle::Tunic) : CoatStyle::Tunic);
            look.apron = (cut & 1U) != 0U ? 1U : 0U;
            break;
        case sim::WardType::Carter:
            gear(hat < 4U ? HeadGear::FlatCap : HeadGear::None);
            coat(cut < 3U ? CoatStyle::Coat : CoatStyle::Shirt);
            break;
        case sim::WardType::Shopkeeper:
            gear(female ? (hat < 3U ? HeadGear::Coif : HeadGear::None)
                        : (hat < 3U ? HeadGear::FlatCap : HeadGear::None));
            coat(female ? CoatStyle::Robe : (cut < 4U ? CoatStyle::Coat : CoatStyle::Tunic));
            look.apron = 1;
            break;
        case sim::WardType::Thief:
            gear(hat < 4U ? HeadGear::Hood : HeadGear::None);
            coat(CoatStyle::Coat);
            look.legColour = 2;
            break;
        case sim::WardType::Urchin:
            gear(hat < 3U ? HeadGear::FlatCap : HeadGear::None);
            coat(female ? CoatStyle::Robe : CoatStyle::Tunic);
            if (look.hairStyle == 0U) {
                look.hairStyle = 1;
            }
            break;
        case sim::WardType::Wastrel:
            gear(hat < 2U ? HeadGear::KnitCap : (hat < 3U ? HeadGear::Headscarf : HeadGear::None));
            coat(female ? (cut < 4U ? CoatStyle::Robe : CoatStyle::Tunic) : CoatStyle::Tunic);
            break;
        case sim::WardType::AnimalKeeper:
            gear(hat < 5U ? HeadGear::BrimHat : HeadGear::None);
            coat(CoatStyle::Coat);
            break;
        case sim::WardType::Serf:
        default:
            if (female) {
                gear(hat < 3U ? HeadGear::Headscarf : (hat < 5U ? HeadGear::Coif : HeadGear::None));
                coat(cut < 6U ? CoatStyle::Robe : CoatStyle::Tunic);
                look.apron = (cut & 1U) != 0U ? 1U : 0U;
            } else {
                gear(hat < 3U ? HeadGear::FlatCap : HeadGear::None);
                coat(cut < 3U ? CoatStyle::Shirt : CoatStyle::Tunic);
            }
            break;
    }
    // A woman's cropped cut under a scarf, or none of her hair showing, is
    // the scarf's: nothing to correct, the builder skips hair under cloth.
    return look;
}

float actorHeightJitter(std::int32_t actorId) noexcept {
    return 0.95F + 0.01F * static_cast<float>(figureDraw(actorId, kSaltHeight) % 11U);
}

// ---------------------------------------------------------------------------
// THE REST SKELETON
// ---------------------------------------------------------------------------

std::array<Vec3, kFigureBoneCount + 1> figureRestJoints(const ActorLook& look) noexcept {
    const Dims d = dimsOf(look);
    std::array<Vec3, kFigureBoneCount + 1> j{};
    j[static_cast<std::size_t>(FigureBone::Pelvis)] = Vec3{0.0F, kHipY, 0.0F};
    j[static_cast<std::size_t>(FigureBone::Torso)] = Vec3{0.0F, kWaistY, 0.0F};
    j[static_cast<std::size_t>(FigureBone::Head)] = Vec3{0.0F, kNeckY, 0.0F};
    j[static_cast<std::size_t>(FigureBone::UpperArmL)] = Vec3{-d.shoulderX, kShoulderY, 0.0F};
    j[static_cast<std::size_t>(FigureBone::ForearmL)] = Vec3{-d.shoulderX, kElbowY, 0.0F};
    j[static_cast<std::size_t>(FigureBone::HandL)] = Vec3{-d.shoulderX, kWristY, 0.0F};
    j[static_cast<std::size_t>(FigureBone::UpperArmR)] = Vec3{d.shoulderX, kShoulderY, 0.0F};
    j[static_cast<std::size_t>(FigureBone::ForearmR)] = Vec3{d.shoulderX, kElbowY, 0.0F};
    j[static_cast<std::size_t>(FigureBone::HandR)] = Vec3{d.shoulderX, kWristY, 0.0F};
    j[static_cast<std::size_t>(FigureBone::ThighL)] = Vec3{-d.hipX, kHipY, 0.0F};
    j[static_cast<std::size_t>(FigureBone::ShinL)] = Vec3{-d.hipX, kKneeY, 0.0F};
    j[static_cast<std::size_t>(FigureBone::FootL)] = Vec3{-d.hipX, kAnkleY, 0.0F};
    j[static_cast<std::size_t>(FigureBone::ThighR)] = Vec3{d.hipX, kHipY, 0.0F};
    j[static_cast<std::size_t>(FigureBone::ShinR)] = Vec3{d.hipX, kKneeY, 0.0F};
    j[static_cast<std::size_t>(FigureBone::FootR)] = Vec3{d.hipX, kAnkleY, 0.0F};
    // The stride: a robe or a dress takes short steps, a coat shorter than
    // trousers.
    float stride = 1.0F;
    if (look.coatStyle == static_cast<std::uint8_t>(CoatStyle::Robe)) {
        stride = 0.55F;
    } else if (look.coatStyle == static_cast<std::uint8_t>(CoatStyle::Coat)) {
        stride = 0.85F;
    }
    j[kFigureBoneCount] = Vec3{stride, 0.0F, 0.0F};
    return j;
}

// ---------------------------------------------------------------------------
// THE MESH
// ---------------------------------------------------------------------------

namespace {

/// Rings are written bottom to top. `domed` rounds both ends with a smaller
/// ring past the joint, so a limb that bends shows no gap.
[[nodiscard]] std::vector<Ring> domed(std::vector<Ring> rings) {
    if (rings.size() < 2) {
        return rings;
    }
    const Ring low = rings.front();
    const Ring high = rings.back();
    Ring below = low;
    below.y = low.y - 0.55F * std::min(low.rx, low.rz);
    below.rx = low.rx * 0.6F;
    below.rz = low.rz * 0.6F;
    Ring above = high;
    above.y = high.y + 0.55F * std::min(high.rx, high.rz);
    above.rx = high.rx * 0.6F;
    above.rz = high.rz * 0.6F;
    rings.insert(rings.begin(), below);
    rings.push_back(above);
    return rings;
}

/// A limb's rings: domed at both ends for the near figure (no gap at a bent
/// joint), plain for the far one, which is never bent.
[[nodiscard]] std::vector<Ring> limbRings(bool far, std::vector<Ring> rings) {
    return far ? rings : domed(std::move(rings));
}

/// The far figure's rings: every other interior ring dropped where it shares
/// its colour with both neighbours (a shape ring between two bands of the same
/// cloth), so the colour bands and the ends stay and the taper is coarser.
[[nodiscard]] std::vector<Ring> thinned(const std::vector<Ring>& rings) {
    const auto same = [](const Rgba8& a, const Rgba8& b) {
        return a.r == b.r && a.g == b.g && a.b == b.b;
    };
    std::vector<Ring> out;
    bool droppedLast = false;
    for (std::size_t i = 0; i < rings.size(); ++i) {
        const bool interior = i > 0 && i + 1 < rings.size();
        if (interior && !droppedLast && same(rings[i].colour, rings[i - 1].colour) &&
            same(rings[i].colour, rings[i + 1].colour)) {
            droppedLast = true;
            continue;
        }
        droppedLast = false;
        out.push_back(rings[i]);
    }
    return out;
}

/// loft(), at the figure's detail: the far one is thinned.
void loftLod(bool far, Builder& b, const std::vector<Ring>& rings, int sides, bool capBottom = true,
             bool capTop = true) {
    shapes::loft(b, far ? thinned(rings) : rings, sides, capBottom, capTop);
}

[[nodiscard]] Ring ring(float y, float rx, float rz, const Rgba8& c, float cx = 0.0F, float cz = 0.0F) noexcept {
    Ring r;
    r.y = y;
    r.rx = rx;
    r.rz = rz;
    r.cx = cx;
    r.cz = cz;
    r.colour = c;
    return r;
}

struct Parts {
    MeshData& mesh;
    Builder at(FigureBone bone) const {
        Builder b;
        b.mesh = &mesh;
        b.bone = static_cast<std::uint8_t>(bone);
        b.rigged = true;
        return b;
    }
};

void buildHead(const Parts& parts, const ActorLook& look, const Rgba8& skin, const Rgba8& hair,
               const Rgba8& coat, int bs, bool far) {
    Builder b = parts.at(FigureBone::Head);
    const bool f = look.sex != 0;
    const float k = f ? 0.95F : 1.0F;
    // The skull and jaw: eight sides, seven rings, the chin forward of the
    // neck and the brow behind the face.
    loftLod(far, b,
         {ring(1.600F, 0.050F, 0.050F, skin), ring(1.640F, 0.062F * k, 0.070F * k, skin),
          ring(1.690F, 0.078F * k, 0.088F * k, skin), ring(1.740F, 0.088F * k, 0.098F * k, skin),
          ring(1.790F, 0.090F * k, 0.100F * k, skin), ring(1.840F, 0.078F * k, 0.090F * k, skin),
          ring(1.875F, 0.046F * k, 0.056F * k, skin)},
         bs);
    // The face: a nose, a mouth, two eyes, a brow. Past the skin radius the
    // far figure keeps only the nose, which is what shows which way it faces.
    shapes::block(b, Vec3{0.0F, 1.725F, -0.101F * k}, Vec3{0.014F, 0.026F, 0.015F}, shapes::shaded(skin, 0.93F));
    if (!far) {
        shapes::block(b, Vec3{0.0F, 1.676F, -0.084F * k}, Vec3{0.022F, 0.004F, 0.004F},
                      shapes::mixed(skin, rgb(150, 70, 64), 0.55F));
    }
    for (const float sx : {-1.0F, 1.0F}) {
        if (far) {
            break;
        }
        shapes::block(b, Vec3{sx * 0.036F * k, 1.765F, -0.0965F * k}, Vec3{0.016F, 0.010F, 0.004F}, kEyeWhite);
        shapes::block(b, Vec3{sx * 0.036F * k, 1.765F, -0.1000F * k}, Vec3{0.008F, 0.008F, 0.003F}, kEyeDark);
        shapes::block(b, Vec3{sx * 0.037F * k, 1.792F, -0.0990F * k}, Vec3{0.019F, f ? 0.003F : 0.005F, 0.004F},
                      shapes::shaded(hair, 0.9F));
    }
    // The hair: a shell over the skull, cut by style; none under cloth or iron.
    const auto gear = static_cast<HeadGear>(look.headGear);
    const bool covered = gear == HeadGear::Helmet || gear == HeadGear::Hood || gear == HeadGear::Headscarf ||
                         gear == HeadGear::Coif;
    if (!covered && look.hairStyle != 0U) {
        switch (look.hairStyle) {
            case 1:
                loftLod(far, b,
                     {ring(1.805F, 0.094F * k, 0.106F * k, hair), ring(1.842F, 0.088F * k, 0.100F * k, hair),
                      ring(1.880F, 0.052F * k, 0.062F * k, hair)},
                     bs);
                break;
            case 2:
                loftLod(far, b,
                     {ring(1.700F, 0.099F * k, 0.098F * k, hair, 0.0F, 0.028F),
                      ring(1.760F, 0.098F * k, 0.105F * k, hair, 0.0F, 0.018F),
                      ring(1.805F, 0.097F * k, 0.108F * k, hair, 0.0F, 0.004F),
                      ring(1.845F, 0.088F * k, 0.100F * k, hair), ring(1.880F, 0.052F * k, 0.062F * k, hair)},
                     bs);
                if (f) {
                    shapes::block(b, Vec3{0.0F, 1.800F, 0.125F}, Vec3{0.040F, 0.034F, 0.034F}, hair);
                }
                break;
            default:
                loftLod(far, b,
                     {ring(1.520F, 0.075F, 0.050F, hair, 0.0F, 0.085F), ring(1.600F, 0.090F, 0.075F, hair, 0.0F, 0.075F),
                      ring(1.690F, 0.099F, 0.100F, hair, 0.0F, 0.035F), ring(1.760F, 0.098F, 0.105F, hair, 0.0F, 0.018F),
                      ring(1.805F, 0.097F, 0.108F, hair, 0.0F, 0.004F), ring(1.845F, 0.088F, 0.100F, hair),
                      ring(1.880F, 0.052F, 0.062F, hair)},
                     bs);
                break;
        }
    }
    // The headgear.
    switch (gear) {
        case HeadGear::FlatCap: {
            const Rgba8 cap = shapes::shaded(coat, 0.8F);
            loftLod(far, b,
                 {ring(1.800F, 0.098F * k, 0.110F * k, cap), ring(1.850F, 0.090F * k, 0.102F * k, cap),
                  ring(1.884F, 0.056F * k, 0.068F * k, cap)},
                 bs);
            shapes::block(b, Vec3{0.0F, 1.810F, -0.118F * k}, Vec3{0.070F, 0.005F, 0.030F}, shapes::shaded(cap, 0.7F));
            break;
        }
        case HeadGear::Helmet:
            loftLod(far, b,
                 {ring(1.755F, 0.122F, 0.132F, kIron), ring(1.772F, 0.122F, 0.132F, kIron),
                  ring(1.772F, 0.102F, 0.114F, kIron), ring(1.830F, 0.098F, 0.110F, kIron),
                  ring(1.890F, 0.068F, 0.078F, kIron), ring(1.930F, 0.018F, 0.022F, kIron)},
                 bs);
            shapes::block(b, Vec3{0.0F, 1.735F, -0.112F}, Vec3{0.008F, 0.042F, 0.006F}, shapes::shaded(kIron, 0.9F));
            break;
        case HeadGear::Hood:
            loftLod(far, b,
                 {ring(1.500F, 0.110F, 0.060F, coat, 0.0F, 0.080F), ring(1.580F, 0.130F, 0.090F, coat, 0.0F, 0.060F),
                  ring(1.660F, 0.118F, 0.110F, coat, 0.0F, 0.040F), ring(1.730F, 0.115F, 0.105F, coat, 0.0F, 0.030F),
                  ring(1.800F, 0.106F, 0.115F, coat, 0.0F, 0.012F), ring(1.860F, 0.086F, 0.105F, coat),
                  ring(1.898F, 0.050F, 0.070F, coat)},
                 bs);
            break;
        case HeadGear::BrimHat: {
            const Rgba8 straw = rgb(176, 148, 90);
            loftLod(far, b, {ring(1.795F, 0.200F, 0.200F, shapes::shaded(straw, 0.9F)), ring(1.806F, 0.200F, 0.200F, straw)}, bs);
            loftLod(far, b,
                 {ring(1.800F, 0.097F, 0.108F, straw), ring(1.835F, 0.094F, 0.104F, straw),
                  ring(1.900F, 0.076F, 0.086F, straw), ring(1.918F, 0.050F, 0.060F, straw)},
                 bs);
            break;
        }
        case HeadGear::KnitCap: {
            const Rgba8 wool = shapes::shaded(coat, 0.85F);
            loftLod(far, b,
                 {ring(1.770F, 0.100F, 0.112F, shapes::shaded(wool, 0.75F)), ring(1.800F, 0.102F, 0.113F, shapes::shaded(wool, 0.75F)),
                  ring(1.800F, 0.102F, 0.113F, wool), ring(1.845F, 0.094F, 0.106F, wool),
                  ring(1.895F, 0.066F, 0.080F, wool), ring(1.920F, 0.026F, 0.032F, wool)},
                 bs);
            break;
        }
        case HeadGear::Headscarf: {
            const Rgba8 cloth = shapes::mixed(coat, kLinen, 0.35F);
            loftLod(far, b,
                 {ring(1.690F, 0.100F, 0.098F, cloth, 0.0F, 0.030F), ring(1.760F, 0.100F, 0.108F, cloth, 0.0F, 0.020F),
                  ring(1.805F, 0.098F, 0.110F, cloth, 0.0F, 0.004F), ring(1.850F, 0.090F, 0.100F, cloth),
                  ring(1.886F, 0.056F, 0.066F, cloth)},
                 bs);
            shapes::block(b, Vec3{0.0F, 1.720F, 0.125F}, Vec3{0.026F, 0.026F, 0.020F}, cloth);
            break;
        }
        case HeadGear::Coif:
            loftLod(far, b,
                 {ring(1.700F, 0.098F, 0.100F, kLinen, 0.0F, 0.024F), ring(1.770F, 0.097F, 0.106F, kLinen, 0.0F, 0.014F),
                  ring(1.812F, 0.096F, 0.108F, kLinen, 0.0F, 0.002F), ring(1.850F, 0.088F, 0.100F, kLinen),
                  ring(1.884F, 0.054F, 0.064F, kLinen)},
                 bs);
            break;
        case HeadGear::None:
        default:
            break;
    }
}

}  // namespace

MeshData buildFigure(const ActorLook& look, bool far) {
    MeshData mesh;
    mesh.id = far ? figureFarMeshId(look) : figureMeshId(look);
    const int bs = far ? 6 : 8;
    const int ls = far ? 4 : 6;
    mesh.version = 1;
    const Dims d = dimsOf(look);
    const auto type = static_cast<sim::WardType>(look.type);
    const Rgba8 skin = kSkin[look.skin & 7U];
    const Rgba8 hair = kHair[look.hairColour & 7U];
    const Rgba8 coat = coatOf(type, look.coatColour);
    const Rgba8 trousers = kTrousers[look.legColour & 3U];
    const auto style = static_cast<CoatStyle>(look.coatStyle);
    const bool clergy = type == sim::WardType::PriestOfTheFlame || type == sim::WardType::DiscipleOfTheFlame;
    const Rgba8 sleeve = style == CoatStyle::Shirt ? kShirt : coat;
    const Rgba8 trim = clergy ? rgb(150, 44, 40) : shapes::shaded(coat, 0.7F);
    const Rgba8 belt = clergy ? rgb(150, 44, 40) : kLeather;
    const Parts parts{mesh};

    // --- the torso, with the neck on it ------------------------------------
    {
        Builder b = parts.at(FigureBone::Torso);
        const float bustRz = d.chestRz + (look.sex != 0 ? 0.014F : 0.0F);
        const float belly = look.build == 3 ? 0.022F : 0.0F;
        const Rgba8 body = style == CoatStyle::Shirt ? shapes::shaded(coat, 0.9F) : coat;
        const Rgba8 under = style == CoatStyle::Shirt ? kShirt : coat;
        loftLod(far, b,
             {ring(1.000F, d.waistRx * 0.98F, d.waistRz + belly * 0.6F, body),
              ring(1.060F, d.waistRx, d.waistRz + belly, body),
              ring(1.200F, (d.waistRx + d.chestRx) * 0.5F, (d.waistRz + d.chestRz) * 0.5F + belly * 0.3F, body),
              ring(1.390F, d.chestRx, bustRz, body), ring(1.530F, d.chestRx * 1.04F, d.chestRz * 0.92F, body),
              ring(1.585F, d.chestRx * 0.62F, d.chestRz * 0.66F, body),
              ring(1.600F, 0.082F, 0.074F, under), ring(1.606F, 0.056F, 0.056F, skin),
              ring(1.680F, 0.050F, 0.050F, skin)},
             bs);
    }

    // --- the pelvis: trousers or the hem of the tunic, the belt, the skirt ---
    {
        Builder b = parts.at(FigureBone::Pelvis);
        const bool hemmed = style == CoatStyle::Tunic || style == CoatStyle::Coat || style == CoatStyle::Robe;
        const Rgba8 hips = hemmed ? coat : trousers;
        loftLod(far, b,
             {ring(0.800F, d.hipRx * 0.92F, d.hipRz * 0.95F, hips), ring(0.920F, d.hipRx, d.hipRz, hips),
              ring(1.000F, d.waistRx * 1.02F, d.waistRz + 0.004F, hips),
              ring(1.004F, d.waistRx * 1.04F, d.waistRz + 0.010F, belt),
              ring(1.046F, d.waistRx * 1.04F, d.waistRz + 0.010F, belt)},
             bs);
        if (style == CoatStyle::Coat || style == CoatStyle::Robe) {
            const bool robe = style == CoatStyle::Robe;
            const float hem = robe ? (look.sex != 0 ? 0.16F : 0.10F) : 0.50F;
            const float flare = robe ? 1.55F : 1.30F;
            const float deep = robe ? 1.95F : 1.45F;
            std::vector<Ring> skirt;
            if (robe) {
                skirt.push_back(ring(hem, d.hipRx * flare, d.hipRz * deep + 0.02F, trim));
                skirt.push_back(ring(hem + 0.07F, d.hipRx * flare * 0.98F, d.hipRz * deep * 0.98F + 0.02F, trim));
                skirt.push_back(ring(hem + 0.07F, d.hipRx * flare * 0.98F, d.hipRz * deep * 0.98F + 0.02F, coat));
            } else {
                skirt.push_back(ring(hem, d.hipRx * flare, d.hipRz * deep + 0.02F, coat));
            }
            skirt.push_back(ring(0.78F, d.hipRx * 1.18F, d.hipRz * 1.22F + 0.01F, coat));
            skirt.push_back(ring(1.00F, d.waistRx * 1.08F, d.waistRz * 1.10F, coat));
            loftLod(far, b, skirt, bs, true, false);
        }
        // A pouch on the belt, and the Watch's cudgel.
        if (!far) {
            shapes::block(b, Vec3{-d.hipRx * 0.72F, 0.965F, -(d.hipRz + 0.026F)}, Vec3{0.034F, 0.034F, 0.020F}, kLeather);
        }
        if (type == sim::WardType::MilitiaWatch) {
            shapes::block(b, Vec3{d.hipRx + 0.040F, 0.86F, 0.02F}, Vec3{0.018F, 0.15F, 0.018F}, kWood);
        }
        if (look.apron != 0U) {
            const float z = (style == CoatStyle::Coat || style == CoatStyle::Robe) ? d.hipRz * 1.30F : d.hipRz + 0.020F;
            shapes::block(b, Vec3{0.0F, 0.70F, -(z + 0.014F)}, Vec3{0.120F, 0.26F, 0.010F}, kApron);
        }
    }
    if (look.apron != 0U) {
        Builder b = parts.at(FigureBone::Torso);
        shapes::block(b, Vec3{0.0F, 1.18F, -(d.waistRz + 0.014F)}, Vec3{0.100F, 0.14F, 0.010F}, kApron);
    }

    // --- the head -----------------------------------------------------------
    buildHead(parts, look, skin, hair, coat, bs, far);

    // --- the arms and the legs, left then right -----------------------------
    for (int s = 0; s < 2; ++s) {
        const float sx = s == 0 ? -1.0F : 1.0F;
        const auto bone = [s](FigureBone l, FigureBone r) { return s == 0 ? l : r; };
        const float ax = sx * d.shoulderX;
        const float robeSleeve = style == CoatStyle::Robe ? 1.22F : 1.0F;
        {
            Builder b = parts.at(bone(FigureBone::UpperArmL, FigureBone::UpperArmR));
            const float r = d.armR;
            loftLod(far, b,
                 limbRings(far, {ring(kElbowY, r * 0.82F, r * 0.82F, sleeve, ax), ring(1.400F, r * robeSleeve, r * robeSleeve, sleeve, ax),
                        ring(kShoulderY - 0.045F, r * 1.10F, r * 1.10F, sleeve, ax),
                        ring(kShoulderY, r * 1.0F, r * 1.0F, sleeve, ax)}),
                 ls);
        }
        {
            Builder b = parts.at(bone(FigureBone::ForearmL, FigureBone::ForearmR));
            const float r = d.armR * 0.92F;
            const bool bare = style == CoatStyle::Shirt;
            const Rgba8 cuff = bare ? shapes::shaded(kShirt, 0.95F) : sleeve;
            loftLod(far, b,
                 limbRings(far, {ring(kWristY, r * 0.74F, r * 0.74F, skin, ax), ring(1.090F, r * 0.82F, r * 0.82F, skin, ax),
                        ring(1.090F, r * 0.95F * robeSleeve, r * 0.95F * robeSleeve, cuff, ax),
                        ring(1.190F, r * 1.0F * robeSleeve, r * 1.0F * robeSleeve, sleeve, ax),
                        ring(kElbowY, r * 0.96F, r * 0.96F, sleeve, ax)}),
                 ls);
        }
        {
            Builder b = parts.at(bone(FigureBone::HandL, FigureBone::HandR));
            if (far) {
                shapes::block(b, Vec3{ax, 0.90F, -0.004F}, Vec3{0.034F, 0.085F, 0.016F}, skin);
            } else {
                shapes::block(b, Vec3{ax, 0.938F, -0.002F}, Vec3{0.034F, 0.046F, 0.017F}, skin);
                shapes::block(b, Vec3{ax, 0.862F, -0.006F}, Vec3{0.031F, 0.040F, 0.013F}, shapes::shaded(skin, 0.96F));
                shapes::block(b, Vec3{ax - sx * 0.030F, 0.940F, -0.028F}, Vec3{0.011F, 0.028F, 0.011F}, shapes::shaded(skin, 0.94F));
            }
        }
        const float lx = sx * d.hipX;
        {
            Builder b = parts.at(bone(FigureBone::ThighL, FigureBone::ThighR));
            const float r = d.thighR;
            loftLod(far, b,
                 limbRings(far, {ring(kKneeY, r * 0.78F, r * 0.78F, trousers, lx), ring(0.640F, r * 0.88F, r * 0.88F, trousers, lx),
                        ring(0.800F, r * 1.0F, r * 1.0F, trousers, lx),
                        ring(kHipY + 0.01F, r * 0.96F, r * 0.96F, trousers, lx)}),
                 ls);
        }
        {
            Builder b = parts.at(bone(FigureBone::ShinL, FigureBone::ShinR));
            const float r = d.shinR;
            loftLod(far, b,
                 limbRings(far, {ring(kAnkleY, r * 0.92F, r * 0.92F, kBootLeather, lx), ring(0.270F, r * 0.94F, r * 0.94F, kBootLeather, lx),
                        ring(0.270F, r * 0.98F, r * 0.98F, trousers, lx), ring(0.400F, r * 1.02F, r * 1.02F, trousers, lx),
                        ring(kKneeY, r * 1.0F, r * 1.0F, trousers, lx)}),
                 ls);
        }
        {
            Builder b = parts.at(bone(FigureBone::FootL, FigureBone::FootR));
            shapes::wedge(b, Vec3{lx, 0.050F, -0.045F}, Vec3{0.050F, 0.040F, 0.118F}, 0.044F, 0.098F, -0.022F, kBootLeather);
            if (!far) {
                shapes::block(b, Vec3{lx, 0.007F, -0.045F}, Vec3{0.053F, 0.007F, 0.124F}, shapes::shaded(kBootLeather, 0.6F));
            }
        }
    }

    fillNormals(mesh);
    if (far) {
        // The far figure is drawn at rest and never posed: no rig to carry.
        mesh.bones.clear();
        return mesh;
    }
    const auto rest = figureRestJoints(look);
    mesh.boneRest.reserve(rest.size() * 3);
    for (const Vec3& v : rest) {
        mesh.boneRest.push_back(v.x);
        mesh.boneRest.push_back(v.y);
        mesh.boneRest.push_back(v.z);
    }
    return mesh;
}

void putFigure(SceneDescription& scene, const ActorLook& look, bool far) {
    const std::uint32_t id = far ? figureFarMeshId(look) : figureMeshId(look);
    const MeshData* present = scene.findMesh(id);
    if (present != nullptr && present->version == 1) {
        return;
    }
    scene.putMesh(buildFigure(look, far));
}

// ---------------------------------------------------------------------------
// PHASE 9: THE KIT
// ---------------------------------------------------------------------------

ActorLook dressedLook(ActorLook look, const GearLook& gear) noexcept {
    switch (gear.head) {
        case GearHead::None:
            break;
        case GearHead::WoolCap:
            look.headGear = static_cast<std::uint8_t>(HeadGear::KnitCap);
            break;
        case GearHead::IronPot:
            look.headGear = static_cast<std::uint8_t>(HeadGear::Helmet);
            break;
        case GearHead::Hood:
            look.headGear = static_cast<std::uint8_t>(HeadGear::Hood);
            break;
        case GearHead::Cap:
            look.headGear = static_cast<std::uint8_t>(HeadGear::FlatCap);
            break;
    }
    switch (gear.body) {
        case GearBody::Coat:
            look.coatStyle = static_cast<std::uint8_t>(CoatStyle::Coat);
            break;
        case GearBody::None:
        case GearBody::Gambeson:
        case GearBody::Brigandine:
            break;
    }
    return look;
}

ActorLook playerFigureLook() noexcept {
    ActorLook look;
    look.type = static_cast<std::uint8_t>(sim::WardType::Sailor);
    look.sex = 0;
    look.build = 2;
    look.skin = 3;
    look.hairStyle = 1;
    look.hairColour = 2;
    look.coatColour = 0;
    look.legColour = 0;
    look.headGear = static_cast<std::uint8_t>(HeadGear::None);
    look.coatStyle = static_cast<std::uint8_t>(CoatStyle::Tunic);
    look.apron = 0;
    return look;
}

namespace {

// The kit's palette: what the things are made of, never the body's kind.
constexpr Rgba8 kQuilt = rgb(192, 178, 146);
constexpr Rgba8 kQuiltSeam = rgb(132, 118, 92);
constexpr Rgba8 kBrigCloth = rgb(110, 46, 40);
constexpr Rgba8 kRivet = rgb(182, 166, 126);
constexpr Rgba8 kGloveLeather = rgb(124, 92, 60);
constexpr Rgba8 kGauntletIron = rgb(126, 132, 142);
constexpr Rgba8 kBootBrown = rgb(88, 58, 36);
constexpr Rgba8 kHobnailLeather = rgb(62, 48, 38);
constexpr Rgba8 kSoleIron = rgb(72, 72, 76);
constexpr Rgba8 kSteel = rgb(182, 188, 198);
constexpr Rgba8 kGlass = rgb(62, 94, 64);
constexpr Rgba8 kShieldPaint = rgb(124, 50, 40);
constexpr Rgba8 kShieldWood = rgb(118, 88, 56);

/// One breakpoint of a body surface: a ring's height and radii.
struct Section {
    float y;
    float rx;
    float rz;
};

/// The widest any segment of `s` is at `y` (both sides of a step count),
/// folded into rx/rz. Nothing when `y` is outside the surface.
void widestAt(const std::vector<Section>& s, float y, float& rx, float& rz) noexcept {
    for (std::size_t i = 0; i + 1 < s.size(); ++i) {
        if (y < s[i].y || y > s[i + 1].y) {
            continue;
        }
        const float span = s[i + 1].y - s[i].y;
        const float t = span > 0.0F ? (y - s[i].y) / span : 1.0F;
        rx = std::max(rx, s[i].rx + (s[i + 1].rx - s[i].rx) * t);
        rz = std::max(rz, s[i].rz + (s[i + 1].rz - s[i].rz) * t);
        if (span <= 0.0F) {
            rx = std::max(rx, s[i].rx);
            rz = std::max(rz, s[i].rz);
        }
    }
}

/// THE JACK: a shell over the torso from just under the belt's top to the
/// collar. Its every ring stands `m` proud of the WIDEST thing the body has
/// at that height (the torso's own rings, the belt, a coat's skirt top, an
/// apron's bib) and it has a ring at every height any of those has one, so
/// between two rings the body is a straight line under a straight line and
/// the clearance holds all the way: a quilted coat that never shows the
/// shirt through it, at any distance the depth buffer can see.
void buildJack(const Parts& parts, const ActorLook& look, const Dims& d, GearBody body, int sides, bool far) {
    const bool quilted = body == GearBody::Gambeson;
    const float m = quilted ? 0.028F : 0.020F;
    const float bustRz = d.chestRz + (look.sex != 0 ? 0.014F : 0.0F);
    const float belly = look.build == 3 ? 0.022F : 0.0F;
    const auto style = static_cast<CoatStyle>(look.coatStyle);
    std::vector<std::vector<Section>> surfaces;
    surfaces.push_back({{1.000F, d.waistRx * 0.98F, d.waistRz + belly * 0.6F},
                        {1.060F, d.waistRx, d.waistRz + belly},
                        {1.200F, (d.waistRx + d.chestRx) * 0.5F, (d.waistRz + d.chestRz) * 0.5F + belly * 0.3F},
                        {1.390F, d.chestRx, bustRz},
                        {1.530F, d.chestRx * 1.04F, d.chestRz * 0.92F},
                        {1.585F, d.chestRx * 0.62F, d.chestRz * 0.66F},
                        {1.600F, 0.082F, 0.074F},
                        {1.606F, 0.056F, 0.056F}});
    surfaces.push_back({{1.000F, d.waistRx * 1.02F, d.waistRz + 0.004F},
                        {1.004F, d.waistRx * 1.04F, d.waistRz + 0.010F},
                        {1.046F, d.waistRx * 1.04F, d.waistRz + 0.010F}});
    if (style == CoatStyle::Coat || style == CoatStyle::Robe) {
        surfaces.push_back({{0.78F, d.hipRx * 1.18F, d.hipRz * 1.22F + 0.01F},
                            {1.00F, d.waistRx * 1.08F, d.waistRz * 1.10F}});
    }
    if (look.apron != 0U) {
        // The bib's front corners sit a tenth either side of the middle,
        // where the ellipse is a fifth shallower than at the front.
        const float bib = 1.25F * (d.waistRz + 0.024F);
        surfaces.push_back({{1.04F, 0.0F, bib}, {1.32F, 0.0F, bib}});
    }
    const auto at = [&surfaces](float y, float proud, const Rgba8& c) {
        float rx = 0.0F;
        float rz = 0.0F;
        for (const std::vector<Section>& s : surfaces) {
            widestAt(s, y, rx, rz);
        }
        return ring(y, rx + proud, rz + proud, c);
    };
    const Rgba8 cloth = quilted ? kQuilt : kBrigCloth;
    const Rgba8 trim = quilted ? shapes::shaded(kQuilt, 0.78F) : kLeather;
    std::vector<Ring> rings;
    // A quilting seam: the cloth pinched into a dark line, near only.
    const auto seam = [&](float y) {
        if (!quilted || far) {
            return;
        }
        rings.push_back(at(y - 0.005F, m, cloth));
        rings.push_back(at(y - 0.005F, m - 0.004F, kQuiltSeam));
        rings.push_back(at(y + 0.005F, m - 0.004F, kQuiltSeam));
        rings.push_back(at(y + 0.005F, m, cloth));
    };
    rings.push_back(at(1.012F, m, trim));
    rings.push_back(at(1.030F, m, trim));
    rings.push_back(at(1.030F, m, cloth));
    rings.push_back(at(1.040F, m, cloth));
    rings.push_back(at(1.046F, m, cloth));
    rings.push_back(at(1.060F, m, cloth));
    seam(1.125F);
    rings.push_back(at(1.200F, m, cloth));
    seam(1.260F);
    rings.push_back(at(1.320F, m, cloth));
    rings.push_back(at(1.390F, m, cloth));
    seam(1.460F);
    rings.push_back(at(1.530F, m, cloth));
    rings.push_back(at(1.570F, m, cloth));
    rings.push_back(at(1.570F, m, trim));
    rings.push_back(at(1.585F, m, trim));
    rings.push_back(at(1.600F, 0.014F, trim));
    Builder b = parts.at(FigureBone::Torso);
    shapes::loft(b, rings, sides, false, false);
    // The brigandine's rivets: three down the front at four heights, on
    // the shell's own front faces.
    if (!quilted && !far) {
        for (const float y : {1.10F, 1.22F, 1.34F, 1.46F}) {
            const Ring r = at(y, m, cloth);
            for (const float x : {-0.07F, 0.0F, 0.07F}) {
                const float t = std::min(1.0F, std::fabs(x) / (0.70710678F * r.rx));
                const float z = -r.rz * (1.0F - 0.29289322F * t);
                shapes::block(b, Vec3{x, y, z - 0.002F}, Vec3{0.007F, 0.007F, 0.005F}, kRivet);
            }
        }
    }
}

/// THE GLOVES: the hand's own blocks a few millimetres bigger in leather or
/// iron, and a cuff round the wrist on the forearm.
void buildGloves(const Parts& parts, const Dims& d, GearHands hands, int sides, bool far) {
    const bool iron = hands == GearHands::Gauntlets;
    const Rgba8 colour = iron ? kGauntletIron : kGloveLeather;
    const float g = iron ? 0.007F : 0.005F;
    for (int s = 0; s < 2; ++s) {
        const float sx = s == 0 ? -1.0F : 1.0F;
        const float ax = sx * d.shoulderX;
        {
            Builder b = parts.at(s == 0 ? FigureBone::HandL : FigureBone::HandR);
            if (far) {
                shapes::block(b, Vec3{ax, 0.90F, -0.004F}, Vec3{0.034F + g, 0.085F + g, 0.016F + g}, colour);
            } else {
                shapes::block(b, Vec3{ax, 0.938F, -0.002F}, Vec3{0.034F + g, 0.046F + g, 0.017F + g}, colour);
                shapes::block(b, Vec3{ax, 0.862F, -0.006F}, Vec3{0.031F + g, 0.040F + g, 0.013F + g},
                              shapes::shaded(colour, 0.94F));
                shapes::block(b, Vec3{ax - sx * 0.030F, 0.940F, -0.028F}, Vec3{0.011F + g, 0.028F + g, 0.011F + g},
                              shapes::shaded(colour, 0.92F));
            }
        }
        {
            Builder b = parts.at(s == 0 ? FigureBone::ForearmL : FigureBone::ForearmR);
            const float r = d.armR * 0.92F;
            const Rgba8 cuff = shapes::shaded(colour, iron ? 1.08F : 0.86F);
            if (iron) {
                shapes::loft(b,
                             {ring(0.972F, r * 0.78F + 0.012F, r * 0.78F + 0.012F, cuff, ax),
                              ring(1.060F, r * 0.90F + 0.015F, r * 0.90F + 0.015F, cuff, ax)},
                             sides, true, true);
            } else {
                shapes::loft(b,
                             {ring(0.972F, r * 0.74F + 0.012F, r * 0.74F + 0.012F, cuff, ax),
                              ring(1.050F, r * 0.82F + 0.014F, r * 0.82F + 0.014F, cuff, ax)},
                             sides, true, true);
            }
        }
    }
}

/// THE BOOTS: a shaft over the boot leather every figure wears, to the
/// calf with a turned top, and a fuller foot; hobnails are an iron sole.
void buildBoots(const Parts& parts, const Dims& d, GearFeet feet, int sides, bool far) {
    const bool nailed = feet == GearFeet::HobnailBoots;
    const Rgba8 leather = nailed ? kHobnailLeather : kBootBrown;
    const Rgba8 turned = shapes::shaded(leather, 0.78F);
    for (int s = 0; s < 2; ++s) {
        const float sx = s == 0 ? -1.0F : 1.0F;
        const float lx = sx * d.hipX;
        {
            Builder b = parts.at(s == 0 ? FigureBone::ShinL : FigureBone::ShinR);
            const float r = d.shinR;
            const float m = 0.010F;
            shapes::loft(b,
                         {ring(0.080F, r * 0.92F + m, r * 0.92F + m, leather, lx),
                          ring(0.270F, r * 0.98F + m, r * 0.98F + m, leather, lx),
                          ring(0.345F, r * 1.01F + m, r * 1.01F + m, leather, lx),
                          ring(0.345F, r * 1.01F + m + 0.008F, r * 1.01F + m + 0.008F, turned, lx),
                          ring(0.375F, r * 1.025F + m + 0.008F, r * 1.025F + m + 0.008F, turned, lx)},
                         sides, false, true);
        }
        {
            Builder b = parts.at(s == 0 ? FigureBone::FootL : FigureBone::FootR);
            shapes::wedge(b, Vec3{lx, 0.053F, -0.046F}, Vec3{0.058F, 0.045F, 0.126F}, 0.052F, 0.104F, -0.022F,
                          leather);
            if (nailed && !far) {
                shapes::block(b, Vec3{lx, 0.0055F, -0.046F}, Vec3{0.061F, 0.0055F, 0.132F}, kSoleIron);
            }
        }
    }
}

/// A builder whose local +Y runs from `butt` along `dir` (unit): rings and
/// blocks are then authored as distance along the haft (y), across it in
/// the swing plane (x) and across it sideways (z), the viewmodel's own
/// convention for a held thing.
[[nodiscard]] Builder haftAt(const Parts& parts, FigureBone bone, const Vec3& butt, const Vec3& dir, float length) {
    Builder b = parts.at(bone);
    float unused = 0.0F;
    b.xf = shapes::alongAxis(butt, Vec3{butt.x + dir.x * length, butt.y + dir.y * length, butt.z + dir.z * length},
                             Vec3{1.0F, 0.0F, 0.0F}, unused);
    return b;
}

/// THE PROP IN THE RIGHT HAND. The grip is in the closed fingers of the
/// hanging hand; a one-handed thing leaves it forward and down at 45
/// degrees (the idle's bent arm brings it up toward the level, the punch's
/// swing over the shoulder), a two-handed one is carried by the top of the
/// haft with its head down by the right foot, and the boat hook stands like
/// a staff. Wood, iron and steel; nothing is art.
void buildProp(const Parts& parts, const Dims& d, GearProp prop, int sides, bool far) {
    const Vec3 grip{d.shoulderX, 0.880F, -0.006F};
    const Vec3 hang{0.0F, -0.70710678F, -0.70710678F};
    const Vec3 carry{0.0F, -0.93969262F, -0.34202014F};
    const Vec3 staff{0.0F, 0.93969262F, -0.34202014F};
    const Rgba8 wrap = shapes::shaded(kWood, 0.55F);
    const Rgba8 darkWood = shapes::shaded(kWood, 0.82F);
    const Rgba8 edge = shapes::shaded(kSteel, 1.08F);
    const int blade = 4;
    const auto from = [&grip](const Vec3& dir, float back) {
        return Vec3{grip.x - dir.x * back, grip.y - dir.y * back, grip.z - dir.z * back};
    };
    switch (prop) {
        case GearProp::None:
            return;
        case GearProp::Cudgel: {
            Builder b = haftAt(parts, FigureBone::HandR, from(hang, 0.06F), hang, 0.56F);
            shapes::loft(b,
                         {ring(0.0F, 0.016F, 0.016F, kIron), ring(0.012F, 0.020F, 0.020F, kIron),
                          ring(0.012F, 0.017F, 0.017F, wrap), ring(0.14F, 0.017F, 0.017F, wrap),
                          ring(0.14F, 0.016F, 0.016F, kWood), ring(0.34F, 0.022F, 0.022F, kWood),
                          ring(0.50F, 0.030F, 0.030F, darkWood), ring(0.56F, 0.024F, 0.024F, darkWood)},
                         sides);
            return;
        }
        case GearProp::Evictor: {
            Builder b = haftAt(parts, FigureBone::HandR, from(hang, 0.06F), hang, 0.58F);
            shapes::loft(b,
                         {ring(0.0F, 0.016F, 0.016F, kIron), ring(0.012F, 0.020F, 0.020F, kIron),
                          ring(0.012F, 0.017F, 0.017F, wrap), ring(0.15F, 0.017F, 0.017F, wrap),
                          ring(0.15F, 0.016F, 0.016F, kWood), ring(0.36F, 0.022F, 0.022F, kWood),
                          ring(0.40F, 0.034F, 0.034F, kIron), ring(0.42F, 0.040F, 0.040F, darkWood),
                          ring(0.50F, 0.044F, 0.044F, darkWood), ring(0.52F, 0.040F, 0.040F, kIron),
                          ring(0.58F, 0.020F, 0.020F, kIron)},
                         sides);
            return;
        }
        case GearProp::Bottle: {
            // Held by the neck, the body of it hanging forward.
            Builder b = haftAt(parts, FigureBone::HandR, from(hang, 0.02F), hang, 0.28F);
            shapes::loft(b,
                         {ring(0.0F, 0.010F, 0.010F, kLeather), ring(0.03F, 0.011F, 0.011F, kGlass),
                          ring(0.08F, 0.013F, 0.013F, kGlass), ring(0.12F, 0.034F, 0.034F, kGlass),
                          ring(0.26F, 0.036F, 0.036F, shapes::shaded(kGlass, 0.9F)),
                          ring(0.28F, 0.030F, 0.030F, shapes::shaded(kGlass, 0.8F))},
                         sides);
            return;
        }
        case GearProp::Knife: {
            Builder b = haftAt(parts, FigureBone::HandR, from(hang, 0.04F), hang, 0.25F);
            shapes::loft(b, {ring(0.0F, 0.012F, 0.012F, kWood), ring(0.10F, 0.013F, 0.013F, kWood)}, sides);
            shapes::block(b, Vec3{0.0F, 0.105F, 0.0F}, Vec3{0.020F, 0.005F, 0.008F}, kIron);
            shapes::loft(b,
                         {ring(0.11F, 0.012F, 0.0035F, kSteel), ring(0.20F, 0.011F, 0.0030F, kSteel),
                          ring(0.25F, 0.0015F, 0.0015F, edge)},
                         blade);
            return;
        }
        case GearProp::Dagger: {
            Builder b = haftAt(parts, FigureBone::HandR, from(hang, 0.05F), hang, 0.33F);
            shapes::loft(b,
                         {ring(0.0F, 0.013F, 0.013F, kIron), ring(0.012F, 0.016F, 0.016F, kIron),
                          ring(0.012F, 0.012F, 0.012F, wrap), ring(0.11F, 0.012F, 0.012F, wrap)},
                         sides);
            shapes::block(b, Vec3{0.0F, 0.115F, 0.0F}, Vec3{0.045F, 0.007F, 0.010F}, kIron);
            shapes::loft(b,
                         {ring(0.12F, 0.016F, 0.0040F, kSteel), ring(0.26F, 0.012F, 0.0035F, kSteel),
                          ring(0.33F, 0.0015F, 0.0015F, edge)},
                         blade);
            return;
        }
        case GearProp::Cutlass: {
            Builder b = haftAt(parts, FigureBone::HandR, from(hang, 0.07F), hang, 0.75F);
            shapes::loft(b,
                         {ring(0.0F, 0.016F, 0.016F, kIron), ring(0.014F, 0.019F, 0.019F, kIron),
                          ring(0.014F, 0.014F, 0.014F, wrap), ring(0.12F, 0.015F, 0.015F, wrap)},
                         sides);
            // The cup guard, and the knuckle bow back to the pommel.
            shapes::loft(b,
                         {ring(0.12F, 0.016F, 0.016F, kIron), ring(0.13F, 0.052F, 0.050F, kIron),
                          ring(0.145F, 0.054F, 0.052F, shapes::shaded(kIron, 0.9F)),
                          ring(0.145F, 0.016F, 0.016F, kIron)},
                         sides);
            if (!far) {
                shapes::block(b, Vec3{0.042F, 0.072F, 0.0F}, Vec3{0.006F, 0.062F, 0.006F}, kIron);
            }
            shapes::loft(b,
                         {ring(0.145F, 0.022F, 0.0050F, kSteel), ring(0.45F, 0.026F, 0.0050F, kSteel),
                          ring(0.66F, 0.030F, 0.0045F, kSteel), ring(0.73F, 0.006F, 0.0025F, edge),
                          ring(0.75F, 0.0015F, 0.0015F, edge)},
                         blade);
            return;
        }
        case GearProp::BoatHook: {
            Builder b = haftAt(parts, FigureBone::HandR, from(staff, 0.55F), staff, 1.50F);
            shapes::loft(b,
                         {ring(0.0F, 0.016F, 0.016F, kIron), ring(0.03F, 0.018F, 0.018F, kWood),
                          ring(1.42F, 0.018F, 0.018F, kWood), ring(1.42F, 0.020F, 0.020F, kIron),
                          ring(1.50F, 0.014F, 0.014F, kIron)},
                         sides);
            // The hook (forward, which is this frame's -X), and the spike.
            shapes::block(b, Vec3{-0.050F, 1.44F, 0.0F}, Vec3{0.045F, 0.010F, 0.008F}, kIron);
            shapes::block(b, Vec3{-0.088F, 1.47F, 0.0F}, Vec3{0.008F, 0.030F, 0.008F}, kIron);
            shapes::block(b, Vec3{0.0F, 1.53F, 0.0F}, Vec3{0.008F, 0.030F, 0.008F}, kIron);
            return;
        }
        case GearProp::Hatchet: {
            // The cleaver head: an iron eye, a broad blade forward, a poll.
            Builder b = haftAt(parts, FigureBone::HandR, from(hang, 0.05F), hang, 0.47F);
            shapes::loft(b, {ring(0.0F, 0.017F, 0.017F, kWood), ring(0.40F, 0.020F, 0.020F, kWood),
                             ring(0.47F, 0.019F, 0.019F, darkWood)},
                         sides);
            shapes::block(b, Vec3{0.0F, 0.425F, 0.0F}, Vec3{0.022F, 0.035F, 0.018F}, kIron);
            shapes::block(b, Vec3{0.066F, 0.420F, 0.0F}, Vec3{0.045F, 0.055F, 0.006F}, kSteel);
            if (!far) {
                shapes::block(b, Vec3{0.112F, 0.420F, 0.0F}, Vec3{0.004F, 0.058F, 0.0035F}, edge);
                shapes::block(b, Vec3{-0.032F, 0.425F, 0.0F}, Vec3{0.012F, 0.022F, 0.016F}, kIron);
            }
            return;
        }
        case GearProp::Mace: {
            Builder b = haftAt(parts, FigureBone::HandR, from(hang, 0.06F), hang, 0.57F);
            shapes::loft(b,
                         {ring(0.0F, 0.016F, 0.016F, kIron), ring(0.014F, 0.019F, 0.019F, kIron),
                          ring(0.014F, 0.016F, 0.016F, wrap), ring(0.15F, 0.016F, 0.016F, wrap),
                          ring(0.15F, 0.015F, 0.015F, kWood), ring(0.42F, 0.017F, 0.017F, kWood)},
                         sides);
            shapes::loft(b,
                         {ring(0.42F, 0.020F, 0.020F, kIron), ring(0.44F, 0.036F, 0.036F, kIron),
                          ring(0.52F, 0.040F, 0.040F, kIron), ring(0.55F, 0.020F, 0.020F, kIron),
                          ring(0.57F, 0.010F, 0.010F, kIron)},
                         sides);
            if (!far) {
                // Four flanges round the head.
                for (const float sgn : {-1.0F, 1.0F}) {
                    shapes::block(b, Vec3{sgn * 0.046F, 0.485F, 0.0F}, Vec3{0.012F, 0.036F, 0.004F},
                                  shapes::shaded(kIron, 0.9F));
                    shapes::block(b, Vec3{0.0F, 0.485F, sgn * 0.046F}, Vec3{0.004F, 0.036F, 0.012F},
                                  shapes::shaded(kIron, 0.9F));
                }
            }
            return;
        }
        case GearProp::Maul: {
            // Carried by the top of the haft, the head down by the foot.
            Builder b = haftAt(parts, FigureBone::HandR, from(carry, 0.08F), carry, 0.86F);
            shapes::loft(b,
                         {ring(0.0F, 0.020F, 0.020F, kIron), ring(0.02F, 0.021F, 0.021F, kWood),
                          ring(0.74F, 0.024F, 0.024F, kWood)},
                         sides);
            shapes::block(b, Vec3{0.0F, 0.800F, 0.0F}, Vec3{0.130F, 0.065F, 0.065F}, kIron);
            if (!far) {
                for (const float sgn : {-1.0F, 1.0F}) {
                    shapes::block(b, Vec3{sgn * 0.128F, 0.800F, 0.0F}, Vec3{0.008F, 0.070F, 0.070F},
                                  shapes::shaded(kIron, 0.85F));
                }
            }
            return;
        }
        case GearProp::BoardingAxe: {
            // The same carry: a bearded blade forward, a spike behind.
            Builder b = haftAt(parts, FigureBone::HandR, from(carry, 0.08F), carry, 0.88F);
            shapes::loft(b,
                         {ring(0.0F, 0.018F, 0.018F, kIron), ring(0.02F, 0.019F, 0.019F, kWood),
                          ring(0.84F, 0.021F, 0.021F, kWood), ring(0.88F, 0.020F, 0.020F, darkWood)},
                         sides);
            shapes::block(b, Vec3{0.0F, 0.800F, 0.0F}, Vec3{0.024F, 0.040F, 0.020F}, kIron);
            shapes::wedge(b, Vec3{0.090F, 0.790F, 0.0F}, Vec3{0.065F, 0.055F, 0.006F}, 0.048F, 0.006F, 0.0F, kSteel);
            if (!far) {
                shapes::block(b, Vec3{0.152F, 0.785F, 0.0F}, Vec3{0.004F, 0.058F, 0.004F}, edge);
                shapes::block(b, Vec3{-0.052F, 0.800F, 0.0F}, Vec3{0.030F, 0.010F, 0.008F}, kIron);
            }
            return;
        }
    }
}

/// THE SHIELD ON THE LEFT ARM: a round shield strapped along the forearm, a
/// buckler gripped in the fist. Both face FORWARD and a little out (a shield
/// faced square to the side is a line from the front, and the front is where
/// a body is seen from), standing clear in front of the forearm and the hip,
/// the boss off the painted face and plank seams across it.
void buildShield(const Parts& parts, const Dims& d, GearShield shield, int sides, bool far) {
    if (shield == GearShield::None) {
        return;
    }
    const bool round = shield == GearShield::RoundShield;
    const float r = round ? 0.27F : 0.15F;
    // The face's outward normal: forward (-Z) and out to the left (-X).
    const Vec3 axis = round ? Vec3{-0.50F, 0.0F, -0.8660254F} : Vec3{-0.40F, 0.0F, -0.9165151F};
    const Vec3 c = round ? Vec3{-(d.shoulderX + d.armR + 0.060F), 1.120F, -0.080F}
                         : Vec3{-(d.shoulderX + 0.050F), 0.900F, -0.070F};
    const float back = round ? 0.022F : 0.012F;
    const Vec3 origin{c.x - axis.x * back, c.y, c.z - axis.z * back};
    const Vec3 end{c.x + axis.x * 0.040F, c.y, c.z + axis.z * 0.040F};
    Builder b = parts.at(round ? FigureBone::ForearmL : FigureBone::HandL);
    float unused = 0.0F;
    b.xf = shapes::alongAxis(origin, end, Vec3{0.0F, 1.0F, 0.0F}, unused);
    const Rgba8 rim = round ? kIron : shapes::shaded(kIron, 0.9F);
    const Rgba8 face = round ? kShieldPaint : kSteel;
    const Rgba8 rear = round ? kShieldWood : shapes::shaded(kIron, 0.7F);
    shapes::loft(b,
                 {ring(0.000F, r * 0.96F, r * 0.96F, rear), ring(0.008F, r, r, rim), ring(0.020F, r, r, rim),
                  ring(0.020F, r * 0.97F, r * 0.97F, face), ring(0.030F, r * 0.90F, r * 0.90F, face),
                  ring(0.030F, r * 0.24F, r * 0.24F, rim), ring(0.048F, r * 0.18F, r * 0.18F, rim),
                  ring(0.058F, r * 0.07F, r * 0.07F, rim)},
                 sides);
    if (round && !far) {
        for (const float x : {-0.14F, -0.05F, 0.05F, 0.14F}) {
            const float chord = std::sqrt(std::max(0.0F, r * r * 0.82F - x * x));
            shapes::block(b, Vec3{x, 0.0265F, 0.0F}, Vec3{0.005F, 0.0045F, chord}, shapes::shaded(kShieldPaint, 0.62F));
        }
    }
    if (!far) {
        // The strap or the grip, from the arm to the shield's back.
        Builder grip = parts.at(round ? FigureBone::ForearmL : FigureBone::HandL);
        const Vec3 at = round ? Vec3{-(d.shoulderX + d.armR * 0.5F), 1.12F, -0.085F}
                              : Vec3{-(d.shoulderX + 0.030F), 0.900F, -0.040F};
        shapes::block(grip, at, Vec3{0.018F, 0.014F, round ? 0.035F : 0.030F}, kLeather);
    }
}

}  // namespace

MeshData buildFigureKit(const ActorLook& look, const GearLook& gear, bool far) {
    MeshData mesh;
    mesh.id = figureKitMeshId(look, gear, far);
    mesh.version = 1;
    if (!gear.hasKit()) {
        return mesh;
    }
    const int bs = far ? 6 : 8;
    const int ls = far ? 4 : 6;
    const Dims d = dimsOf(look);
    const Parts parts{mesh};
    switch (gear.body) {
        case GearBody::Gambeson:
        case GearBody::Brigandine:
            buildJack(parts, look, d, gear.body, bs, far);
            break;
        case GearBody::None:
        case GearBody::Coat:
            break;
    }
    switch (gear.hands) {
        case GearHands::WorkGloves:
        case GearHands::Gauntlets:
            buildGloves(parts, d, gear.hands, ls, far);
            break;
        case GearHands::None:
            break;
    }
    switch (gear.feet) {
        case GearFeet::Boots:
        case GearFeet::HobnailBoots:
            buildBoots(parts, d, gear.feet, ls, far);
            break;
        case GearFeet::None:
            break;
    }
    buildProp(parts, d, gear.prop, ls, far);
    buildShield(parts, d, gear.shield, bs, far);

    fillNormals(mesh);
    if (far) {
        mesh.bones.clear();
        return mesh;
    }
    const auto rest = figureRestJoints(look);
    mesh.boneRest.reserve(rest.size() * 3);
    for (const Vec3& v : rest) {
        mesh.boneRest.push_back(v.x);
        mesh.boneRest.push_back(v.y);
        mesh.boneRest.push_back(v.z);
    }
    return mesh;
}

void putFigureKit(SceneDescription& scene, const ActorLook& look, const GearLook& gear, bool far) {
    if (!gear.hasKit()) {
        return;
    }
    const std::uint32_t id = figureKitMeshId(look, gear, far);
    const MeshData* present = scene.findMesh(id);
    if (present != nullptr && present->version == 1) {
        return;
    }
    scene.putMesh(buildFigureKit(look, gear, far));
}

// ---------------------------------------------------------------------------
// THE ANIMATION
// ---------------------------------------------------------------------------

namespace {

struct Local {
    float pitch = 0.0F;
    float yaw = 0.0F;
    float roll = 0.0F;
};

struct Angles {
    std::array<Local, kFigureBoneCount> j{};
    Vec3 root{};
};

[[nodiscard]] Local& at(Angles& a, FigureBone b) noexcept { return a.j[static_cast<std::size_t>(b)]; }

[[nodiscard]] float smooth01(float x) noexcept {
    const float t = std::clamp(x, 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

[[nodiscard]] float lerpf(float a, float b, float t) noexcept { return a + (b - a) * t; }

[[nodiscard]] Angles lerpAngles(const Angles& a, const Angles& b, float t) noexcept {
    Angles out;
    for (std::size_t i = 0; i < kFigureBoneCount; ++i) {
        out.j[i].pitch = lerpf(a.j[i].pitch, b.j[i].pitch, t);
        out.j[i].yaw = lerpf(a.j[i].yaw, b.j[i].yaw, t);
        out.j[i].roll = lerpf(a.j[i].roll, b.j[i].roll, t);
    }
    out.root = Vec3{lerpf(a.root.x, b.root.x, t), lerpf(a.root.y, b.root.y, t), lerpf(a.root.z, b.root.z, t)};
    return out;
}

/// Standing, breathing, looking about a little. `t` is the loop's phase.
[[nodiscard]] Angles idleAngles(float t) noexcept {
    Angles a;
    const float b = std::sin(kTwoPi * t * 4.0F);
    const float look = std::sin(kTwoPi * t);
    at(a, FigureBone::Torso).pitch = -0.012F * b;
    at(a, FigureBone::Head).yaw = 0.14F * look;
    at(a, FigureBone::Head).pitch = 0.025F * std::sin(kTwoPi * t * 2.0F + 1.0F) + 0.012F * b;
    at(a, FigureBone::Pelvis).roll = 0.018F * std::sin(kTwoPi * t * 0.5F);
    at(a, FigureBone::UpperArmL).roll = -0.07F - 0.01F * b;
    at(a, FigureBone::UpperArmR).roll = 0.07F + 0.01F * b;
    at(a, FigureBone::UpperArmL).pitch = 0.04F;
    at(a, FigureBone::UpperArmR).pitch = 0.04F;
    at(a, FigureBone::ForearmL).pitch = 0.16F;
    at(a, FigureBone::ForearmR).pitch = 0.16F;
    at(a, FigureBone::HandL).pitch = 0.08F;
    at(a, FigureBone::HandR).pitch = 0.08F;
    at(a, FigureBone::ThighL).pitch = 0.02F;
    at(a, FigureBone::ThighR).pitch = -0.02F;
    return a;
}

[[nodiscard]] Angles walkAngles(float t, float stride) noexcept {
    Angles a;
    const float s = std::sin(kTwoPi * t);
    const float c = std::cos(kTwoPi * t);
    const float legAmp = 0.44F * stride;
    at(a, FigureBone::ThighL).pitch = legAmp * s;
    at(a, FigureBone::ThighR).pitch = -legAmp * s;
    at(a, FigureBone::ShinL).pitch = -(0.08F + 0.80F * stride * std::max(0.0F, c));
    at(a, FigureBone::ShinR).pitch = -(0.08F + 0.80F * stride * std::max(0.0F, -c));
    at(a, FigureBone::FootL).pitch = -0.7F * (at(a, FigureBone::ThighL).pitch + at(a, FigureBone::ShinL).pitch);
    at(a, FigureBone::FootR).pitch = -0.7F * (at(a, FigureBone::ThighR).pitch + at(a, FigureBone::ShinR).pitch);
    at(a, FigureBone::UpperArmL).pitch = -0.42F * s * stride;
    at(a, FigureBone::UpperArmR).pitch = 0.42F * s * stride;
    at(a, FigureBone::UpperArmL).roll = -0.06F;
    at(a, FigureBone::UpperArmR).roll = 0.06F;
    at(a, FigureBone::ForearmL).pitch = 0.20F + 0.30F * std::max(0.0F, -s);
    at(a, FigureBone::ForearmR).pitch = 0.20F + 0.30F * std::max(0.0F, s);
    at(a, FigureBone::HandL).pitch = 0.08F;
    at(a, FigureBone::HandR).pitch = 0.08F;
    at(a, FigureBone::Torso).yaw = 0.10F * s;
    at(a, FigureBone::Pelvis).yaw = -0.08F * s;
    at(a, FigureBone::Head).yaw = -0.02F * s;
    at(a, FigureBone::Torso).pitch = -0.04F;
    at(a, FigureBone::Head).pitch = 0.04F;
    at(a, FigureBone::Pelvis).roll = 0.025F * c;
    a.root.y = -0.075F * stride * s * s;
    return a;
}

/// One punch, `side` +1 for the right hand and -1 for the left, looping: the
/// wind-up, the strike, the hold, the return to guard.
[[nodiscard]] Angles punchAngles(float t, float side) noexcept {
    Angles a;
    float wind = 0.0F;
    float ext = 0.0F;
    if (t < 0.30F) {
        wind = smooth01(t / 0.30F);
    } else if (t < 0.42F) {
        const float k = smooth01((t - 0.30F) / 0.12F);
        wind = 1.0F - k;
        ext = k;
    } else if (t < 0.55F) {
        ext = 1.0F;
    } else {
        ext = 1.0F - smooth01((t - 0.55F) / 0.45F);
    }
    const float guard = std::max(0.0F, 1.0F - ext - wind);
    const FigureBone upS = side > 0.0F ? FigureBone::UpperArmR : FigureBone::UpperArmL;
    const FigureBone foreS = side > 0.0F ? FigureBone::ForearmR : FigureBone::ForearmL;
    const FigureBone upG = side > 0.0F ? FigureBone::UpperArmL : FigureBone::UpperArmR;
    const FigureBone foreG = side > 0.0F ? FigureBone::ForearmL : FigureBone::ForearmR;
    at(a, upS).pitch = 0.65F * guard - 0.55F * wind + 1.45F * ext;
    at(a, foreS).pitch = 1.75F * guard + 1.95F * wind + 0.18F * ext;
    at(a, upS).roll = side * (0.10F * guard + 0.15F * wind) * -1.0F;
    at(a, upG).pitch = 0.75F;
    at(a, foreG).pitch = 1.85F;
    at(a, upG).roll = side * 0.28F;
    at(a, FigureBone::Torso).yaw = side * (0.55F * ext - 0.28F * wind);
    at(a, FigureBone::Pelvis).yaw = side * (0.22F * ext - 0.10F * wind);
    at(a, FigureBone::Head).yaw = -side * (0.30F * ext - 0.15F * wind);
    at(a, FigureBone::Torso).pitch = -0.10F;
    at(a, FigureBone::Head).pitch = 0.10F;
    // The stance: the off foot forward, knees soft.
    at(a, FigureBone::ThighL).pitch = side > 0.0F ? 0.28F : -0.22F;
    at(a, FigureBone::ThighR).pitch = side > 0.0F ? -0.22F : 0.28F;
    at(a, FigureBone::ShinL).pitch = -0.22F;
    at(a, FigureBone::ShinR).pitch = -0.22F;
    at(a, FigureBone::FootL).pitch = -0.04F;
    at(a, FigureBone::FootR).pitch = -0.04F;
    a.root.y = -0.045F;
    return a;
}

[[nodiscard]] Angles blockAngles(float t) noexcept {
    Angles a;
    const float tremor = 0.02F * std::sin(kTwoPi * t * 3.0F);
    at(a, FigureBone::UpperArmL).pitch = 0.95F + tremor;
    at(a, FigureBone::UpperArmR).pitch = 0.95F - tremor;
    at(a, FigureBone::UpperArmL).roll = 0.38F;
    at(a, FigureBone::UpperArmR).roll = -0.38F;
    at(a, FigureBone::ForearmL).pitch = 2.10F;
    at(a, FigureBone::ForearmR).pitch = 2.10F;
    at(a, FigureBone::Torso).pitch = -0.16F;
    at(a, FigureBone::Head).pitch = 0.14F;
    at(a, FigureBone::ThighL).pitch = 0.20F;
    at(a, FigureBone::ThighR).pitch = -0.16F;
    at(a, FigureBone::ShinL).pitch = -0.20F;
    at(a, FigureBone::ShinR).pitch = -0.20F;
    a.root.y = -0.04F;
    return a;
}

/// The conversation: the head nods and turns, the right hand talks, the left
/// rests and lifts now and then.
[[nodiscard]] Angles talkAngles(float t) noexcept {
    Angles a = idleAngles(t);
    const float g1 = std::sin(kTwoPi * t * 3.0F);
    const float g2 = std::sin(kTwoPi * t * 3.0F + 0.9F);
    const float slow = std::sin(kTwoPi * t);
    at(a, FigureBone::Head).yaw = 0.10F * slow;
    at(a, FigureBone::Head).pitch = 0.05F * std::sin(kTwoPi * t * 6.0F);
    at(a, FigureBone::Torso).pitch = -0.03F;
    at(a, FigureBone::Torso).yaw = -0.05F * slow;
    at(a, FigureBone::UpperArmR).pitch = 0.58F + 0.12F * g1;
    at(a, FigureBone::UpperArmR).roll = 0.16F + 0.05F * g2;
    at(a, FigureBone::ForearmR).pitch = 1.10F + 0.35F * g2;
    at(a, FigureBone::HandR).pitch = 0.20F * g1;
    at(a, FigureBone::HandR).roll = 0.15F * g2;
    const float lift = std::max(0.0F, std::sin(kTwoPi * t * 2.0F - 1.2F));
    at(a, FigureBone::UpperArmL).pitch = 0.10F + 0.40F * lift;
    at(a, FigureBone::ForearmL).pitch = 0.45F + 0.70F * lift;
    return a;
}

[[nodiscard]] Angles hitAngles(float u) noexcept {
    Angles a = idleAngles(0.0F);
    const float r = std::sin(kPi * std::min(1.0F, u * 2.6F)) * (1.0F - smooth01((u - 0.4F) / 0.6F));
    at(a, FigureBone::Torso).pitch = 0.46F * r;
    at(a, FigureBone::Head).pitch = 0.50F * r;
    at(a, FigureBone::UpperArmL).pitch = -0.30F * r;
    at(a, FigureBone::UpperArmR).pitch = -0.30F * r;
    at(a, FigureBone::UpperArmL).roll = -0.07F - 0.45F * r;
    at(a, FigureBone::UpperArmR).roll = 0.07F + 0.45F * r;
    at(a, FigureBone::Pelvis).pitch = -0.10F * r;
    a.root.z = 0.05F * r;
    return a;
}

/// Where a body struck down ends: sitting on the floor, dazed, forward over
/// its knees. What the ward's floored bodies are drawn in.
[[nodiscard]] Angles recoverFinal() noexcept {
    Angles a;
    // Legs out along the floor, knees soft, the torso slumped over them.
    at(a, FigureBone::ThighL).pitch = 1.50F;
    at(a, FigureBone::ThighR).pitch = 1.50F;
    at(a, FigureBone::ThighL).roll = -0.12F;
    at(a, FigureBone::ThighR).roll = 0.12F;
    at(a, FigureBone::ShinL).pitch = 0.04F;
    at(a, FigureBone::ShinR).pitch = 0.04F;
    at(a, FigureBone::FootL).pitch = -1.40F;
    at(a, FigureBone::FootR).pitch = -1.40F;
    at(a, FigureBone::Torso).pitch = -0.42F;
    at(a, FigureBone::Head).pitch = -0.30F;
    at(a, FigureBone::UpperArmL).pitch = 0.55F;
    at(a, FigureBone::UpperArmR).pitch = 0.55F;
    at(a, FigureBone::UpperArmL).roll = -0.12F;
    at(a, FigureBone::UpperArmR).roll = 0.12F;
    at(a, FigureBone::ForearmL).pitch = 0.7F;
    at(a, FigureBone::ForearmR).pitch = 0.7F;
    a.root.y = -(kHipY - 0.14F);
    return a;
}

/// Where a body slain ends: flat on its back, the arms out, the head turned.
[[nodiscard]] Angles deathFinal() noexcept {
    Angles a;
    at(a, FigureBone::Pelvis).pitch = 1.5708F;
    at(a, FigureBone::Head).yaw = 0.45F;
    at(a, FigureBone::Head).pitch = -0.10F;
    at(a, FigureBone::UpperArmL).roll = -0.55F;
    at(a, FigureBone::UpperArmR).roll = 0.55F;
    at(a, FigureBone::UpperArmL).pitch = 0.25F;
    at(a, FigureBone::UpperArmR).pitch = 0.25F;
    at(a, FigureBone::ForearmL).pitch = 0.25F;
    at(a, FigureBone::ForearmR).pitch = 0.25F;
    at(a, FigureBone::ThighL).roll = -0.10F;
    at(a, FigureBone::ThighR).roll = 0.10F;
    at(a, FigureBone::FootL).pitch = -0.2F;
    at(a, FigureBone::FootR).pitch = -0.2F;
    a.root.y = -(kHipY - 0.115F);
    return a;
}

[[nodiscard]] Angles anglesFor(ActorClip clip, std::uint32_t frame, float stride) noexcept {
    const std::uint32_t length = figureClipLength(clip);
    const bool oneShot = actorClipOneShot(clip);
    const std::uint32_t at = oneShot ? std::min(frame, length) : frame % length;
    const float t = static_cast<float>(at) / static_cast<float>(length);
    switch (clip) {
        case ActorClip::Walk: return walkAngles(t, stride);
        case ActorClip::PunchRight: return punchAngles(t, 1.0F);
        case ActorClip::PunchLeft: return punchAngles(t, -1.0F);
        case ActorClip::Block: return blockAngles(t);
        case ActorClip::Hit: return hitAngles(t);
        case ActorClip::Recover: return lerpAngles(idleAngles(0.0F), recoverFinal(), smooth01(t));
        case ActorClip::Death: return lerpAngles(idleAngles(0.0F), deathFinal(), smooth01(t));
        case ActorClip::Talk: return talkAngles(t);
        case ActorClip::Idle:
        default: return idleAngles(t);
    }
}

[[nodiscard]] FigurePose forward(const Vec3* rest, const Angles& a) noexcept {
    FigurePose pose;
    for (std::size_t b = 0; b < kFigureBoneCount; ++b) {
        const Local& l = a.j[b];
        const Mat3 local = shapes::euler(l.pitch, l.yaw, l.roll);
        if (b == 0) {
            pose.rotation[0] = local;
            pose.joint[0] = Vec3{rest[0].x + a.root.x, rest[0].y + a.root.y, rest[0].z + a.root.z};
            continue;
        }
        const std::size_t p = static_cast<std::size_t>(figureParentOf(static_cast<FigureBone>(b)));
        pose.rotation[b] = shapes::mul(pose.rotation[p], local);
        const Vec3 off = shapes::apply(pose.rotation[p], Vec3{rest[b].x - rest[p].x, rest[b].y - rest[p].y, rest[b].z - rest[p].z});
        pose.joint[b] = Vec3{pose.joint[p].x + off.x, pose.joint[p].y + off.y, pose.joint[p].z + off.z};
    }
    return pose;
}

}  // namespace

std::uint32_t figureClipLength(ActorClip clip) noexcept {
    switch (clip) {
        case ActorClip::Walk: return 52;
        case ActorClip::PunchLeft:
        case ActorClip::PunchRight: return 40;
        case ActorClip::Block: return 90;
        case ActorClip::Hit: return 30;
        case ActorClip::Recover: return 45;
        case ActorClip::Death: return 40;
        case ActorClip::Talk: return 150;
        case ActorClip::Idle:
        default: return 240;
    }
}

FigurePose evaluateFigurePose(const Vec3* restJoints, ActorClip clip, std::uint32_t frame) noexcept {
    return forward(restJoints, anglesFor(clip, frame, restJoints[kFigureBoneCount].x));
}

FigurePose figureRestPose(const Vec3* restJoints) noexcept { return forward(restJoints, Angles{}); }

void skinFigure(const float* restPositions, const float* restNormals, const std::uint8_t* bones,
                std::size_t vertexCount, const Vec3* restJoints, const FigurePose& pose,
                float* positionsOut, float* normalsOut) noexcept {
    for (std::size_t v = 0; v < vertexCount; ++v) {
        const std::size_t b = std::min<std::size_t>(bones[v], kFigureBoneCount - 1);
        const Mat3& r = pose.rotation[b];
        const Vec3& rest = restJoints[b];
        const Vec3& joint = pose.joint[b];
        const float x = restPositions[v * 3] - rest.x;
        const float y = restPositions[v * 3 + 1] - rest.y;
        const float z = restPositions[v * 3 + 2] - rest.z;
        positionsOut[v * 3] = joint.x + r.m[0] * x + r.m[1] * y + r.m[2] * z;
        positionsOut[v * 3 + 1] = joint.y + r.m[3] * x + r.m[4] * y + r.m[5] * z;
        positionsOut[v * 3 + 2] = joint.z + r.m[6] * x + r.m[7] * y + r.m[8] * z;
        const float nx = restNormals[v * 3];
        const float ny = restNormals[v * 3 + 1];
        const float nz = restNormals[v * 3 + 2];
        normalsOut[v * 3] = r.m[0] * nx + r.m[1] * ny + r.m[2] * nz;
        normalsOut[v * 3 + 1] = r.m[3] * nx + r.m[4] * ny + r.m[5] * nz;
        normalsOut[v * 3 + 2] = r.m[6] * nx + r.m[7] * ny + r.m[8] * nz;
    }
}

void skinFigure(const MeshData& rest, const FigurePose& pose, std::vector<float>& positions,
                std::vector<float>& normals) {
    const std::size_t count = rest.vertexCount();
    positions.assign(count * 3, 0.0F);
    normals.assign(count * 3, 0.0F);
    if (rest.bones.size() != count || rest.boneRest.size() < (kFigureBoneCount + 1) * 3) {
        return;
    }
    std::array<Vec3, kFigureBoneCount + 1> joints{};
    for (std::size_t i = 0; i < joints.size(); ++i) {
        joints[i] = Vec3{rest.boneRest[i * 3], rest.boneRest[i * 3 + 1], rest.boneRest[i * 3 + 2]};
    }
    skinFigure(rest.positions.data(), rest.normals.data(), rest.bones.data(), count, joints.data(), pose,
               positions.data(), normals.data());
}

}  // namespace granadad::render3d
