// The figure tables (how tall a body of each kind stands, which figure a tavern role wears) and
// the lens's own line-of-sight walk.
#include "session_internal.hpp"

namespace granadad::render {

// THE FIGURE TABLES ARE PUBLIC (3D BUILD, A lane): render3d's actor instancer
// draws the same sixteen kinds of body out of the same two tables, so a
// bouncer is the Watch's build in 3D exactly as he is on the sheet.

/// How tall a body of each kind stands, in tiles, and the width its drawing is
/// scaled to at that height.
///
/// A PERSON IS 1.875 TILES SOLE TO CROWN, which is kStandingHeightTilesQ8 out
/// of sim/vertical_scale.hpp -- the same header the player's own eye height
/// comes from. At roughly 0.9 m to the tile that is a 1.71 m adult and the
/// crown lands a hand above the player's 1.70-tile eye, so you look people in
/// the mouth. That is correct, and it is the cheapest proof the two scales
/// agree.
///
/// The WIDTH is not the drawing's own aspect. The sheet's figures are chunky
/// -- twelve texels of shoulder in a sixteen-texel cell -- and taken literally
/// that is a person a tile and a half across, standing in a collision square
/// 0.7 of a tile wide. So the height is the truth and the width is scaled to
/// put shoulders at about half a tile, which is a person shaped like a person.
FigureScale figureScaleOf(sim::WardType type) noexcept {
    switch (type) {
        case sim::WardType::MilitiaWatch: return {1.95F, 0.86F};
        case sim::WardType::Urchin: return {1.30F, 0.58F};
        case sim::WardType::Thief: return {1.80F, 0.72F};
        case sim::WardType::PriestOfTheFlame: return {1.90F, 0.84F};
        case sim::WardType::DiscipleOfTheFlame: return {1.82F, 0.80F};
        case sim::WardType::Dog: return {0.62F, 0.90F};
        case sim::WardType::Stray: return {0.46F, 0.66F};
        case sim::WardType::Cat: return {0.42F, 0.62F};
        case sim::WardType::Mouse: return {0.22F, 0.34F};
        default: return {1.875F, 0.80F};
    }
}

/// WHICH DRAWN FIGURE A TAVERN ROLE WEARS.
///
/// #78 REPLACED THE ELLIPSE STACK, and it is worth recording what it replaced
/// and why. A tavern actor used to be three stacked ellipses -- legs, torso,
/// head -- with a pale patch for a face. That was the right first step and its
/// own header said so: a narrow stack of three reads as a person from across a
/// room where one blob reads as an egg. What it could not do is survive being
/// MULTIPLIED. The owner captured a conversation frame, looked at the figure
/// standing in the Gilded Gull's doorway, and called it what it was: a plain
/// egg shape with a round head, which will not read as polished with hundreds
/// of them on a street.
///
/// content/art/sprites was already in the repo -- twenty-five 16x16 figures in
/// the MERCOLAS-24 palette, front-on, with a helmet on the guard, white and red
/// on the priest, a hood on the vagrant and a hat on the merchant. Those are
/// silhouettes a player can tell apart at eight tiles in lamplight, which is
/// the whole visual target, and somebody had already drawn them.
///
/// So the taproom's fourteen are drawn out of the same sheet the ward's six
/// hundred are, and there is ONE look for a person in this game rather than two.
sim::WardType figureForRole(sim::ActorRole role, std::int32_t id) noexcept {
    switch (role) {
        case sim::ActorRole::Bartender:
        case sim::ActorRole::Innkeeper:
            return sim::WardType::Shopkeeper;
        // A bouncer is hired for his shoulders, and the sheet's guard is the
        // only figure on it built like that.
        case sim::ActorRole::Bouncer:
            return sim::WardType::MilitiaWatch;
        case sim::ActorRole::PriestOfTheFlame:
            return sim::WardType::PriestOfTheFlame;
        // Grey on grey, keeping to the corner, hard to pick out. The sheet's
        // ragged vagrant is exactly that and the Skyrunners dress like it.
        case sim::ActorRole::SkyrunnerContact:
            return sim::WardType::Thief;
        case sim::ActorRole::Vermin:
            return sim::WardType::Mouse;
        case sim::ActorRole::Patron:
        default:
            // THE GULL IS THE CAPTAINS' TAVERN -- charts on the walls, factors
            // doing deals, the good wine (DOCKS-GAZETTEER K03) -- and it is on
            // a working quay. So the room is a mix and which half a patron is
            // in is a pure function of his id, because a room where everybody
            // wears the same coat is the defect this is fixing.
            return (id & 1) == 0 ? sim::WardType::Shopkeeper : sim::WardType::Serf;
    }
}

/// Whether the LENS at one Q8 point has a clear line to a body at another --
/// the question a shot composed on somebody has to ask, and NOT the one
/// TileQuery::lineOfSight answers. The sim's rule is a supercover walk
/// between two TILE CENTRES that lets a ray slip through a lattice corner
/// when only one of the two cells it grazes is solid: the right rule for
/// "can he see me" between people, and exactly the wrong one for a camera,
/// which sits at a Q8 point and draws every solid cell as a full-height
/// block. The voice lane's watch reveal was a picture of the taproom table
/// at (151,69) for this reason: Cull could see the player past its corner by
/// the sim's rule, and the frame showed its west face.
///
/// So: a grid walk (Amanatides-Woo) from the exact eye to the exact body,
/// every cell the segment enters asked of solid(), a corner crossing blocked
/// if EITHER grazed cell is solid, and the walk run three times -- down the
/// middle and along two lines a quarter tile either side of it. `whole`
/// asks for all three, a body half a tile across in view and not a hair of
/// it (what a stand is COMPOSED on); without it the middle and either side
/// will do, which is "can you see them" for a body walking along a wall.
/// The endpoints' own cells never block: a body stands in a cell, it is
/// not one. Render side, doubles legal, nothing here is hashed or written.
bool cameraSightClear(const sim::TileQuery& tiles, std::int32_t fromXQ8, std::int32_t fromYQ8,
                      std::int32_t toXQ8, std::int32_t toYQ8, std::int32_t band,
                      bool whole) noexcept {
    const auto walk = [&](double ax, double ay, double bx, double by) {
        const double dx = bx - ax;
        const double dy = by - ay;
        std::int32_t cx = static_cast<std::int32_t>(std::floor(ax));
        std::int32_t cy = static_cast<std::int32_t>(std::floor(ay));
        const std::int32_t ex = static_cast<std::int32_t>(std::floor(bx));
        const std::int32_t ey = static_cast<std::int32_t>(std::floor(by));
        const std::int32_t stepX = dx > 0.0 ? 1 : (dx < 0.0 ? -1 : 0);
        const std::int32_t stepY = dy > 0.0 ? 1 : (dy < 0.0 ? -1 : 0);
        constexpr double kFar = 1.0e18;
        const double tDeltaX = stepX != 0 ? 1.0 / std::abs(dx) : kFar;
        const double tDeltaY = stepY != 0 ? 1.0 / std::abs(dy) : kFar;
        double tMaxX = stepX > 0 ? (static_cast<double>(cx + 1) - ax) * tDeltaX
                       : stepX < 0 ? (ax - static_cast<double>(cx)) * tDeltaX
                                   : kFar;
        double tMaxY = stepY > 0 ? (static_cast<double>(cy + 1) - ay) * tDeltaY
                       : stepY < 0 ? (ay - static_cast<double>(cy)) * tDeltaY
                                   : kFar;
        // Bounded by the cells the segment can cross, plus one for each
        // corner it might take diagonally.
        for (std::int32_t guard = std::abs(ex - cx) + std::abs(ey - cy) + 2; guard > 0; --guard) {
            if (cx == ex && cy == ey) {
                return true;
            }
            constexpr double kCorner = 1.0e-9;
            if (std::abs(tMaxX - tMaxY) < kCorner) {
                // Through a lattice corner: a shut corner is a wall to a
                // lens whichever cell of the two is the solid one.
                if (tiles.solid(cx + stepX, cy, band) || tiles.solid(cx, cy + stepY, band)) {
                    return false;
                }
                cx += stepX;
                cy += stepY;
                tMaxX += tDeltaX;
                tMaxY += tDeltaY;
            } else if (tMaxX < tMaxY) {
                cx += stepX;
                tMaxX += tDeltaX;
            } else {
                cy += stepY;
                tMaxY += tDeltaY;
            }
            if (cx == ex && cy == ey) {
                return true;
            }
            if (tiles.solid(cx, cy, band)) {
                return false;
            }
        }
        return cx == ex && cy == ey;
    };
    const double ax = static_cast<double>(fromXQ8) / static_cast<double>(sim::kSubOne);
    const double ay = static_cast<double>(fromYQ8) / static_cast<double>(sim::kSubOne);
    const double bx = static_cast<double>(toXQ8) / static_cast<double>(sim::kSubOne);
    const double by = static_cast<double>(toYQ8) / static_cast<double>(sim::kSubOne);
    const double dx = bx - ax;
    const double dy = by - ay;
    const double length = std::sqrt(dx * dx + dy * dy);
    if (length < 1.0e-6) {
        return true;
    }
    // A quarter tile either side, perpendicular to the line: the width of
    // the body the shot is about.
    constexpr double kHalfBody = 0.25;
    const double nx = -dy / length * kHalfBody;
    const double ny = dx / length * kHalfBody;
    if (!walk(ax, ay, bx, by)) {
        return false;
    }
    const bool left = walk(ax + nx, ay + ny, bx + nx, by + ny);
    const bool right = walk(ax - nx, ay - ny, bx - nx, by - ny);
    return whole ? (left && right) : (left || right);
}

}  // namespace granadad::render
