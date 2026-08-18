// See sound_bank.hpp. The manifest below names REAL files — every entry was
// verified against the vendored tree when it was written, and
// test_audio_engine.cpp re-verifies existence on any checkout that has the
// Kenney packs (verify-windows.ps1's native run always does). Variant counts
// are NOT uniform: e.g. the Interface set ships tick_001, _002 and _004 with
// no _003, which is why these are explicit file lists and never a
// printf-pattern over an assumed range.

#include "granadad/audio/sound_bank.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>

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

template <std::size_t N>
constexpr std::span<const sv> asSpan(const sv (&arr)[N]) noexcept {
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
    }
    return {};
}

Bus busFor(SoundId id) noexcept {
    switch (id) {
        case SoundId::FootstepStone:
        case SoundId::FootstepWood:
        case SoundId::FootstepEarth:
        case SoundId::FootstepCloth:
        case SoundId::FootstepIce:
        case SoundId::WadeSplash:
            return Bus::Footsteps;
        case SoundId::WaterDrip:
        case SoundId::HarbourBell:
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
            return Bus::Combat;
    }
    return Bus::Master;
}

SoundBank SoundBank::load(const std::filesystem::path& contentDir) {
    SoundBank bank;
    const std::filesystem::path root = contentDir / kAudioRootRel;
    std::size_t attempted = 0;
    for (std::size_t i = 0; i < kSoundIdCount; ++i) {
        const SoundId id = static_cast<SoundId>(i);
        for (const std::string_view rel : soundPaths(id)) {
            ++attempted;
            const std::vector<unsigned char> bytes =
                readWholeFile(root / std::filesystem::path(rel));
            if (bytes.empty()) {
                ++bank.missing_;
                continue;
            }
            std::optional<Sample> sample = decodeOggToMono(bytes.data(), bytes.size());
            if (!sample || sample->mono.empty()) {
                ++bank.missing_;
                continue;
            }
            bank.variants_[i].push_back(
                std::make_shared<const Sample>(std::move(*sample)));
        }
    }
    if (bank.missing_ != 0) {
        // ONE line, and the game keeps booting — the atlas's own convention
        // for a checkout without content/art.
        std::fprintf(stderr,
                     "granadad-audio: %zu of %zu sound files unavailable under "
                     "%s; those sounds stay silent\n",
                     bank.missing_, attempted, root.string().c_str());
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
