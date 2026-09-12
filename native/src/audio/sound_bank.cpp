// See sound_bank.hpp. The manifests below name REAL files — every entry was
// verified against the vendored tree when it was written, and
// test_audio_engine.cpp re-verifies existence on any checkout that has the
// Kenney packs (verify-windows.ps1's native run always does), and the LOT
// rows likewise wherever content/art/lot/audio is staged. Variant counts
// are NOT uniform: e.g. the Interface set ships tick_001, _002 and _004 with
// no _003, which is why these are explicit file lists and never a
// printf-pattern over an assumed range.

#include "granadad/audio/sound_bank.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <optional>

#include "granadad/audio/decode.hpp"

namespace granadad::audio {

namespace {

using sv = std::string_view;

// --- footsteps: Impact Sounds, 5 surfaces x 5 variants ----------------------
constexpr sv kFootStone[] = {
    "Impact Sounds/Audio/footstep_concrete_000.ogg",
    "Impact Sounds/Audio/footstep_concrete_001.ogg",
    "Impact Sounds/Audio/footstep_concrete_002.ogg",
    "Impact Sounds/Audio/footstep_concrete_003.ogg",
    "Impact Sounds/Audio/footstep_concrete_004.ogg",
};
constexpr sv kFootWood[] = {
    "Impact Sounds/Audio/footstep_wood_000.ogg",
    "Impact Sounds/Audio/footstep_wood_001.ogg",
    "Impact Sounds/Audio/footstep_wood_002.ogg",
    "Impact Sounds/Audio/footstep_wood_003.ogg",
    "Impact Sounds/Audio/footstep_wood_004.ogg",
};
constexpr sv kFootEarth[] = {
    "Impact Sounds/Audio/footstep_grass_000.ogg",
    "Impact Sounds/Audio/footstep_grass_001.ogg",
    "Impact Sounds/Audio/footstep_grass_002.ogg",
    "Impact Sounds/Audio/footstep_grass_003.ogg",
    "Impact Sounds/Audio/footstep_grass_004.ogg",
};
constexpr sv kFootCloth[] = {
    "Impact Sounds/Audio/footstep_carpet_000.ogg",
    "Impact Sounds/Audio/footstep_carpet_001.ogg",
    "Impact Sounds/Audio/footstep_carpet_002.ogg",
    "Impact Sounds/Audio/footstep_carpet_003.ogg",
    "Impact Sounds/Audio/footstep_carpet_004.ogg",
};
constexpr sv kFootIce[] = {
    "Impact Sounds/Audio/footstep_snow_000.ogg",
    "Impact Sounds/Audio/footstep_snow_001.ogg",
    "Impact Sounds/Audio/footstep_snow_002.ogg",
    "Impact Sounds/Audio/footstep_snow_003.ogg",
    "Impact Sounds/Audio/footstep_snow_004.ogg",
};

// --- water: Foley Sounds ----------------------------------------------------
constexpr sv kWadeSplash[] = {
    "Foley Sounds/Audio/Water/sinkWater1.ogg",
    "Foley Sounds/Audio/Water/sinkWater2.ogg",
    "Foley Sounds/Audio/Water/sinkWater3.ogg",
    "Foley Sounds/Audio/Water/sinkWater4.ogg",
};
constexpr sv kWaterDrip[] = {
    "Foley Sounds/Audio/Water/drip1.ogg",
    "Foley Sounds/Audio/Water/drip2.ogg",
    "Foley Sounds/Audio/Water/drip3.ogg",
    "Foley Sounds/Audio/Water/drip4.ogg",
};

// --- UI: Interface Sounds (TES-restrained set) ------------------------------
constexpr sv kUiClick[] = {
    "Interface Sounds/Audio/click_001.ogg", "Interface Sounds/Audio/click_002.ogg",
    "Interface Sounds/Audio/click_003.ogg", "Interface Sounds/Audio/click_004.ogg",
    "Interface Sounds/Audio/click_005.ogg",
};
constexpr sv kUiBack[] = {
    "Interface Sounds/Audio/back_001.ogg", "Interface Sounds/Audio/back_002.ogg",
    "Interface Sounds/Audio/back_003.ogg", "Interface Sounds/Audio/back_004.ogg",
};
constexpr sv kUiConfirm[] = {
    "Interface Sounds/Audio/confirmation_001.ogg",
    "Interface Sounds/Audio/confirmation_002.ogg",
    "Interface Sounds/Audio/confirmation_003.ogg",
    "Interface Sounds/Audio/confirmation_004.ogg",
};
constexpr sv kUiError[] = {
    "Interface Sounds/Audio/error_001.ogg", "Interface Sounds/Audio/error_002.ogg",
    "Interface Sounds/Audio/error_003.ogg", "Interface Sounds/Audio/error_004.ogg",
    "Interface Sounds/Audio/error_005.ogg", "Interface Sounds/Audio/error_006.ogg",
    "Interface Sounds/Audio/error_007.ogg", "Interface Sounds/Audio/error_008.ogg",
};
constexpr sv kUiOpen[] = {
    "Interface Sounds/Audio/open_001.ogg", "Interface Sounds/Audio/open_002.ogg",
    "Interface Sounds/Audio/open_003.ogg", "Interface Sounds/Audio/open_004.ogg",
};
constexpr sv kUiClose[] = {
    "Interface Sounds/Audio/close_001.ogg", "Interface Sounds/Audio/close_002.ogg",
    "Interface Sounds/Audio/close_003.ogg", "Interface Sounds/Audio/close_004.ogg",
};
constexpr sv kUiSelect[] = {
    "Interface Sounds/Audio/select_001.ogg", "Interface Sounds/Audio/select_002.ogg",
    "Interface Sounds/Audio/select_003.ogg", "Interface Sounds/Audio/select_004.ogg",
    "Interface Sounds/Audio/select_005.ogg", "Interface Sounds/Audio/select_006.ogg",
    "Interface Sounds/Audio/select_007.ogg", "Interface Sounds/Audio/select_008.ogg",
};
constexpr sv kUiTick[] = {
    // The set really has no tick_003 — the gap is upstream's, not a typo here.
    "Interface Sounds/Audio/tick_001.ogg",
    "Interface Sounds/Audio/tick_002.ogg",
    "Interface Sounds/Audio/tick_004.ogg",
};
constexpr sv kUiToggle[] = {
    "Interface Sounds/Audio/toggle_001.ogg", "Interface Sounds/Audio/toggle_002.ogg",
    "Interface Sounds/Audio/toggle_003.ogg", "Interface Sounds/Audio/toggle_004.ogg",
};
constexpr sv kUiQuestion[] = {
    "Interface Sounds/Audio/question_001.ogg",
    "Interface Sounds/Audio/question_002.ogg",
    "Interface Sounds/Audio/question_003.ogg",
    "Interface Sounds/Audio/question_004.ogg",
};
constexpr sv kUiScroll[] = {
    "Interface Sounds/Audio/scroll_001.ogg", "Interface Sounds/Audio/scroll_002.ogg",
    "Interface Sounds/Audio/scroll_003.ogg", "Interface Sounds/Audio/scroll_004.ogg",
    "Interface Sounds/Audio/scroll_005.ogg",
};

// --- diegetic: RPG Audio ----------------------------------------------------
constexpr sv kBookOpen[] = {"RPG Audio/Audio/bookOpen.ogg"};
constexpr sv kBookClose[] = {"RPG Audio/Audio/bookClose.ogg"};
constexpr sv kBookFlip[] = {
    "RPG Audio/Audio/bookFlip1.ogg",
    "RPG Audio/Audio/bookFlip2.ogg",
    "RPG Audio/Audio/bookFlip3.ogg",
};
constexpr sv kCoinHandle[] = {
    "RPG Audio/Audio/handleCoins.ogg",
    "RPG Audio/Audio/handleCoins2.ogg",
};
constexpr sv kDoorOpen[] = {
    "RPG Audio/Audio/doorOpen_1.ogg",
    "RPG Audio/Audio/doorOpen_2.ogg",
};
constexpr sv kDoorClose[] = {
    "RPG Audio/Audio/doorClose_1.ogg", "RPG Audio/Audio/doorClose_2.ogg",
    "RPG Audio/Audio/doorClose_3.ogg", "RPG Audio/Audio/doorClose_4.ogg",
};
constexpr sv kCreak[] = {
    "RPG Audio/Audio/creak1.ogg",
    "RPG Audio/Audio/creak2.ogg",
    "RPG Audio/Audio/creak3.ogg",
};
constexpr sv kMetalLatch[] = {"RPG Audio/Audio/metalLatch.ogg"};
constexpr sv kMetalClick[] = {"RPG Audio/Audio/metalClick.ogg"};
constexpr sv kKnifeDraw[] = {
    "RPG Audio/Audio/drawKnife1.ogg",
    "RPG Audio/Audio/drawKnife2.ogg",
    "RPG Audio/Audio/drawKnife3.ogg",
};
constexpr sv kKnifeSlice[] = {
    "RPG Audio/Audio/knifeSlice.ogg",
    "RPG Audio/Audio/knifeSlice2.ogg",
};
constexpr sv kChop[] = {"RPG Audio/Audio/chop.ogg"};
constexpr sv kClothRustle[] = {
    "RPG Audio/Audio/cloth1.ogg", "RPG Audio/Audio/cloth2.ogg",
    "RPG Audio/Audio/cloth3.ogg", "RPG Audio/Audio/cloth4.ogg",
};

// --- combat: Impact Sounds + Foley ------------------------------------------
constexpr sv kPunchMedium[] = {
    "Impact Sounds/Audio/impactPunch_medium_000.ogg",
    "Impact Sounds/Audio/impactPunch_medium_001.ogg",
    "Impact Sounds/Audio/impactPunch_medium_002.ogg",
    "Impact Sounds/Audio/impactPunch_medium_003.ogg",
    "Impact Sounds/Audio/impactPunch_medium_004.ogg",
};
constexpr sv kPunchHeavy[] = {
    "Impact Sounds/Audio/impactPunch_heavy_000.ogg",
    "Impact Sounds/Audio/impactPunch_heavy_001.ogg",
    "Impact Sounds/Audio/impactPunch_heavy_002.ogg",
    "Impact Sounds/Audio/impactPunch_heavy_003.ogg",
    "Impact Sounds/Audio/impactPunch_heavy_004.ogg",
};
constexpr sv kThudMedium[] = {
    "Impact Sounds/Audio/impactSoft_medium_000.ogg",
    "Impact Sounds/Audio/impactSoft_medium_001.ogg",
    "Impact Sounds/Audio/impactSoft_medium_002.ogg",
    "Impact Sounds/Audio/impactSoft_medium_003.ogg",
    "Impact Sounds/Audio/impactSoft_medium_004.ogg",
};
constexpr sv kThudHeavy[] = {
    "Impact Sounds/Audio/impactSoft_heavy_000.ogg",
    "Impact Sounds/Audio/impactSoft_heavy_001.ogg",
    "Impact Sounds/Audio/impactSoft_heavy_002.ogg",
    "Impact Sounds/Audio/impactSoft_heavy_003.ogg",
    "Impact Sounds/Audio/impactSoft_heavy_004.ogg",
};
constexpr sv kGrazeLight[] = {
    "Impact Sounds/Audio/impactGeneric_light_000.ogg",
    "Impact Sounds/Audio/impactGeneric_light_001.ogg",
    "Impact Sounds/Audio/impactGeneric_light_002.ogg",
    "Impact Sounds/Audio/impactGeneric_light_003.ogg",
    "Impact Sounds/Audio/impactGeneric_light_004.ogg",
};
constexpr sv kWhoosh[] = {
    "Foley Sounds/Audio/Woosh/woosh1.ogg", "Foley Sounds/Audio/Woosh/woosh2.ogg",
    "Foley Sounds/Audio/Woosh/woosh3.ogg", "Foley Sounds/Audio/Woosh/woosh4.ogg",
    "Foley Sounds/Audio/Woosh/woosh5.ogg", "Foley Sounds/Audio/Woosh/woosh6.ogg",
    "Foley Sounds/Audio/Woosh/woosh7.ogg", "Foley Sounds/Audio/Woosh/woosh8.ogg",
};
constexpr sv kSwordDraw[] = {
    "Foley Sounds/Audio/Swords/swordSlide1.ogg",
    "Foley Sounds/Audio/Swords/swordSlide2.ogg",
    "Foley Sounds/Audio/Swords/swordSlide3.ogg",
};
constexpr sv kSwordClash[] = {
    "Foley Sounds/Audio/Swords/swordMetal1.ogg",
    "Foley Sounds/Audio/Swords/swordMetal2.ogg",
    "Foley Sounds/Audio/Swords/swordMetal3.ogg",
    "Foley Sounds/Audio/Swords/swordMetal4.ogg",
    "Foley Sounds/Audio/Swords/swordMetal5.ogg",
    "Foley Sounds/Audio/Swords/swordMetal6.ogg",
    "Foley Sounds/Audio/Swords/swordMetal7.ogg",
};
constexpr sv kSwordHit[] = {
    "Foley Sounds/Audio/Swords/sword1.ogg", "Foley Sounds/Audio/Swords/sword2.ogg",
    "Foley Sounds/Audio/Swords/sword3.ogg", "Foley Sounds/Audio/Swords/sword4.ogg",
    "Foley Sounds/Audio/Swords/sword5.ogg", "Foley Sounds/Audio/Swords/sword6.ogg",
    "Foley Sounds/Audio/Swords/sword7.ogg", "Foley Sounds/Audio/Swords/sword8.ogg",
    "Foley Sounds/Audio/Swords/sword9.ogg", "Foley Sounds/Audio/Swords/sword10.ogg",
    "Foley Sounds/Audio/Swords/sword11.ogg",
};
constexpr sv kHelmetHit[] = {
    "Foley Sounds/Audio/Swords/hitHelmet1.ogg",
    "Foley Sounds/Audio/Swords/hitHelmet2.ogg",
    "Foley Sounds/Audio/Swords/hitHelmet3.ogg",
    "Foley Sounds/Audio/Swords/hitHelmet4.ogg",
    "Foley Sounds/Audio/Swords/hitHelmet5.ogg",
};
constexpr sv kHarbourBell[] = {
    "Impact Sounds/Audio/impactBell_heavy_000.ogg",
    "Impact Sounds/Audio/impactBell_heavy_001.ogg",
    "Impact Sounds/Audio/impactBell_heavy_002.ogg",
    "Impact Sounds/Audio/impactBell_heavy_003.ogg",
    "Impact Sounds/Audio/impactBell_heavy_004.ogg",
};

// --- THE LOT PASS: Kenney stand-ins for the ids it added -------------------
// So a checkout without content/art/lot/audio (the docker gate, the
// standalone pack) still speaks where a Kenney sound reads right. Footstep
// stand-ins reuse the surface each material used to land on; the sheathe is
// a belt being handled and a closed case is a book set down -- restrained,
// per the TES note at the top of sound_ids.hpp. PlayerHurt, PlayerDown and
// the three bed loops have no honest stand-in and stay silent there.
constexpr sv kSheatheKenney[] = {
    "RPG Audio/Audio/beltHandle1.ogg",
    "RPG Audio/Audio/beltHandle2.ogg",
    "RPG Audio/Audio/clothBelt.ogg",
    "RPG Audio/Audio/clothBelt2.ogg",
};
constexpr sv kCaseClosedKenney[] = {
    "RPG Audio/Audio/bookPlace1.ogg",
    "RPG Audio/Audio/bookPlace2.ogg",
    "RPG Audio/Audio/bookPlace3.ogg",
};
// THE PULL PACK. CaseNews: the book changed -- a page turning, restrained,
// so the plate's one cue never reads as a fanfare.
constexpr sv kCaseNewsKenney[] = {
    "RPG Audio/Audio/bookFlip1.ogg",
    "RPG Audio/Audio/bookFlip2.ogg",
    "RPG Audio/Audio/bookFlip3.ogg",
};

// ---------------------------------------------------------------------------
// THE LOT PASS: the second root (kLotAudioRootRel = content/art/lot/audio).
// Every row here was staged by tools/lot-pipeline/lot-audio-import.ps1 and is
// listed with its measured peak in docs/asset-manifest-lot.md. The gain column
// is that manifest's gainDb intent, set from the peaks: anything hotter than
// -1 dBFS is trimmed down to it (the five hot masters land at -3..-4 dB), the
// two quiet Malbers stance sounds are lifted (+10 / +8), everything else is 0.
// Baked into the PCM at load (applyGainDb), so the mixer carries no per-row
// gain. Explicit lists, never a printf-pattern, exactly like the Kenney rows.
// ---------------------------------------------------------------------------

// FootstepStone: stone: Footsteps Pack concrete, 12 (Kenney 5 stand in)
constexpr LotSoundFile kLotFootStone[] = {
    {"footsteps/concrete/concrete_01.ogg",            0.0F},  // peak -6.9 dBFS
    {"footsteps/concrete/concrete_02.ogg",            0.0F},  // peak -5.5 dBFS
    {"footsteps/concrete/concrete_03.ogg",            0.0F},  // peak -13.9 dBFS
    {"footsteps/concrete/concrete_04.ogg",            0.0F},  // peak -5.6 dBFS
    {"footsteps/concrete/concrete_05.ogg",            0.0F},  // peak -3.9 dBFS
    {"footsteps/concrete/concrete_06.ogg",            0.0F},  // peak -6.3 dBFS
    {"footsteps/concrete/concrete_07.ogg",            0.0F},  // peak -1.3 dBFS
    {"footsteps/concrete/concrete_08.ogg",            0.0F},  // peak -4.6 dBFS
    {"footsteps/concrete/concrete_09.ogg",            0.0F},  // peak -6.2 dBFS
    {"footsteps/concrete/concrete_10.ogg",            0.0F},  // peak -11.5 dBFS
    {"footsteps/concrete/concrete_11.ogg",            0.0F},  // peak -5.1 dBFS
    {"footsteps/concrete/concrete_12.ogg",            0.0F},  // peak -2.5 dBFS
};
// FootstepEarth: earth: Footsteps Pack earthground, 12 (Kenney grass stands in)
constexpr LotSoundFile kLotFootEarth[] = {
    {"footsteps/earthground/earthground_01.ogg",      0.0F},  // peak -6.3 dBFS
    {"footsteps/earthground/earthground_02.ogg",      0.0F},  // peak -10.6 dBFS
    {"footsteps/earthground/earthground_03.ogg",      0.0F},  // peak -7.0 dBFS
    {"footsteps/earthground/earthground_04.ogg",      0.0F},  // peak -10.3 dBFS
    {"footsteps/earthground/earthground_05.ogg",      0.0F},  // peak -5.4 dBFS
    {"footsteps/earthground/earthground_06.ogg",      0.0F},  // peak -8.9 dBFS
    {"footsteps/earthground/earthground_07.ogg",      0.0F},  // peak -7.4 dBFS
    {"footsteps/earthground/earthground_08.ogg",      0.0F},  // peak -5.9 dBFS
    {"footsteps/earthground/earthground_09.ogg",      0.0F},  // peak -10.4 dBFS
    {"footsteps/earthground/earthground_10.ogg",     -1.9F},  // peak +0.9 dBFS
    {"footsteps/earthground/earthground_11.ogg",      0.0F},  // peak -9.3 dBFS
    {"footsteps/earthground/earthground_12.ogg",      0.0F},  // peak -7.5 dBFS
};
// FootstepIce: ice: Footsteps Pack ice-and-snow, 12 (Kenney snow stands in)
constexpr LotSoundFile kLotFootIce[] = {
    {"footsteps/iceandsnow/iceandsnow_01.ogg",        0.0F},  // peak -3.3 dBFS
    {"footsteps/iceandsnow/iceandsnow_02.ogg",        0.0F},  // peak -2.0 dBFS
    {"footsteps/iceandsnow/iceandsnow_03.ogg",        0.0F},  // peak -4.5 dBFS
    {"footsteps/iceandsnow/iceandsnow_04.ogg",       -0.5F},  // peak -0.5 dBFS
    {"footsteps/iceandsnow/iceandsnow_05.ogg",        0.0F},  // peak -2.8 dBFS
    {"footsteps/iceandsnow/iceandsnow_06.ogg",       -0.2F},  // peak -0.8 dBFS
    {"footsteps/iceandsnow/iceandsnow_07.ogg",        0.0F},  // peak -3.2 dBFS
    {"footsteps/iceandsnow/iceandsnow_08.ogg",        0.0F},  // peak -1.1 dBFS
    {"footsteps/iceandsnow/iceandsnow_09.ogg",       -0.3F},  // peak -0.7 dBFS
    {"footsteps/iceandsnow/iceandsnow_10.ogg",        0.0F},  // peak -4.0 dBFS
    {"footsteps/iceandsnow/iceandsnow_11.ogg",        0.0F},  // peak -2.8 dBFS
    {"footsteps/iceandsnow/iceandsnow_12.ogg",        0.0F},  // peak -2.3 dBFS
};
// WadeSplash: wading: Footsteps Pack water, 12 (Kenney sinkWater stands in)
constexpr LotSoundFile kLotWadeSplash[] = {
    {"footsteps/water/water_01.ogg",                  0.0F},  // peak -14.7 dBFS
    {"footsteps/water/water_02.ogg",                  0.0F},  // peak -7.8 dBFS
    {"footsteps/water/water_03.ogg",                  0.0F},  // peak -2.5 dBFS
    {"footsteps/water/water_04.ogg",                 -0.9F},  // peak -0.1 dBFS
    {"footsteps/water/water_05.ogg",                  0.0F},  // peak -1.5 dBFS
    {"footsteps/water/water_06.ogg",                  0.0F},  // peak -8.8 dBFS
    {"footsteps/water/water_07.ogg",                  0.0F},  // peak -2.1 dBFS
    {"footsteps/water/water_08.ogg",                 -0.9F},  // peak -0.1 dBFS
    {"footsteps/water/water_09.ogg",                 -1.4F},  // peak +0.4 dBFS
    {"footsteps/water/water_10.ogg",                 -0.9F},  // peak -0.1 dBFS
    {"footsteps/water/water_11.ogg",                  0.0F},  // peak -7.8 dBFS
    {"footsteps/water/water_12.ogg",                  0.0F},  // peak -1.7 dBFS
};
// FootstepMetal: metal: NEW surface, 12 (Kenney concrete stands in)
constexpr LotSoundFile kLotFootMetal[] = {
    {"footsteps/metal/metal_01.ogg",                  0.0F},  // peak -3.0 dBFS
    {"footsteps/metal/metal_02.ogg",                  0.0F},  // peak -4.3 dBFS
    {"footsteps/metal/metal_03.ogg",                  0.0F},  // peak -2.8 dBFS
    {"footsteps/metal/metal_04.ogg",                 -2.6F},  // peak +1.6 dBFS
    {"footsteps/metal/metal_05.ogg",                  0.0F},  // peak -2.3 dBFS
    {"footsteps/metal/metal_06.ogg",                  0.0F},  // peak -4.0 dBFS
    {"footsteps/metal/metal_07.ogg",                  0.0F},  // peak -4.0 dBFS
    {"footsteps/metal/metal_08.ogg",                  0.0F},  // peak -4.1 dBFS
    {"footsteps/metal/metal_09.ogg",                 -2.3F},  // peak +1.3 dBFS
    {"footsteps/metal/metal_10.ogg",                  0.0F},  // peak -3.8 dBFS
    {"footsteps/metal/metal_11.ogg",                  0.0F},  // peak -4.5 dBFS
    {"footsteps/metal/metal_12.ogg",                  0.0F},  // peak -4.0 dBFS
};
// FootstepGravel: gravel: NEW surface, 12 (Kenney concrete stands in)
constexpr LotSoundFile kLotFootGravel[] = {
    {"footsteps/gravel/gravel_01.ogg",                0.0F},  // peak -22.1 dBFS
    {"footsteps/gravel/gravel_02.ogg",                0.0F},  // peak -16.1 dBFS
    {"footsteps/gravel/gravel_03.ogg",                0.0F},  // peak -5.0 dBFS
    {"footsteps/gravel/gravel_04.ogg",                0.0F},  // peak -10.3 dBFS
    {"footsteps/gravel/gravel_05.ogg",                0.0F},  // peak -5.0 dBFS
    {"footsteps/gravel/gravel_06.ogg",                0.0F},  // peak -14.7 dBFS
    {"footsteps/gravel/gravel_07.ogg",                0.0F},  // peak -6.0 dBFS
    {"footsteps/gravel/gravel_08.ogg",                0.0F},  // peak -5.1 dBFS
    {"footsteps/gravel/gravel_09.ogg",                0.0F},  // peak -11.0 dBFS
    {"footsteps/gravel/gravel_10.ogg",                0.0F},  // peak -3.9 dBFS
    {"footsteps/gravel/gravel_11.ogg",                0.0F},  // peak -15.3 dBFS
    {"footsteps/gravel/gravel_12.ogg",                0.0F},  // peak -7.6 dBFS
};
// FootstepMud: mud: NEW surface, 12 (Kenney grass stands in)
constexpr LotSoundFile kLotFootMud[] = {
    {"footsteps/mud/mud_01.ogg",                      0.0F},  // peak -13.2 dBFS
    {"footsteps/mud/mud_02.ogg",                      0.0F},  // peak -12.3 dBFS
    {"footsteps/mud/mud_03.ogg",                      0.0F},  // peak -11.2 dBFS
    {"footsteps/mud/mud_04.ogg",                      0.0F},  // peak -9.7 dBFS
    {"footsteps/mud/mud_05.ogg",                      0.0F},  // peak -4.0 dBFS
    {"footsteps/mud/mud_06.ogg",                      0.0F},  // peak -10.2 dBFS
    {"footsteps/mud/mud_07.ogg",                      0.0F},  // peak -15.0 dBFS
    {"footsteps/mud/mud_08.ogg",                      0.0F},  // peak -14.3 dBFS
    {"footsteps/mud/mud_09.ogg",                      0.0F},  // peak -12.9 dBFS
    {"footsteps/mud/mud_10.ogg",                      0.0F},  // peak -12.5 dBFS
    {"footsteps/mud/mud_11.ogg",                      0.0F},  // peak -3.8 dBFS
    {"footsteps/mud/mud_12.ogg",                      0.0F},  // peak -12.6 dBFS
};
// Whoosh: a swing at air
constexpr LotSoundFile kLotWhoosh[] = {
    {"sfx/malbers/whoosh_1.ogg",                      0.0F},  // peak -6.1 dBFS
    {"sfx/malbers/whoosh_2.ogg",                      0.0F},  // peak -5.1 dBFS
    {"sfx/malbers/whoosh_3.ogg",                      0.0F},  // peak -12.4 dBFS
    {"sfx/malbers/whoosh_4.ogg",                      0.0F},  // peak -6.1 dBFS
};
// WhooshHard: the hard release (hot masters, trimmed)
constexpr LotSoundFile kLotWhooshHard[] = {
    {"sfx/malbers/whoosh_strong.ogg",                -2.9F},  // peak +1.9 dBFS
    {"sfx/malbers/swing_sword.ogg",                  -3.5F},  // peak +2.5 dBFS
};
// PunchMedium: the medium band
constexpr LotSoundFile kLotPunchMedium[] = {
    {"sfx/malbers/flesh_hit_1.ogg",                  -4.1F},  // peak +3.1 dBFS
    {"sfx/malbers/flesh_hit_2.ogg",                   0.0F},  // peak -7.5 dBFS
    {"sfx/malbers/hit_2.ogg",                        -1.4F},  // peak +0.4 dBFS
};
// PunchHeavy: the heavy band (a knockdown)
constexpr LotSoundFile kLotPunchHeavy[] = {
    {"sfx/malbers/hit_1.ogg",                         0.0F},  // peak -4.8 dBFS
    {"sfx/malbers/hit_3.ogg",                        -3.0F},  // peak +2.0 dBFS
};
// HitGrave: a crowning or a kill (hot master, trimmed)
constexpr LotSoundFile kLotHitGrave[] = {
    {"sfx/malbers/hit_grave.ogg",                    -4.1F},  // peak +3.1 dBFS
};
// SwordClash: the guard catches it
constexpr LotSoundFile kLotSwordClash[] = {
    {"sfx/malbers/sword_clash_1.ogg",                -1.1F},  // peak +0.1 dBFS
    {"sfx/malbers/sword_clash_2.ogg",                 0.0F},  // peak -3.2 dBFS
    {"sfx/malbers/sword_clash_3.ogg",                -4.0F},  // peak +3.0 dBFS
};
// ThudHeavy: a body going down
constexpr LotSoundFile kLotThudHeavy[] = {
    {"sfx/malbers/fall_land.ogg",                     0.0F},  // peak -1.1 dBFS
};
// SwordDraw: hands up / steel out (quiet master, lifted)
constexpr LotSoundFile kLotSwordDraw[] = {
    {"sfx/malbers/default_draw.ogg",                 10.0F},  // peak -30.4 dBFS
};
// Sheathe: hands down (quiet master, lifted)
constexpr LotSoundFile kLotSheathe[] = {
    {"sfx/malbers/default_store.ogg",                 8.0F},  // peak -21.9 dBFS
};
// PlayerHurt: the owner's own hurt vox, round-robin
constexpr LotSoundFile kLotPlayerHurt[] = {
    {"sfx/trojia3d/hurt_eric_0.ogg",                  0.0F},  // peak -4.8 dBFS
    {"sfx/trojia3d/hurt_eric_1.ogg",                  0.0F},  // peak -4.9 dBFS
    {"sfx/trojia3d/hurt_eric_2.ogg",                  0.0F},  // peak -4.4 dBFS
    {"sfx/trojia3d/hurt_eric_3.ogg",                  0.0F},  // peak -4.9 dBFS
};
// PlayerDown: the death ceremony
constexpr LotSoundFile kLotPlayerDown[] = {
    {"sfx/trojia3d/hurt_eric_death.ogg",              0.0F},  // peak -3.7 dBFS
};
// CaseClosed: a case closing (2.4 s sting)
constexpr LotSoundFile kLotCaseClosed[] = {
    {"sfx/trojia3d/victory_sting.ogg",                0.0F},  // peak -6.8 dBFS
};
// AmbienceCoastal: 6 s loop under the Harbour bed
constexpr LotSoundFile kLotAmbienceCoastal[] = {
    {"sfx/trojia3d/ambience_coastal.ogg",             0.0F},  // peak -10.5 dBFS
};
// AmbienceStone: 6 s loop under the Interior bed
constexpr LotSoundFile kLotAmbienceStone[] = {
    {"sfx/trojia3d/ambience_stone.ogg",               0.0F},  // peak -3.7 dBFS
};
// AmbienceOrganic: registered, held; no bed reads it
constexpr LotSoundFile kLotAmbienceOrganic[] = {
    {"sfx/trojia3d/ambience_organic.ogg",             0.0F},  // peak -10.6 dBFS
};
// CaseNews: the manifest's own held candidate for a progression cue (0.28 s)
constexpr LotSoundFile kLotCaseNews[] = {
    {"sfx/trojia3d/combo_chime.ogg",                  -4.0F},  // peak -1.6 dBFS, trimmed
};

template <std::size_t N>
constexpr std::span<const sv> asSpan(const sv (&arr)[N]) noexcept {
    return {arr, N};
}

template <std::size_t N>
constexpr std::span<const LotSoundFile> asLotSpan(const LotSoundFile (&arr)[N]) noexcept {
    return {arr, N};
}

[[nodiscard]] std::vector<unsigned char> readWholeFile(
    const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size <= 0) {
        return {};
    }
    in.seekg(0, std::ios::beg);
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    if (!in) {
        return {};
    }
    return bytes;
}

/// One file off disk, decoded, trimmed. nullopt on absent/malformed.
[[nodiscard]] std::optional<Sample> loadOne(const std::filesystem::path& path,
                                            float gainDb, bool keepStereo) {
    const std::vector<unsigned char> bytes = readWholeFile(path);
    if (bytes.empty()) {
        return std::nullopt;
    }
    std::optional<Sample> sample = decodeOgg(bytes.data(), bytes.size(), keepStereo);
    if (!sample || sample->frames() == 0) {
        return std::nullopt;
    }
    applyGainDb(*sample, gainDb);
    return sample;
}

}  // namespace

