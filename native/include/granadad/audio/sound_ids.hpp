#pragma once

// The logical sound vocabulary — every noise this game can make, as an id.
//
// The WIRING (session.cpp, main.cpp — a later pass, see audio_engine.hpp)
// speaks only these ids. Which CC0 file under content/art/kenney-all-in-1/Audio
// a given id resolves to is the sound bank's business (sound_bank.cpp), so
// swapping an asset is a one-line manifest edit and no call site moves.
//
// TES-restrained on purpose: Morrowind's UI says almost nothing and what it
// says is quiet. The Ui* family here maps to the softest of the Kenney
// interface sets, and nothing in this enum exists so a menu can beep more.
//
// STRICTLY CLIENT-SIDE. Nothing under granadad/audio/ may be included by
// native/src/sim or the gate targets. Audio observes the sim; the sim must
// never be able to observe audio (see audio_engine.hpp's determinism note).

#include <cstddef>
#include <cstdint>

namespace granadad::audio {

/// Every one-shot the engine can play. Contiguous from zero; kSoundIdCount
/// closes the range so tables and tests can iterate the whole vocabulary.
enum class SoundId : std::uint8_t {
    // -- footsteps, one per Surface (see audio_engine.hpp) -------------------
    FootstepStone = 0,
    FootstepWood,
    FootstepEarth,
    FootstepCloth,
    FootstepIce,
    // -- water ---------------------------------------------------------------
    WadeSplash,  ///< layered under a footstep when fluidDepth > 0
    WaterDrip,   ///< sparse harbour/undercroft one-shot
    // -- UI (restrained; Interface Sounds + UI Audio) ------------------------
    UiClick,
    UiBack,
    UiConfirm,
    UiError,
    UiOpen,
    UiClose,
    UiSelect,
    UiTick,
    UiToggle,
    UiQuestion,
    UiScroll,
    // -- diegetic world sounds (RPG Audio) -----------------------------------
    BookOpen,   ///< casebook / spellbook / letters
    BookClose,
    BookFlip,
    CoinHandle,  ///< barter, purse
    DoorOpen,
    DoorClose,
    Creak,
    MetalLatch,
    MetalClick,
    KnifeDraw,
    KnifeSlice,
    Chop,
    ClothRustle,  ///< dodge, garment, pickpocket
    // -- combat (Impact Sounds + Foley) --------------------------------------
    PunchMedium,
    PunchHeavy,
    ThudMedium,  ///< body blow landing soft
    ThudHeavy,
    GrazeLight,
    Whoosh,     ///< a swing that connects with nothing
    SwordDraw,
    SwordClash,
    SwordHit,
    HelmetHit,
    HarbourBell,  ///< also an ambience sparse — the Docks' own bell
};

inline constexpr std::size_t kSoundIdCount = 42;

[[nodiscard]] constexpr std::size_t soundIndex(SoundId id) noexcept {
    return static_cast<std::size_t>(id);
}

/// Mixer buses. Master scales everything; the rest are the settings-screen
/// sliders the wiring pass will eventually expose.
enum class Bus : std::uint8_t {
    Master = 0,
    Ambient,
    Footsteps,
    Ui,
    World,
    Combat,
};

inline constexpr std::size_t kBusCount = 6;

[[nodiscard]] constexpr std::size_t busIndex(Bus bus) noexcept {
    return static_cast<std::size_t>(bus);
}

/// Which bus a one-shot lands on by default.
[[nodiscard]] Bus busFor(SoundId id) noexcept;

/// Ambient beds — a running mood, not a one-shot. The vendored Kenney set has
/// no looping ambience recordings at all (surveyed: zero gull/wave/crowd/wind
/// files), so v1 beds are PROCEDURAL: filtered-noise water-lap and wind layers
/// synthesized in the mixer, plus sparse randomized one-shots (drips, the
/// harbour bell). When real CC0 harbour loops are vendored later they slot
/// into the same bed table as loop layers — see audio_engine.cpp's BedDef.
enum class BedId : std::uint8_t {
    None = 0,
    Harbour,   ///< outdoors in the Docks: water-lap + wind + drips + the bell
    Interior,  ///< under a roof: faint wind, settling creaks
};

inline constexpr std::size_t kBedCount = 3;

/// Footstep surface classes. Five, matching the five Kenney footstep sets.
/// Every material registry id maps to one of these — audio_engine.cpp owns the
/// 22-entry table and test_audio_engine.cpp pins it against
/// render::materialIds() so a new material raw fails loudly, not silently.
enum class Surface : std::uint8_t {
    Stone = 0,  ///< granite, brick, facades, reman_concrete, light/glow-stone, steel
    Wood,       ///< oak, trudgeon_wood (soaked included)
    Earth,      ///< dirt, ash, thatch, phorys
    Cloth,      ///< cloth, leather
    Ice,        ///< ice — the snow set reads right for it
};

inline constexpr std::size_t kSurfaceCount = 5;

}  // namespace granadad::audio
