#pragma once

// The name-resolution table the signage survey designed: what a Docks
// building's or street's sign actually SAYS, and how confident that is.
//
// docks_signs_generated.hpp is DATA -- the 83 `place_sign` markers a mapper
// already authored in content/maps/src/docks_surface.tmx, transformed into
// world tile coordinates by tools/scripts/gen_docks_signs.py. This file is
// the LOGIC: the priority order the survey settled on, in one place, so a
// caller never has to decide for itself which source wins.
//
// THE PRIORITY ORDER, checked in this sequence:
//
//   1. docks::kPlaces      a real, hand-authored building/street name that
//                          was ALREADY in this codebase before the signage
//                          survey (docks.hpp). Wins outright when it covers
//                          the sign's own tile -- see THE GILDED GULL, which
//                          is in both tables and this is why it does not
//                          double up.
//   2. the .tmx sign itself the `place` property on the marker, recovered by
//                          the survey. This is what fires for 39 of the 40
//                          door signs and all 43 way signs: a real name that
//                          was sitting in the source map, unused, before now.
//   3. PlotRaw correlation  compound.hpp's PlotRaw::name, matched by the
//                          survey to four of the .tmx sign ids by a shared
//                          prefix (c1_/c2_/c3_/c4_ <-> C1_QUAYWARD etc). NOT
//                          WIRED HERE: the survey's own Part 3 table found
//                          this fires as the PRIMARY source for zero of the
//                          40 doors, because every one of them already has a
//                          tier-2 tmx name and tier 2 wins first. Wiring a
//                          compounds.json load into the render path to win a
//                          contest it cannot win is exactly the speculative
//                          abstraction this codebase's own rules warn against
//                          -- so tier 3 is documented and skipped rather than
//                          built. If a future door genuinely loses its tier-2
//                          name and DOES have a correlated PlotRaw id, this is
//                          the tier to wire it into.
//   4. function-derived     a label reasoned from the `what` flavour text
//                          (kind alone, when even that is missing). Dead code
//                          today -- every one of the 40 doors has a real
//                          name -- kept wired as the safety net the survey
//                          recommended for a door the tile-GID scan gap might
//                          still turn up.
//
// SignSourceTier exists so a caller (or a test, or a report) can be honest
// about which of the four actually fired, rather than blurring "real name"
// and "we made something up" into one string.

#include <string_view>

#include "granadad/sim/docks.hpp"
#include "granadad/sim/docks_signs_generated.hpp"