std::span<const std::string_view> soundPaths(SoundId id) noexcept {
    switch (id) {
        case SoundId::FootstepStone: return asSpan(kFootStone);
        case SoundId::FootstepWood: return asSpan(kFootWood);
        case SoundId::FootstepEarth: return asSpan(kFootEarth);
        case SoundId::FootstepCloth: return asSpan(kFootCloth);
        case SoundId::FootstepIce: return asSpan(kFootIce);
        case SoundId::WadeSplash: return asSpan(kWadeSplash);
        case SoundId::WaterDrip: return asSpan(kWaterDrip);
        case SoundId::UiClick: return asSpan(kUiClick);
        case SoundId::UiBack: return asSpan(kUiBack);
        case SoundId::UiConfirm: return asSpan(kUiConfirm);
        case SoundId::UiError: return asSpan(kUiError);
        case SoundId::UiOpen: return asSpan(kUiOpen);
        case SoundId::UiClose: return asSpan(kUiClose);
        case SoundId::UiSelect: return asSpan(kUiSelect);
        case SoundId::UiTick: return asSpan(kUiTick);
        case SoundId::UiToggle: return asSpan(kUiToggle);
        case SoundId::UiQuestion: return asSpan(kUiQuestion);
        case SoundId::UiScroll: return asSpan(kUiScroll);
        case SoundId::BookOpen: return asSpan(kBookOpen);
        case SoundId::BookClose: return asSpan(kBookClose);
        case SoundId::BookFlip: return asSpan(kBookFlip);
        case SoundId::CoinHandle: return asSpan(kCoinHandle);
        case SoundId::DoorOpen: return asSpan(kDoorOpen);
        case SoundId::DoorClose: return asSpan(kDoorClose);
        case SoundId::Creak: return asSpan(kCreak);
        case SoundId::MetalLatch: return asSpan(kMetalLatch);
        case SoundId::MetalClick: return asSpan(kMetalClick);
        case SoundId::KnifeDraw: return asSpan(kKnifeDraw);
        case SoundId::KnifeSlice: return asSpan(kKnifeSlice);
        case SoundId::Chop: return asSpan(kChop);
        case SoundId::ClothRustle: return asSpan(kClothRustle);
        case SoundId::PunchMedium: return asSpan(kPunchMedium);
        case SoundId::PunchHeavy: return asSpan(kPunchHeavy);
        case SoundId::ThudMedium: return asSpan(kThudMedium);
        case SoundId::ThudHeavy: return asSpan(kThudHeavy);
        case SoundId::GrazeLight: return asSpan(kGrazeLight);
        case SoundId::Whoosh: return asSpan(kWhoosh);
        case SoundId::SwordDraw: return asSpan(kSwordDraw);
        case SoundId::SwordClash: return asSpan(kSwordClash);
        case SoundId::SwordHit: return asSpan(kSwordHit);
        case SoundId::HelmetHit: return asSpan(kHelmetHit);
        case SoundId::HarbourBell: return asSpan(kHarbourBell);
        // THE LOT PASS: Kenney stand-ins (see the block above), or none.
        case SoundId::FootstepMetal: return asSpan(kFootStone);
        case SoundId::FootstepGravel: return asSpan(kFootStone);
        case SoundId::FootstepMud: return asSpan(kFootEarth);
        case SoundId::WhooshHard: return asSpan(kWhoosh);
        case SoundId::HitGrave: return asSpan(kPunchHeavy);
        case SoundId::Sheathe: return asSpan(kSheatheKenney);
        case SoundId::CaseClosed: return asSpan(kCaseClosedKenney);
        case SoundId::CaseNews: return asSpan(kCaseNewsKenney);
        case SoundId::PlayerHurt:
        case SoundId::PlayerDown:
        case SoundId::AmbienceCoastal:
        case SoundId::AmbienceStone:
        case SoundId::AmbienceOrganic:
            return {};
    }
    return {};
}

