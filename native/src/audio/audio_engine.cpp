// See audio_engine.hpp — especially the determinism note and the wiring plan.

#include "granadad/audio/audio_engine.hpp"

#include <algorithm>

namespace granadad::audio {

namespace {

// ---------------------------------------------------------------------------
// Material -> footstep surface. Index == material registry id, and the NAMES
// are carried so test_audio_engine.cpp can pin this table row-by-row against
// render::materialIds() — the registry sorts the raws' string ids, so a new
// material raw shifts ids and MUST fail a test rather than silently remap
// every footstep in the ward.
// ---------------------------------------------------------------------------
constexpr MaterialSurfaceRow kMaterialSurfaces[] = {
    {"ash", Surface::Earth},          // soft volcanic grit
    {"brick", Surface::Stone},
    {"brick_facade", Surface::Stone},
    {"chromatis", Surface::Stone},
    {"chromatis_melt", Surface::Stone},
    {"cloth", Surface::Cloth},
    {"dirt", Surface::Earth},
    {"glowstone", Surface::Stone},
    {"granite", Surface::Stone},
    {"granite_facade", Surface::Stone},
    {"ice", Surface::Ice},
    {"leather", Surface::Cloth},
    {"lightstone", Surface::Stone},
    {"lightstone_shards", Surface::Stone},
    {"oak", Surface::Wood},
    {"phorys", Surface::Earth},       // the reef-growth reads organic underfoot
    {"reman_concrete", Surface::Stone},
    {"reman_facade", Surface::Stone},
    {"steel", Surface::Stone},        // no metal footstep set vendored; stone
                                      // rings closer than wood
    {"thatch", Surface::Earth},
    {"trudgeon_wood", Surface::Wood},
    {"trudgeon_wood@getilia_soak", Surface::Wood},
};

constexpr std::size_t kMaterialSurfaceCount =
    sizeof(kMaterialSurfaces) / sizeof(kMaterialSurfaces[0]);

// ---------------------------------------------------------------------------
// Bed definitions. Day/night gains are interpolated by dayness(); the
// procedural layers are the v1 harbour bed (the vendored set has no ambience
// recordings — see sound_ids.hpp). When real CC0 harbour loops arrive they
// are added as BedLoop rows and nothing else changes.
// ---------------------------------------------------------------------------
struct BedProc {
    ProcLayer layer;
    float dayGain;
    float nightGain;
};

struct BedSparse {
    SoundId id;
    float minGapSec;
    float maxGapSec;
    float dayGain;
    float nightGain;
    float panSpread;  ///< one-shots land at a random pan in +-spread
};

struct BedLoop {
    SoundId id;
    float dayGain;
    float nightGain;
};

struct BedDef {
    std::span<const BedProc> procs;
    std::span<const BedSparse> sparses;
    std::span<const BedLoop> loops;
};

constexpr BedProc kHarbourProcs[] = {
    {ProcLayer::WaterLap, 0.50F, 0.38F},
    {ProcLayer::Wind, 0.22F, 0.40F},
};
constexpr BedSparse kHarbourSparses[] = {
    // Drips and slaps off the pilings, denser at night when the crowd is not
    // there to cover them; the Docks bell, rare, mostly a daytime thing.
    {SoundId::WaterDrip, 5.0F, 13.0F, 0.22F, 0.32F, 0.8F},
    {SoundId::WadeSplash, 9.0F, 22.0F, 0.16F, 0.12F, 0.9F},
    {SoundId::HarbourBell, 50.0F, 140.0F, 0.10F, 0.05F, 0.4F},
};

constexpr BedProc kInteriorProcs[] = {
    {ProcLayer::Wind, 0.05F, 0.09F},  // wind heard THROUGH the walls
};
constexpr BedSparse kInteriorSparses[] = {
    {SoundId::Creak, 9.0F, 25.0F, 0.28F, 0.35F, 0.6F},
};

[[nodiscard]] BedDef bedDef(BedId bed) noexcept {
    switch (bed) {
        case BedId::Harbour:
            return {kHarbourProcs, kHarbourSparses, {}};
        case BedId::Interior:
            return {kInteriorProcs, kInteriorSparses, {}};
        case BedId::None:
            break;
    }
    return {};
}

[[nodiscard]] float lerp(float a, float b, float t) noexcept {
    return a + (b - a) * t;
}

}  // namespace

std::span<const MaterialSurfaceRow> materialSurfaceTable() noexcept {
    return {kMaterialSurfaces, kMaterialSurfaceCount};
}

Surface surfaceForMaterial(std::uint16_t materialId) noexcept {
    if (materialId < kMaterialSurfaceCount) {
        return kMaterialSurfaces[materialId].surface;
    }
    return Surface::Stone;
}

SoundId footstepSoundFor(Surface surface) noexcept {
    switch (surface) {
        case Surface::Stone: return SoundId::FootstepStone;
        case Surface::Wood: return SoundId::FootstepWood;
        case Surface::Earth: return SoundId::FootstepEarth;
        case Surface::Cloth: return SoundId::FootstepCloth;
        case Surface::Ice: return SoundId::FootstepIce;
    }
    return SoundId::FootstepStone;
}

float dayness(int secondsSinceMidnight) noexcept {
    // Wrap into one day so a caller handing in an engine-clock total behaves.
    int s = secondsSinceMidnight % 86400;
    if (s < 0) {
        s += 86400;
    }
    constexpr int kDawnStart = 5 * 3600;
    constexpr int kDawnEnd = 7 * 3600;
    constexpr int kDuskStart = 19 * 3600;
    constexpr int kDuskEnd = 21 * 3600;
    if (s < kDawnStart || s >= kDuskEnd) {
        return 0.0F;
    }
    if (s < kDawnEnd) {
        return static_cast<float>(s - kDawnStart) /
               static_cast<float>(kDawnEnd - kDawnStart);
    }
    if (s < kDuskStart) {
        return 1.0F;
    }
    return 1.0F - static_cast<float>(s - kDuskStart) /
                      static_cast<float>(kDuskEnd - kDuskStart);
}

// ---------------------------------------------------------------------------

std::unique_ptr<AudioEngine> AudioEngine::create(SoundBank bank,
                                                 std::unique_ptr<Backend> backend,
                                                 std::uint64_t rngSeed) {
    // Not make_unique: the constructor is private.
    return std::unique_ptr<AudioEngine>(
        new AudioEngine(std::move(bank), std::move(backend), rngSeed));
}

AudioEngine::AudioEngine(SoundBank bank, std::unique_ptr<Backend> backend,
                         std::uint64_t rngSeed)
    : bank_(std::move(bank)), backend_(std::move(backend)), rng_(rngSeed) {
    lastVariant_.fill(0xFF);
    mixer_.setNoiseSeed(static_cast<std::uint32_t>(rngSeed ^ (rngSeed >> 32)) |
                        1U);
    deviceOpen_ = backend_ ? backend_->start(mixer_) : false;
}

AudioEngine::~AudioEngine() {
    // The backend stops pulling before the mixer dies: backend_ is declared
    // after mixer_, so it is destroyed first.
}

std::uint64_t AudioEngine::nextRandom() noexcept {
    // splitmix64 — self-contained, seeded by the caller, never the sim's.
    rng_ += 0x9E3779B97F4A7C15ULL;
    std::uint64_t z = rng_;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

float AudioEngine::rand01() noexcept {
    return static_cast<float>(nextRandom() >> 40) * (1.0F / 16777216.0F);
}

void AudioEngine::playOn(Bus bus, SoundId id, float gain, float pan,
                         float pitch) {
    const std::size_t count = bank_.variantCount(id);
    if (count == 0) {
        return;  // silence, never a crash — the missing-content contract
    }
    std::size_t variant = static_cast<std::size_t>(nextRandom() % count);
    const std::size_t idx = soundIndex(id);
    if (count > 1 && variant == static_cast<std::size_t>(lastVariant_[idx])) {
        variant = (variant + 1) % count;  // never the same twice running
    }
    lastVariant_[idx] = static_cast<std::uint8_t>(variant);
    mixer_.play(bank_.sample(id, variant), bus, gain, pan, pitch, false);
}

void AudioEngine::playOneShot(SoundId id, float gain, float pan, float pitch) {
    playOn(busFor(id), id, gain, pan, pitch);
}

bool AudioEngine::footstep(std::uint16_t materialId, bool running,
                           int fluidDepth) {
    // The cadence gate: session may call this every frame while the player
    // moves; only actual step rhythm comes out.
    const float gap = running ? 0.21F : 0.34F;
    if (sinceStep_ < gap) {
        return false;
    }
    sinceStep_ = 0.0F;
    const float pitch = 0.94F + 0.12F * rand01();
    const float pan = 0.06F * (rand01() - 0.5F);
    float gain = running ? 0.9F : 0.6F;
    if (fluidDepth > 0) {
        const int depth = std::min(fluidDepth, 7);
        const float splashGain =
            std::min(1.0F, 0.35F + 0.09F * static_cast<float>(depth));
        playOn(Bus::Footsteps, SoundId::WadeSplash, splashGain, pan,
               0.9F + 0.2F * rand01());
        if (depth >= 3) {
            gain *= 0.5F;  // deep water mostly drowns the surface sound
        }
    }
    playOn(Bus::Footsteps, footstepSoundFor(surfaceForMaterial(materialId)),
           gain, pan, pitch);
    return true;
}

void AudioEngine::primeSlot(BedSlot& slot) {
    const BedDef def = bedDef(slot.bed);
    for (std::size_t i = 0; i < kMaxSparse; ++i) {
        slot.sparseTimer[i] = 0.0F;
        slot.loopVoice[i] = Mixer::kNoVoice;
    }
    for (std::size_t i = 0; i < def.sparses.size() && i < kMaxSparse; ++i) {
        // First fire lands within a few seconds so a bed does not feel dead
        // on arrival, then settles into its own gap range.
        slot.sparseTimer[i] = 1.0F + rand01() * 3.0F;
    }
    for (std::size_t i = 0; i < def.loops.size() && i < kMaxSparse; ++i) {
        const SoundId id = def.loops[i].id;
        const std::size_t count = bank_.variantCount(id);
        if (count == 0) {
            continue;
        }
        slot.loopVoice[i] =
            mixer_.play(bank_.sample(id, static_cast<std::size_t>(
                                             nextRandom() % count)),
                        Bus::Ambient, 0.0F, 0.0F, 1.0F, /*loop=*/true);
    }
}

void AudioEngine::retireSlot(BedSlot& slot) {
    for (std::size_t i = 0; i < kMaxSparse; ++i) {
        if (slot.loopVoice[i] != Mixer::kNoVoice) {
            mixer_.stop(slot.loopVoice[i], 0.2F);
            slot.loopVoice[i] = Mixer::kNoVoice;
        }
    }
    slot.bed = BedId::None;
    slot.env = 0.0F;
    slot.envRate = 0.0F;
}

void AudioEngine::advanceSlot(BedSlot& slot, float dt, float day) {
    if (slot.bed == BedId::None) {
        return;
    }
    slot.env = std::clamp(slot.env + slot.envRate * dt, 0.0F, 1.0F);
    if (slot.env <= 0.0F && slot.envRate < 0.0F) {
        retireSlot(slot);
        return;
    }
    const BedDef def = bedDef(slot.bed);
    // Loop-file layers track env * day/night gain.
    for (std::size_t i = 0; i < def.loops.size() && i < kMaxSparse; ++i) {
        if (slot.loopVoice[i] != Mixer::kNoVoice) {
            const float g =
                lerp(def.loops[i].nightGain, def.loops[i].dayGain, day);
            mixer_.setVoiceGain(slot.loopVoice[i], g * slot.env, 0.1F);
        }
    }
    // Sparse one-shots: only while audibly present, and only the winning-in
    // slot fires (a fading bed does not keep dripping).
    if (slot.envRate < 0.0F || slot.env < 0.05F) {
        return;
    }
    for (std::size_t i = 0; i < def.sparses.size() && i < kMaxSparse; ++i) {
        slot.sparseTimer[i] -= dt;
        if (slot.sparseTimer[i] > 0.0F) {
            continue;
        }
        const BedSparse& sp = def.sparses[i];
        const float g = lerp(sp.nightGain, sp.dayGain, day) * slot.env;
        playOn(Bus::Ambient, sp.id, g, sp.panSpread * (2.0F * rand01() - 1.0F),
               0.95F + 0.1F * rand01());
        slot.sparseTimer[i] =
            sp.minGapSec + (sp.maxGapSec - sp.minGapSec) * rand01();
    }
}

void AudioEngine::startBed(BedId bed, float crossfadeSec) {
    if (bed == activeBed_) {
        return;  // re-asserting every frame is legal wiring
    }
    if (bed == BedId::None) {
        stopBed(crossfadeSec);
        return;
    }
    const float rate = crossfadeSec > 0.0F ? 1.0F / crossfadeSec : 1.0e6F;
    if (fading_.bed != BedId::None) {
        retireSlot(fading_);  // a double-switch mid-fade drops the older bed
    }
    if (active_.bed != BedId::None) {
        fading_ = active_;
        fading_.envRate = -rate;
    }
    active_ = BedSlot{};
    active_.bed = bed;
    active_.env = 0.0F;
    active_.envRate = rate;
    primeSlot(active_);
    activeBed_ = bed;
}

void AudioEngine::stopBed(float fadeSec) {
    if (active_.bed == BedId::None) {
        return;
    }
    const float rate = fadeSec > 0.0F ? 1.0F / fadeSec : 1.0e6F;
    if (fading_.bed != BedId::None) {
        retireSlot(fading_);
    }
    fading_ = active_;
    fading_.envRate = -rate;
    active_ = BedSlot{};
    activeBed_ = BedId::None;
}

void AudioEngine::setTimeOfDay(int secondsSinceMidnight) noexcept {
    timeOfDay_ = secondsSinceMidnight;
}

void AudioEngine::setBusGain(Bus bus, float gain) {
    mixer_.setBusGain(bus, gain);
}

void AudioEngine::update(float dtSec) {
    // A pause or a debugger hitch must not dump minutes of sparse one-shots
    // in one frame.
    const float dt = std::clamp(dtSec, 0.0F, 0.25F);
    sinceStep_ = std::min(sinceStep_ + dt, 1.0e6F);

    const float day = dayness(timeOfDay_);
    advanceSlot(active_, dt, day);
    advanceSlot(fading_, dt, day);

    // Procedural layer targets: both slots contribute, so a Harbour->Interior
    // crossfade is the water fading under the rising creaks, not a cut.
    std::array<float, kProcLayerCount> target{};
    for (const BedSlot* slot : {&active_, &fading_}) {
        if (slot->bed == BedId::None) {
            continue;
        }
        const BedDef def = bedDef(slot->bed);
        for (const BedProc& p : def.procs) {
            target[static_cast<std::size_t>(p.layer)] +=
                lerp(p.nightGain, p.dayGain, day) * slot->env;
        }
    }
    for (std::size_t i = 0; i < kProcLayerCount; ++i) {
        mixer_.setProceduralGain(static_cast<ProcLayer>(i),
                                 std::min(target[i], 1.0F), 0.08F);
    }
}

void AudioEngine::render(float* interleavedStereo, int frames) {
    mixer_.render(interleavedStereo, frames);
}

}  // namespace granadad::audio
