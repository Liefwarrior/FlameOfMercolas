#pragma once

// THE PEOPLE, AS PIXELS THE OWNER DREW.
//
// Until now a body in this game was three stacked ellipses and a pale patch
// where its face would be. That was the right first step -- a narrow stack of
// three reads as a person from across a room where one blob reads as an egg --
// but it does not survive being multiplied. The owner captured a frame and said
// so: at hundreds of figures on a street, every one of them the same lozenge in
// a different brown, "polished" is not the word anybody would use.
//
// content/art/sprites is the answer and it was already in the repo:
// twenty-five 16x16 actor sprites in the MERCOLAS-24 palette, drawn front-on,
// with a tag vocabulary and a query per Java actor type. A guard has a helmet
// and a red tabard. A priest is white and red head to foot. A vagrant has a
// hood. A merchant has a hat. Those are silhouettes a player can tell apart at
// eight tiles in lamplight, which is the whole visual target, and they exist.
//
// SO THE ART IS REUSED AND THE JAVA CODE IS NOT. This is a flat texel store and
// a tag query -- about a hundred and fifty lines -- and not a port of
// client-observer's SpriteIndex. The index file's `actorQueries` block is read
// as authored: a type names a tag set, and every sprite carrying all of those
// tags is a variant of it. Which variant a given actor gets is a pure function
// of its id, so nobody flickers between two coats as they walk.
//
// WHEN THE PACK IS NOT THERE the sheet falls back to a procedural silhouette
// per role -- built from the same body proportions the ellipse stack used, so a
// checkout with no art tree still renders people rather than nothing. Ugly and
// legible, in that order, exactly like TileAtlas's fallback.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/render/framebuffer.hpp"
#include "granadad/sim/ward_actors.hpp"

namespace granadad::render {

/// One 16x16 figure, RGBA, row-major, top row first. Alpha is a CUTOUT and not
/// a blend: a texel is either the figure or it is the street behind it, which
/// is what keeps a body hard-edged at this resolution instead of fringed.
struct ActorSprite {
    static constexpr int kPx = 16;
    static constexpr std::size_t kTexels = kPx * kPx;
    std::uint32_t texels[kTexels] = {};
    /// Rows of the 16 that carry any opaque texel at all, so the billboard can
    /// be sized to the FIGURE rather than to its padding -- a sprite drawn 14
    /// rows tall inside a 16-row cell stands two rows above the pavement
    /// otherwise.
    int firstRow = 0;
    int lastRow = kPx - 1;
    int firstCol = 0;
    int lastCol = kPx - 1;
};

/// The owner's actor art, indexed by ward type.
class ActorSheet {
public:
    /// Loads content/art/sprites. Never throws: a missing pack yields the
    /// procedural fallback, because a content edit must not stop the game.
    [[nodiscard]] static ActorSheet load(const std::filesystem::path& contentDir);

    /// The procedural pack, with no files involved.
    [[nodiscard]] static ActorSheet procedural();

    [[nodiscard]] bool fromAuthoredArt() const noexcept { return fromAuthoredArt_; }

    /// How many distinct sprites were read out of the sheet. Pinned by a case:
    /// a loader that is silent about a missing file lets the whole visual claim
    /// pass against the fallback.
    [[nodiscard]] std::size_t spriteCount() const noexcept { return sprites_.size(); }

    /// The figure this actor wears. `variantKey` picks between the pack's
    /// authored variants and must be a pure function of the actor, so a body
    /// does not change coat as it walks.
    [[nodiscard]] const ActorSprite& forType(sim::WardType type,
                                             std::uint32_t variantKey) const noexcept;

    /// How many variants a type has. Zero means the fallback is in use for it.
    [[nodiscard]] std::size_t variantsFor(sim::WardType type) const noexcept;

private:
    void buildFallback();

    std::vector<ActorSprite> sprites_;
    /// Per type, the indices into sprites_ that may be used.
    std::vector<std::vector<std::size_t>> byType_;
    bool fromAuthoredArt_ = false;
};

/// The tag query each ward type answers to, as authored in
/// content/art/sprites/sprite-index.json's `actorQueries` block.
///
/// SIXTEEN TYPES AGAINST ELEVEN AUTHORED QUERIES, and the extra five are the
/// ones the owner's complaint named. An urchin queries the vagrant's tags with
/// `ragged` required, so it is drawn as the smallest hooded figure on the
/// sheet; a thief queries them too and is scaled down and darkened rather than
/// given art nobody drew. Inventing sprite ids that are not in the index would
/// be this build authoring art, which it does not do.
[[nodiscard]] std::string_view actorSheetQuery(sim::WardType type) noexcept;

}  // namespace granadad::render
