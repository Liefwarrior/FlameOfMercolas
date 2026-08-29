#include "granadad/render/signage_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "granadad/render/hud.hpp"
#include "granadad/sim/docks_signs.hpp"

namespace granadad::render {

namespace {

/// Above the floor of the level the sign's building stands on. ABSOLUTE tile
/// heights, the same convention every other billboard in this renderer uses
/// (see world_renderer.cpp's lamp bracket and tavern flame heights) -- a
/// storey is kBandHeight (3) tiles now, and a sign nailed above a doorway
/// does not get taller when the ceiling does.
///
///   2.35  a door sign / shop sign, mounted over the lintel
///   2.05  a street post, lower -- it reads from further along the road, not
///         from standing under it
constexpr float kDoorSignHeight = 2.35F;
constexpr float kWaySignHeight = 2.05F;

/// How much slack a projected label's screen box is allowed to overlap
/// another already-drawn one before it is skipped. A few pixels, not zero --
/// two labels whose boxes brush by a pixel of drop-shadow are not actually
/// unreadable, and demanding zero overlap would drop legible signs for no
/// reason on a crowded block like the Tarwalk quay apron.
constexpr int kOverlapSlackPx = 2;

struct ScreenRect {
    int x0, y0, x1, y1;
};

[[nodiscard]] bool overlaps(const ScreenRect& a, const ScreenRect& b) noexcept {
    return a.x0 < b.x1 + kOverlapSlackPx && b.x0 < a.x1 + kOverlapSlackPx &&
           a.y0 < b.y1 + kOverlapSlackPx && b.y0 < a.y1 + kOverlapSlackPx;
}

struct Candidate {
    std::string_view text;
    float worldX, worldY, worldZ;
    float distance;
};

}  // namespace

void drawSignage(Framebuffer& target, const Camera& camera, const SignageSettings& settings) {
    const int width = target.width();
    const int height = target.height();
    const Projection projection = projectionFor(camera, width, height);
    const float focal = projection.focal;
    const float horizon = projection.horizon;
    const float halfWidth = static_cast<float>(width) * 0.5F;

    const float forwardX = std::sin(camera.yaw);
    const float forwardY = -std::cos(camera.yaw);
    const float rightX = std::cos(camera.yaw);
    const float rightY = std::sin(camera.yaw);

    // --- gather every sign that is in range and facing the camera --------
    std::vector<Candidate> candidates;
    candidates.reserve(sim::docks::kSignCount);
    for (const sim::docks::Sign& sign : sim::docks::kSigns) {
        const float relX = sign.anchorX - camera.x;
        const float relY = sign.anchorY - camera.y;
        const float distance = relX * forwardX + relY * forwardY;
        if (distance < settings.minDistance || distance > settings.maxDistance) {
            continue;  // behind the eye, or too far to read
        }
        const sim::docks::ResolvedSignLabel resolved = sim::docks::resolveSignLabel(sign);
        const float signHeight =
            sign.kind == sim::docks::SignKind::Door ? kDoorSignHeight : kWaySignHeight;
        candidates.push_back(Candidate{resolved.text, sign.anchorX, sign.anchorY,
                                       bandSurface(sign.band) + signHeight, distance});
    }

    // Nearest first: when two labels would collide on screen the nearer
    // (more relevant, more legible) one gets the pixels.
    std::sort(candidates.begin(), candidates.end(),
             [](const Candidate& a, const Candidate& b) { return a.distance < b.distance; });

    const int scale = std::max(1, hudMinorScale(height));
    const int glyphH = 6 * scale;

    std::vector<ScreenRect> drawn;
    drawn.reserve(candidates.size() + 1);
    // THE INTERFACE GOES IN FIRST, as just another box already on the screen.
    // Everything below it is the rule this file already had; it simply could
    // not see the HUD before. See SignageSettings::exclusion.
    if (settings.exclusion.x1 > settings.exclusion.x0 &&
        settings.exclusion.y1 > settings.exclusion.y0) {
        drawn.push_back(ScreenRect{settings.exclusion.x0, settings.exclusion.y0,
                                   settings.exclusion.x1, settings.exclusion.y1});
    }

    for (const Candidate& candidate : candidates) {
        const float relX = candidate.worldX - camera.x;
        const float relY = candidate.worldY - camera.y;
        const float lateral = relX * rightX + relY * rightY;
        const float centreX = halfWidth + lateral * focal / candidate.distance;
        const float centreY =
            horizon - (candidate.worldZ - camera.z) * focal / candidate.distance;

        // ---- occlusion: reuse the depth buffer the world pass wrote -----
        //
        // Sampled at the label's own anchor pixel rather than its (wider)
        // text box: the box is centred on the anchor, so the anchor pixel is
        // the one point guaranteed to be either "on the building" or "just
        // in front of it" for every sign in the table.
        const int sampleX = static_cast<int>(std::lround(centreX));
        const int sampleY = static_cast<int>(std::lround(centreY));
        if (!target.contains(sampleX, sampleY)) {
            continue;  // projects off the edge of the frame entirely
        }
        const float pixelDepth = target.depth()[target.index(sampleX, sampleY)];
        if (!std::isfinite(pixelDepth)) {
            continue;  // raw sky here -- the building is not actually in view
        }
        // A little slack (0.4 tile) for the sign standing slightly in front
        // of or behind the wall face it is nailed to -- without it, a sign
        // anchored exactly on its own building's front wall is judged
        // "behind" that same wall by float rounding and never draws.
        if (pixelDepth < candidate.distance - 0.4F) {
            continue;  // something nearer (another wall) is in front of it
        }

        // ---- fade out over the last stretch before maxDistance ----------
        const float fadeStart = std::max(settings.minDistance, settings.maxDistance - settings.fadeSpan);
        const float alpha =
            candidate.distance <= fadeStart
                ? 1.0F
                : std::clamp(1.0F - (candidate.distance - fadeStart) / settings.fadeSpan, 0.0F, 1.0F);
        if (alpha <= 0.02F) {
            continue;
        }

        // ---- anti-overlap: nearest-first, skip a box that collides ------
        const int textW = textWidth(candidate.text, scale);
        const ScreenRect box{
            static_cast<int>(std::lround(centreX)) - textW / 2,
            static_cast<int>(std::lround(centreY)) - glyphH / 2,
            static_cast<int>(std::lround(centreX)) + textW / 2,
            static_cast<int>(std::lround(centreY)) + glyphH / 2,
        };
        // A candidate entirely off-screen on the left/right does not compete
        // for space with anything and costs nothing to skip early.
        if (box.x1 < 0 || box.x0 > width || box.y1 < 0 || box.y0 > height) {
            continue;
        }
        bool collides = false;
        for (const ScreenRect& already : drawn) {
            if (overlaps(box, already)) {
                collides = true;
                break;
            }
        }
        if (collides) {
            continue;
        }

        // HARDENING PASS. A SIGN'S BARE TEXT AGAINST ITS OWN BUILDING IS
        // EXACTLY THE S8 "NES POP-UP" PROBLEM, THE WORLD-SPACE HALF OF IT.
        // A real capture confirmed "THE BILGE" nearly unreadable painted
        // straight over a similarly-toned wall behind it, with nothing but
        // the engine's generic 1px drop shadow between the two. Backed now
        // by the identical plate the alert row uses -- see drawTextPlate's
        // own header on where its exact black/bone constants come from --
        // but kept DELIBERATELY LIGHTWEIGHT: a 1-pixel border and a hair of
        // padding, not the full pop-up's 8px+ margins, because a busy block
        // can have several of these on screen at once and they already fade
        // by distance (`alpha`, reused here so the plate recedes with the
        // text it backs rather than staying opaque after the words have
        // faded out).
        drawTextPlate(target, box.x0 - scale, box.y0 - 1, box.x1 + scale, box.y1 + 1, 1, alpha);
        constexpr Rgb kSignInk{0.93F, 0.87F, 0.68F};  // warm parchment, reads on stone and sky alike
        drawText(target, box.x0, box.y0, candidate.text, kSignInk, alpha, scale);
        drawn.push_back(box);
    }
}

}  // namespace granadad::render