std::span<const LotSoundFile> lotSoundPaths(SoundId id) noexcept {
    switch (id) {
        case SoundId::FootstepStone: return asLotSpan(kLotFootStone);
        case SoundId::FootstepEarth: return asLotSpan(kLotFootEarth);
        case SoundId::FootstepIce: return asLotSpan(kLotFootIce);
        case SoundId::WadeSplash: return asLotSpan(kLotWadeSplash);
        case SoundId::FootstepMetal: return asLotSpan(kLotFootMetal);
        case SoundId::FootstepGravel: return asLotSpan(kLotFootGravel);
        case SoundId::FootstepMud: return asLotSpan(kLotFootMud);
        case SoundId::Whoosh: return asLotSpan(kLotWhoosh);
        case SoundId::WhooshHard: return asLotSpan(kLotWhooshHard);
        case SoundId::PunchMedium: return asLotSpan(kLotPunchMedium);
        case SoundId::PunchHeavy: return asLotSpan(kLotPunchHeavy);
        case SoundId::HitGrave: return asLotSpan(kLotHitGrave);
        case SoundId::SwordClash: return asLotSpan(kLotSwordClash);
        case SoundId::ThudHeavy: return asLotSpan(kLotThudHeavy);
        case SoundId::SwordDraw: return asLotSpan(kLotSwordDraw);
        case SoundId::Sheathe: return asLotSpan(kLotSheathe);
        case SoundId::PlayerHurt: return asLotSpan(kLotPlayerHurt);
        case SoundId::PlayerDown: return asLotSpan(kLotPlayerDown);
        case SoundId::CaseClosed: return asLotSpan(kLotCaseClosed);
        case SoundId::AmbienceCoastal: return asLotSpan(kLotAmbienceCoastal);
        case SoundId::AmbienceStone: return asLotSpan(kLotAmbienceStone);
        case SoundId::AmbienceOrganic: return asLotSpan(kLotAmbienceOrganic);
        case SoundId::CaseNews: return asLotSpan(kLotCaseNews);
        default:
            return {};
    }
}

