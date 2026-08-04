#pragma once

// The 27 authored light sources of the Docks, and how they stop being a
// runtime read of a 1.8 MB authored art file.
//
// WHERE THEY LIVE AND WHY THAT WAS A DEFECT
//
// The lamps are authored in Tiled as `<object type="light_source">` markers on
// the `markers` object layer of content/maps/src/docks_surface.tmx, each with a
// `luminance` property in 0..31. TiledWorldImporter parses that layer and does
// not bake it, so the light sources are simply not in the TROJSAV the game
// loads. The Java client worked around that by StAX-scanning the authored .tmx
// at boot -- LampMarkersLoader, whose own javadoc ends "When marker baking
// lands in the TROJSAV, delete this class and read them from the save."
// ARCHITECTURE.md section 0.1 names it as the real defect behind the lighting
// story: a shipped build without content/maps/src degrades silently to zero
// lamps.
//
// So they are BAKED, once, by granadad-bake-lamps, into
// content/maps/baked/<world>.lamps.json beside the world it belongs to. The
// game reads that; it never opens a .tmx. The scanner that produced it is in
// this library and is tested, so the bake is reproducible from canon rather
// than being a hand-typed table.
//
// (Baking into the TROJSAV itself would be better still, and is not available:
// content/ is the owner's read-only canon and the C++ has no TROJSAV writer --
// Java's Deflater level 1 and miniz's do not emit the same bytes, so a rewritten
// save could not be byte-compared against the original. A sidecar beside the
// world is the honest version of "baked" that this build can actually do.)
//
// COORDINATES. A marker's pixel (x, y) inside a `z:<sign><n>` group becomes the
// world tile (32 + floor(x/16), 32 + floor(y/16), 8 + (z - minZ)) -- the
// importer's own placement rule, with the one-chunk VOID border offset applied.
// Integers throughout; the float part of lighting starts in the renderer.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace granadad::render {

/// How a light source reads: an open flame or a shielded lantern. Classed from
/// the marker's name, exactly as the Java did, because Tiled carries no other
/// field for it.
enum class LampWarmth : std::uint8_t {
    /// Lanterns, hanging lamps, door lamps: a cooler, whiter pool.
    Lantern = 0,
    /// Braziers, cauldrons, ovens, torches, candles, hearths: ember-orange.
    Fire = 1,
};

[[nodiscard]] std::string_view lampWarmthName(LampWarmth warmth) noexcept;

/// One authored light source, in world tiles.
struct Lamp {
    std::string name;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
    /// Authored 0..31. The renderer maps it to radius and peak.
    std::int32_t luminance = 0;
    LampWarmth warmth = LampWarmth::Lantern;
};

/// Marker-name tokens that class a source as an open flame.
[[nodiscard]] bool isFireLampName(std::string_view markerName) noexcept;

/// Scans a Tiled .tmx for its light_source markers and returns them in world
/// tiles, in document order.
///
/// Deliberately a tiny hand-rolled tag scanner rather than an XML library: the
/// only thing this needs to understand is elements and attributes, the file is
/// repo-local canon, and adding a parser dependency to read one object layer
/// would be the tail wagging the dog. It never resolves an entity and never
/// follows a DTD, so there is no external-entity surface to harden.
[[nodiscard]] std::vector<Lamp> scanTmxLightSources(std::string_view tmx);

/// Reads a whole file and scans it. Throws std::runtime_error if unreadable.
[[nodiscard]] std::vector<Lamp> scanTmxFile(const std::filesystem::path& tmxFile);

/// Serialises lamps to the baked sidecar's JSON text, with a trailing newline.
/// Byte-stable: same lamps in, same bytes out, on any platform.
[[nodiscard]] std::string writeLampBake(const std::string& worldName,
                                        const std::string& sourceRelativePath,
                                        const std::vector<Lamp>& lamps);

/// Parses the baked sidecar. Throws std::runtime_error on a malformed file.
[[nodiscard]] std::vector<Lamp> readLampBake(std::string_view json);

/// content/maps/baked/<world>.lamps.json.
[[nodiscard]] std::filesystem::path lampBakePath(const std::filesystem::path& contentDir,
                                                 const std::string& worldName);

/// Loads the sidecar for a world. Returns an empty list, and never throws, when
/// the file is absent -- a missing presentation nicety must not stop the game
/// booting, which is the one thing the Java loader got exactly right.
[[nodiscard]] std::vector<Lamp> loadLamps(const std::filesystem::path& contentDir,
                                          const std::string& worldName);

}  // namespace granadad::render