namespace granadad::sim::docks {

enum class SignSourceTier {
    /// Tier 1: docks::kPlaces already had it.
    KnownPlace,
    /// Tier 2: recovered from the .tmx `place_sign` marker itself.
    TmxRecovered,
    /// Tier 3: PlotRaw correlation. Never actually returned today -- see the
    /// header above -- kept in the enum so the priority order is complete.
    PlotCorrelated,
    /// Tier 4: nothing real was found; reasoned from what little is known.
    Synthesized,
};

struct ResolvedSignLabel {
    std::string_view text;
    SignSourceTier tier;
};

/// What tier 4 says when even the flavour text is empty -- SHOP / TAVERN /
/// WAREHOUSE / HOUSE / GUARDHOUSE is the kind of function-derived guess the
/// task brief asked for, but nothing in the 40 authored doors ever needs it:
/// every one of them carries a `what` line, so this only fires for a `way`
/// marker missing both `place` and `what`, which the generated table does
/// not currently contain either.
[[nodiscard]] inline std::string_view functionFallbackLabel(SignKind kind) noexcept {
    return kind == SignKind::Door ? "BUILDING" : "WAY";
}

/// True when a docks::kPlaces entry names this DOOR's own building, rather
/// than a street or area the door merely happens to sit inside.
///
/// THE SIGNAL IS THE RECTANGLE ITSELF, not a hand-maintained list of street
/// names. kPlaces mixes two authoring intents that placeNameAt's point-in-
/// rect check cannot tell apart: THE GILDED GULL's entry (docks.hpp) was
/// authored to cover exactly that one building, and its rect (146,66)-
/// (160,79) is a byte-for-byte match for sign_k03_gilded_gull's own tmx
/// footprint -- because it IS the same building, described twice. TARWALK,
/// ROPEWYND, SALTGATE RISE, GALLOWS ROW and THE LONG PIERS, by contrast, are
/// each authored to span a whole street or reach -- every one of their rects
/// is far larger than any single door's footprint, so none of them can ever
/// coincide with one by accident. A door's footprint rect matching a
/// kPlaces rect exactly is therefore proof the two entries name the same
/// structure; a door's footprint merely falling inside a much bigger kPlaces
/// rect is proof of the opposite. Verified against every door in
/// docks_signs_generated.hpp: the Gull is the only exact match today, and a
/// future kPlaces entry that genuinely names one building (authored the same
/// way the Gull's was) keeps working here with no list to update.
[[nodiscard]] inline bool kPlacesNamesThisDoor(const Sign& sign) noexcept {
    for (std::size_t i = 0; i < kPlaceCount; ++i) {
        const Place& place = kPlaces[i];
        if (place.band == sign.band && place.x0 == sign.x0 && place.y0 == sign.y0 &&
            place.x1 == sign.x1 && place.y1 == sign.y1) {
            return true;
        }
    }
    return false;
}

/// The one place that decides what a Sign's label actually is, in priority
/// order. Never empty.
///
/// TIER 1 IS CHECKED AGAINST THE FOOTPRINT'S CENTRE, NOT THE SIGN'S OWN
/// ANCHOR. A door sign's anchor is deliberately planted just outside the
/// building, on the street the sign faces (sign_k03_gilded_gull's anchor is
/// (153.5, 65.5); the Gull's own footprint starts at y0=66) -- exactly right
/// for where to DRAW the label, exactly wrong for asking kPlaces "whose
/// building is this", which is a question about the footprint, not the
/// doorstep.
///
/// FOR A DOOR SIGN, A TIER-1 HIT ONLY WINS WHEN IT NAMES THAT SAME DOOR'S
/// BUILDING -- see kPlacesNamesThisDoor. Without that check, any door whose
/// footprint centre merely falls inside one of kPlaces' broad street/area
/// rects (TARWALK, ROPEWYND, SALTGATE RISE, GALLOWS ROW, THE LONG PIERS) had
/// its own real tmx name silently overwritten by the street it stands on --
/// Merle's Boats reading THE LONG PIERS, Saltgate Watch-Post reading GALLOWS
/// ROW, and six more like them. Way signs are unaffected: a `way` marker's
/// whole job is to say what street it is standing on, so the street name
/// winning there is correct, not a bug.
[[nodiscard]] inline ResolvedSignLabel resolveSignLabel(const Sign& sign) noexcept {
    const std::int32_t centreX = (sign.x0 + sign.x1) / 2;
    const std::int32_t centreY = (sign.y0 + sign.y1) / 2;
    const std::string_view known = placeNameAt(centreX, centreY, sign.band);
    if (!known.empty() && (sign.kind == SignKind::Way || kPlacesNamesThisDoor(sign))) {
        return {known, SignSourceTier::KnownPlace};
    }
    if (sign.place != nullptr && sign.place[0] != '\0') {
        return {std::string_view{sign.place}, SignSourceTier::TmxRecovered};
    }
    // Tier 3 (PlotRaw correlation) intentionally not consulted -- see this
    // file's own header.
    if (sign.what != nullptr && sign.what[0] != '\0') {
        return {std::string_view{sign.what}, SignSourceTier::Synthesized};
    }
    return {functionFallbackLabel(sign.kind), SignSourceTier::Synthesized};
}

}  // namespace granadad::sim::docks