std::string_view trackPath(TrackId id) noexcept {
    switch (id) {
        case TrackId::None: return {};
        case TrackId::DocksExplore: return "music/13_whispers_of_the_abyss_loop.ogg";
        case TrackId::DocksCombat: return "music/14_chains_of_the_damned_loop.ogg";
        case TrackId::InteriorExplore: return "music/35_tomb_of_echoes_loop.ogg";
        case TrackId::Climax: return "music/15_the_final_eclipse_loop.ogg";
        case TrackId::Ashes: return "music/1_ashes_of_the_forgotten_loop.ogg";
        case TrackId::CursedGrove: return "music/11_the_cursed_grove_loop.ogg";
        case TrackId::Cathedral: return "music/22_the_forgotten_cathedral_loop.ogg";
    }
    return {};
}

std::shared_ptr<const Sample> loadTrack(const std::filesystem::path& contentDir,
                                        TrackId id) {
    const std::string_view rel = trackPath(id);
    if (rel.empty()) {
        return nullptr;
    }
    // Stereo kept, no trim: the loops sit at -3..-11 dBFS peak and the
    // director's own track gain (kMusicTrackGain) sets the level.
    std::optional<Sample> sample = loadOne(
        contentDir / kLotAudioRootRel / std::filesystem::path(rel), 0.0F,
        /*keepStereo=*/true);
    if (!sample) {
        return nullptr;
    }
    return std::make_shared<const Sample>(std::move(*sample));
}

Bus busFor(SoundId id) noexcept {
    switch (id) {
        case SoundId::FootstepStone:
        case SoundId::FootstepWood:
        case SoundId::FootstepEarth:
        case SoundId::FootstepCloth:
        case SoundId::FootstepIce:
        case SoundId::FootstepMetal:
        case SoundId::FootstepGravel:
        case SoundId::FootstepMud:
        case SoundId::WadeSplash:
            return Bus::Footsteps;
        case SoundId::WaterDrip:
        case SoundId::HarbourBell:
        case SoundId::AmbienceCoastal:
        case SoundId::AmbienceStone:
        case SoundId::AmbienceOrganic:
            return Bus::Ambient;
        case SoundId::UiClick:
        case SoundId::UiBack:
        case SoundId::UiConfirm:
        case SoundId::UiError:
        case SoundId::UiOpen:
        case SoundId::UiClose:
        case SoundId::UiSelect:
        case SoundId::UiTick:
        case SoundId::UiToggle:
        case SoundId::UiQuestion:
        case SoundId::UiScroll:
            return Bus::Ui;
        case SoundId::BookOpen:
        case SoundId::BookClose:
        case SoundId::BookFlip:
        case SoundId::CoinHandle:
        case SoundId::DoorOpen:
        case SoundId::DoorClose:
        case SoundId::Creak:
        case SoundId::MetalLatch:
        case SoundId::MetalClick:
        case SoundId::ClothRustle:
        case SoundId::CaseClosed:
        case SoundId::CaseNews:
            return Bus::World;
        case SoundId::KnifeDraw:
        case SoundId::KnifeSlice:
        case SoundId::Chop:
        case SoundId::PunchMedium:
        case SoundId::PunchHeavy:
        case SoundId::ThudMedium:
        case SoundId::ThudHeavy:
        case SoundId::GrazeLight:
        case SoundId::Whoosh:
        case SoundId::SwordDraw:
        case SoundId::SwordClash:
        case SoundId::SwordHit:
        case SoundId::HelmetHit:
        case SoundId::WhooshHard:
        case SoundId::HitGrave:
        case SoundId::Sheathe:
        case SoundId::PlayerHurt:
        case SoundId::PlayerDown:
            return Bus::Combat;
    }
    return Bus::Master;
}

SoundBank SoundBank::load(const std::filesystem::path& contentDir) {
    SoundBank bank;
    const std::filesystem::path root = contentDir / kAudioRootRel;
    const std::filesystem::path lotRoot = contentDir / kLotAudioRootRel;
    std::size_t attempted = 0;
    std::size_t lotAttempted = 0;
    for (std::size_t i = 0; i < kSoundIdCount; ++i) {
        const SoundId id = static_cast<SoundId>(i);
        // THE LOT PASS: the LOT rows first. Any variant that loads makes the
        // id a LOT id and its Kenney rows are not even opened; none loading
        // (the docker gate, a checkout without the staged audio) falls
        // through to the Kenney rows exactly as before the pass.
        for (const LotSoundFile& file : lotSoundPaths(id)) {
            ++lotAttempted;
            std::optional<Sample> sample =
                loadOne(lotRoot / std::filesystem::path(file.rel), file.gainDb,
                        /*keepStereo=*/false);
            if (!sample) {
                ++bank.lotMissing_;
                continue;
            }
            bank.variants_[i].push_back(
                std::make_shared<const Sample>(std::move(*sample)));
            ++bank.lotLoaded_;
        }
        if (!bank.variants_[i].empty()) {
            continue;
        }
        for (const std::string_view rel : soundPaths(id)) {
            ++attempted;
            std::optional<Sample> sample =
                loadOne(root / std::filesystem::path(rel), 0.0F,
                        /*keepStereo=*/false);
            if (!sample) {
                ++bank.missing_;
                continue;
            }
            bank.variants_[i].push_back(
                std::make_shared<const Sample>(std::move(*sample)));
        }
    }
    if (bank.missing_ != 0) {
        // ONE line, and the game keeps booting -- the atlas's own convention
        // for a checkout without content/art.
        std::fprintf(stderr,
                     "granadad-audio: %zu of %zu sound files unavailable under "
                     "%s; those sounds stay silent\n",
                     bank.missing_, attempted, root.string().c_str());
    }
    if (bank.lotMissing_ != 0) {
        // ONE line for the second root too. Not a finding: the LOT tree is
        // gitignored and absent from every gate by design.
        std::fprintf(stderr,
                     "granadad-audio: LOT audio: %zu of %zu files unavailable "
                     "under %s; Kenney rows stand in where they exist\n",
                     bank.lotMissing_, lotAttempted, lotRoot.string().c_str());
    }
    return bank;
}

SoundBank SoundBank::empty() {
    return SoundBank{};
}

SoundBank SoundBank::synthetic() {
    SoundBank bank;
    // Two 10ms decaying blips per id, pitched apart per id and per variant so
    // nothing is accidentally proven against a shared buffer.
    constexpr std::size_t kBlipLen = 480;
    for (std::size_t i = 0; i < kSoundIdCount; ++i) {
        for (std::size_t variant = 0; variant < 2; ++variant) {
            Sample s;
            s.mono.resize(kBlipLen);
            const float hz =
                330.0F + 7.0F * static_cast<float>(i) +
                90.0F * static_cast<float>(variant);
            const float step = 2.0F * 3.14159265358979323846F * hz /
                               static_cast<float>(kSampleRate);
            for (std::size_t n = 0; n < kBlipLen; ++n) {
                const float env =
                    1.0F - static_cast<float>(n) / static_cast<float>(kBlipLen);
                s.mono[n] = 0.5F * env * std::sin(step * static_cast<float>(n));
            }
            bank.variants_[i].push_back(
                std::make_shared<const Sample>(std::move(s)));
        }
    }
    return bank;
}

std::size_t SoundBank::variantCount(SoundId id) const noexcept {
    const std::size_t i = soundIndex(id);
    return i < kSoundIdCount ? variants_[i].size() : 0;
}

std::shared_ptr<const Sample> SoundBank::sample(SoundId id,
                                                std::size_t variant) const noexcept {
    const std::size_t i = soundIndex(id);
    if (i >= kSoundIdCount || variant >= variants_[i].size()) {
        return nullptr;
    }
    return variants_[i][variant];
}

bool SoundBank::anyLoaded() const noexcept {
    for (const auto& v : variants_) {
        if (!v.empty()) {
            return true;
        }
    }
    return false;
}

}  // namespace granadad::audio
