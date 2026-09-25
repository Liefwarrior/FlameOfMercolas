// WEATHER, RENDER-ONLY. Roadmap D15 ruled it yes on one condition: the sim
// never reads it. Four skies -- clear, overcast, harbour fog, wind -- each
// with an intensity that eases in and out over the hours, drawn as a pure
// function of (world seed, calendar day, clock): no RNG stream, no wall
// clock, no sim field. These cases hold the lane to that in the four ways
// that matter:
//
//   * PURE. weatherFor at one seed, one day and one clock is one answer, and
//     the shipped seed's first days are pinned period by period (edges in
//     minutes, kinds, peaks) so a change to the draw is a red case and not a
//     different sky nobody noticed. The day's shape is swept for four hundred
//     days: two to four periods, eased, never cut, fog the most common thing
//     that is not clear and a dawn, dusk and night thing;
//   * THE CLEAR SKY IS THE OLD SKY. skyAt(t) and skyAt(t, Weather{}) are the
//     same bytes at every minute of the day, which is what keeps every frame
//     and every sky test pinned before this lane exactly where it was;
//   * THE SIM DOES NOT KNOW. A session pinned to fog, overcast or wind runs
//     the same steps to the same tavern and population digests as a clear
//     one -- while its frame is a different picture;
//   * THE SAME INPUTS ARE THE SAME FRAME. Two sessions, one config, one fog:
//     byte-identical frames. One description refreshed twice: one scene hash.
//     The veil is in the 3D picture headless through rlsw, twice the same,
//     and a clear description has no veil in it at all.
//
// Plus the wiring at both ends: the capture summary's `weather=` field, the
// --weather= names, the chunk version moving with the weather, and the wind
// bed lifting under a blow and dropping in a still fog.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

#include "granadad/audio/audio_engine.hpp"
#include "granadad/audio/mixer.hpp"
#include "granadad/audio/sound_bank.hpp"
#include "granadad/content/content_dir.hpp"
#include "granadad/content/world.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/render/atlas.hpp"
#include "granadad/render/framebuffer.hpp"
#include "granadad/render/lighting.hpp"
#include "granadad/render/session.hpp"
#include "granadad/render/vertical.hpp"
#include "granadad/render3d/backend.hpp"
#include "granadad/render3d/chunk_mesher.hpp"
#include "granadad/render3d/scene.hpp"
#include "granadad/render3d/world_scene.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/human_scale.hpp"
#include "granadad/sim/player.hpp"
#include "granadad/sim/tavern.hpp"
#include "granadad/sim/tile_query.hpp"
#include "granadad/sim/vertical_scale.hpp"
#include "granadad/sim/ward_actors.hpp"
#include "granadad/sim/world_hash.hpp"

using namespace granadad::render3d;
namespace render = granadad::render;
namespace sim = granadad::sim;
namespace content = granadad::content;
using granadad::audio::AudioEngine;
using granadad::audio::BedId;
using granadad::audio::ProcLayer;
using granadad::audio::SoundBank;
using render::WeatherKind;

namespace {

/// SessionConfig's own default seed, "GRANADAD". The one every shipped frame
/// and every twin run is drawn under.
constexpr std::uint64_t kShippedSeed = 0x4752414E41444144ull;

constexpr int kWidth = 320;
constexpr int kHeight = 180;
constexpr float kAspect = static_cast<float>(kWidth) / static_cast<float>(kHeight);

[[nodiscard]] int minutes(int minute) { return minute * 60; }

[[nodiscard]] render::Weather pinned(WeatherKind kind, float intensity) {
    render::Weather weather;
    weather.kind = kind;
    weather.intensity = intensity;
    return weather;
}

[[nodiscard]] bool sameRgb(const render::Rgb& a, const render::Rgb& b) {
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

/// Field for field, exact: the point is "the same bytes", not "close".
[[nodiscard]] bool sameSky(const render::SkyState& a, const render::SkyState& b) {
    return sameRgb(a.ambient, b.ambient) && sameRgb(a.skyTop, b.skyTop) &&
           sameRgb(a.skyHorizon, b.skyHorizon) && sameRgb(a.fog, b.fog) &&
           a.fogDistance == b.fogDistance && a.daylight == b.daylight && a.veil == b.veil &&
           a.haloScale == b.haloScale;
}

/// The Docks at nine at night under one pinned sky. liveWeather stays off, so
/// `weather` is the whole of it: Clear at nothing, anything else at full.
[[nodiscard]] render::SessionConfig docksUnder(WeatherKind kind) {
    render::SessionConfig config;
    config.contentDir = content::contentDir();
    config.world = sim::docks::kWorldName;
    config.timeOfDay = 21 * 3600;
    config.timeOfDayGiven = true;
    config.width = kWidth;
    config.height = kHeight;
    config.weather = kind;
    return config;
}

/// Three seconds forward down the Tarwalk, two standing still: enough for
/// the room and the ward to have stepped, the player to have moved, and a
/// hash to have something to disagree about.
void walk(render::Session& session) {
    sim::MoveInput forward;
    forward.forward = 1;
    session.stepMany(forward, 3 * sim::kStepsPerSecond);
    const sim::MoveInput still{};
    session.stepMany(still, 2 * sim::kStepsPerSecond);
}

/// The tavern's own section of the world hash -- the twin gate's number.
[[nodiscard]] std::uint64_t tavernDigest(const render::Session& session) {
    sim::WorldHasher hasher;
    session.tavern().hash_into(hasher.section_sink(session.tavern().id()));
    return hasher.section_hash(session.tavern().id());
}

/// The ward's people, digested the way test_street_watch.cpp digests them.
[[nodiscard]] std::uint64_t peopleDigest(const render::Session& session) {
    sim::HashSink sink(0x5745415448455221ull);  // "WEATHER!"
    session.people().hash_into(sink);
    return sink.finished();
}

const content::World& docksWorld() {
    static const content::World world =
        content::loadWorldFile(content::bakedMap(sim::docks::kWorldName));
    return world;
}

const render::TileAtlas& proceduralAtlas() {
    static const render::TileAtlas atlas = render::TileAtlas::procedural();
    return atlas;
}

/// The authored spawn on the quayside, looking west down the Tarwalk.
[[nodiscard]] render::Camera spawnCamera() {
    render::Camera eye;
    eye.x = static_cast<float>(sim::docks::kSpawnTileX) + 0.5F;
    eye.y = static_cast<float>(sim::docks::kSpawnTileY) + 0.5F;
    eye.z = render::bandSurface(sim::docks::kSpawnBand) +
            static_cast<float>(sim::kEyeHeightTilesQ8) / 256.0F;
    eye.yaw = 265.0F * 3.14159265358979323846F / 180.0F;
    eye.pitch = 0.0F;
    eye.hfovTan = 1.0F;
    return eye;
}

[[nodiscard]] BackendConfig headlessConfig() {
    BackendConfig config;
    config.width = kWidth;
    config.height = kHeight;
    config.windowScale = 1;
    config.vsync = false;
    return config;
}

[[nodiscard]] render::Framebuffer drawOnce(Backend& video, const SceneDescription& scene,
                                           SceneStats* stats) {
    video.beginFrame(scene.clearColour);
    const SceneStats drawn = video.drawScene(scene);
    if (stats != nullptr) {
        *stats = drawn;
    }
    render::Framebuffer shot(1, 1);
    video.endFrame(&shot);
    return shot;
}

/// How far a mesh vertex sits from the mesh's own origin.
[[nodiscard]] float radiusOf(const MeshData& mesh, std::uint16_t index) {
    const std::size_t at = static_cast<std::size_t>(index) * 3U;
    const float x = mesh.positions[at];
    const float y = mesh.positions[at + 1];
    const float z = mesh.positions[at + 2];
    return std::sqrt(x * x + y * y + z * z);
}

/// Renders `frames` of audio and throws them away: what advances the
/// mixer's gain ramps between updates.
void drain(AudioEngine& engine, int frames) {
    std::vector<float> buffer(static_cast<std::size_t>(frames) * 2U);
    engine.render(buffer.data(), frames);
}

}  // namespace

// ---------------------------------------------------------------------------
// the model
// ---------------------------------------------------------------------------

TEST_CASE("the weather is a pure function of the seed, the day and the clock") {
    // Same three numbers, same answer, on every call.
    for (std::int32_t day = 0; day < 8; ++day) {
        for (int minute = 0; minute < 1440; minute += 7) {
            const render::Weather a = render::weatherFor(kShippedSeed, day, minutes(minute));
            const render::Weather b = render::weatherFor(kShippedSeed, day, minutes(minute));
            CHECK(a == b);
            CHECK(a.intensity >= 0.0F);
            CHECK(a.intensity <= 1.0F);
            if (a.kind == WeatherKind::Clear) {
                CHECK(a.intensity == 0.0F);
                CHECK(a.clear());
            }
        }
    }
    // The clock wraps: a day and a minute is that minute.
    CHECK(render::weatherFor(kShippedSeed, 3, minutes(840)) ==
          render::weatherFor(kShippedSeed, 3, minutes(840) + render::kSecondsPerDay));
    // A different seed is a different year -- somewhere in its first month.
    bool differs = false;
    for (std::int32_t day = 0; day < 30 && !differs; ++day) {
        for (int minute = 0; minute < 1440 && !differs; minute += 30) {
            differs = render::weatherFor(kShippedSeed, day, minutes(minute)) !=
                      render::weatherFor(kShippedSeed ^ 0x5745415448455221ull, day,
                                         minutes(minute));
        }
    }
    CHECK(differs);
}

TEST_CASE("the shipped seed's first days are pinned, period by period") {
    // Computed off the draw as written (splitmix64 over the seed, the day and
    // a named salt) and asserted here, so the sky a reviewer shoots on day
    // three is the sky the next build shoots. Minutes from midnight; a period
    // that runs through midnight starts negative or ends past 1440.
    struct Pinned {
        std::int32_t day;
        std::int32_t start;
        std::int32_t end;
        WeatherKind kind;
        float peak;
    };
    const Pinned pins[] = {
        {0, 356, 1102, WeatherKind::Clear, 0.0F},    {0, 1102, 1803, WeatherKind::Wind, 0.5937F},
        {1, 363, 1020, WeatherKind::Overcast, 0.6599F}, {1, 1020, 1755, WeatherKind::Overcast, 0.9239F},
        {2, 315, 1077, WeatherKind::Clear, 0.0F},    {2, 1077, 1787, WeatherKind::Clear, 0.0F},
        {3, 347, 1053, WeatherKind::Wind, 0.8212F},  {3, 1053, 1577, WeatherKind::Fog, 0.8360F},
    };
    for (const Pinned& pin : pins) {
        CAPTURE(pin.day);
        CAPTURE(pin.start);
        const render::WeatherPeriod at =
            render::weatherPeriodAt(kShippedSeed, pin.day, minutes(pin.start));
        CHECK(at.startMinute == pin.start);
        CHECK(at.endMinute == pin.end);
        CHECK(at.kind == pin.kind);
        CHECK(at.peak == doctest::Approx(pin.peak).epsilon(0.002));
        // And the last minute of it, today, is still it.
        const render::WeatherPeriod late =
            render::weatherPeriodAt(kShippedSeed, pin.day, minutes(std::min(pin.end - 1, 1439)));
        CHECK(late.startMinute == pin.start);
        CHECK(late.kind == pin.kind);
    }

    // Through midnight: day one's small hours are day zero's last period --
    // the same wind at the same peak, its start counted back from this
    // midnight -- so nothing resets at 00:00.
    const render::WeatherPeriod small = render::weatherPeriodAt(kShippedSeed, 1, minutes(0));
    CHECK(small.startMinute == 1102 - 1440);
    CHECK(small.endMinute == 363);
    CHECK(small.kind == WeatherKind::Wind);
    CHECK(small.peak == doctest::Approx(0.5937F).epsilon(0.002));
    const render::WeatherPeriod lateDayZero =
        render::weatherPeriodAt(kShippedSeed, 0, minutes(1439));
    CHECK(lateDayZero.kind == small.kind);
    CHECK(lateDayZero.peak == small.peak);
    CHECK(render::weatherFor(kShippedSeed, 0, minutes(1439)).intensity ==
          doctest::Approx(render::weatherFor(kShippedSeed, 1, minutes(0)).intensity)
              .epsilon(0.03));

    // Day three's afternoon, minute by minute where it matters: the wind
    // full at two, half out thirty minutes before its cut, nothing at the
    // cut (17:33), the fog half in thirty minutes after and full by nine.
    const render::Weather two = render::weatherFor(kShippedSeed, 3, minutes(840));
    CHECK(two.kind == WeatherKind::Wind);
    CHECK(two.intensity == doctest::Approx(0.8212F).epsilon(0.002));
    const render::Weather halfOut = render::weatherFor(kShippedSeed, 3, minutes(1023));
    CHECK(halfOut.kind == WeatherKind::Wind);
    CHECK(halfOut.intensity == doctest::Approx(0.8212F * 0.5F).epsilon(0.002));
    const render::Weather cut = render::weatherFor(kShippedSeed, 3, minutes(1053));
    CHECK(cut.clear());
    const render::Weather halfIn = render::weatherFor(kShippedSeed, 3, minutes(1083));
    CHECK(halfIn.kind == WeatherKind::Fog);
    CHECK(halfIn.intensity == doctest::Approx(0.8360F * 0.5F).epsilon(0.002));
    const render::Weather nine = render::weatherFor(kShippedSeed, 3, minutes(1300));
    CHECK(nine.kind == WeatherKind::Fog);
    CHECK(nine.intensity == doctest::Approx(0.8360F).epsilon(0.002));
}

TEST_CASE("a day is two to four periods, eased, never cut, and the fog keeps to the water's hours") {
    int tally[render::kWeatherKindCount] = {};
    int fogOnTheWater = 0;  // a period whose middle sits at night, dawn or dusk
    int fogByDay = 0;
    for (std::int32_t day = 0; day < 400; ++day) {
        int starts = 0;
        std::int32_t lastStart = -100000;
        int firstBadMinute = -1;
        std::string why;
        const auto bad = [&](int minute, const char* what) {
            if (firstBadMinute < 0) {
                firstBadMinute = minute;
                why = what;
            }
        };
        render::Weather previous = render::weatherFor(kShippedSeed, day, 0);
        for (int minute = 0; minute < 1440; ++minute) {
            const render::WeatherPeriod period =
                render::weatherPeriodAt(kShippedSeed, day, minutes(minute));
            if (period.startMinute > minute || period.endMinute <= minute) {
                bad(minute, "the period does not cover the minute");
            }
            if (period.endMinute - period.startMinute < 240) {
                bad(minute, "a period shorter than four hours");
            }
            if (period.startMinute != lastStart) {
                if (period.startMinute < lastStart) {
                    bad(minute, "periods out of order");
                }
                lastStart = period.startMinute;
                if (period.startMinute >= 0) {
                    ++starts;
                    ++tally[static_cast<int>(period.kind)];
                    if (period.kind == WeatherKind::Fog) {
                        const std::int32_t mid = (period.startMinute + period.endMinute) / 2;
                        const std::int32_t hour = ((mid % 1440) + 1440) % 1440 / 60;
                        if (hour < 9 || hour >= 17) {
                            ++fogOnTheWater;
                        } else {
                            ++fogByDay;
                        }
                    }
                }
                if ((period.kind == WeatherKind::Clear) != (period.peak == 0.0F)) {
                    bad(minute, "a clear period with a peak, or a weather with none");
                }
                if (period.kind != WeatherKind::Clear && (period.peak < 0.55F || period.peak > 1.0F)) {
                    bad(minute, "a peak outside 0.55..1");
                }
            }
            const render::Weather now = render::weatherFor(kShippedSeed, day, minutes(minute));
            if (now.kind != period.kind) {
                bad(minute, "weatherFor and weatherPeriodAt disagree on the kind");
            }
            // Eased: never more than a few hundredths in a minute.
            if (std::fabs(now.intensity - previous.intensity) >= 0.03F) {
                bad(minute, "a step in the intensity");
            }
            // And a change of kind only ever happens at nothing -- where
            // every kind is clear -- so a period change is never a cut.
            if (now.kind != previous.kind && (now.intensity >= 0.01F || previous.intensity >= 0.01F)) {
                bad(minute, "the kind changed while something was showing");
            }
            previous = now;
        }
        CAPTURE(day);
        CAPTURE(firstBadMinute);
        CAPTURE(why);
        CHECK(firstBadMinute < 0);
        CHECK(starts >= 2);
        CHECK(starts <= 4);
    }
    // Clear leads. Fog is the most common thing that is not, and it is a
    // dawn, dusk and night thing: off the water, not over the noon quay.
    const int clear = tally[static_cast<int>(WeatherKind::Clear)];
    const int overcast = tally[static_cast<int>(WeatherKind::Overcast)];
    const int fog = tally[static_cast<int>(WeatherKind::Fog)];
    const int wind = tally[static_cast<int>(WeatherKind::Wind)];
    MESSAGE("four hundred days: clear " << clear << ", overcast " << overcast << ", fog " << fog
                                        << " (" << fogOnTheWater << " off the water, " << fogByDay
                                        << " by day), wind " << wind);
    CHECK(clear > fog);
    CHECK(fog > overcast);
    CHECK(fog > wind);
    CHECK(fogOnTheWater > 3 * fogByDay);
}

TEST_CASE("the weather's names round-trip, and nothing else parses") {
    for (int i = 0; i < render::kWeatherKindCount; ++i) {
        const auto kind = static_cast<WeatherKind>(i);
        WeatherKind back = WeatherKind::Wind;
        REQUIRE(render::parseWeatherKind(render::weatherKindName(kind), back));
        CHECK(back == kind);
    }
    CHECK(render::weatherKindName(WeatherKind::Clear) == "clear");
    CHECK(render::weatherKindName(WeatherKind::Overcast) == "overcast");
    CHECK(render::weatherKindName(WeatherKind::Fog) == "fog");
    CHECK(render::weatherKindName(WeatherKind::Wind) == "wind");
    WeatherKind untouched = WeatherKind::Overcast;
    CHECK_FALSE(render::parseWeatherKind("Fog", untouched));
    CHECK_FALSE(render::parseWeatherKind("rain", untouched));
    CHECK_FALSE(render::parseWeatherKind("", untouched));
    CHECK(untouched == WeatherKind::Overcast);
}

// ---------------------------------------------------------------------------
// the sky
// ---------------------------------------------------------------------------

TEST_CASE("the clear sky is the sky every earlier frame was drawn under, byte for byte") {
    render::Weather nothing;
    nothing.kind = WeatherKind::Fog;
    nothing.intensity = 0.0F;
    REQUIRE(nothing.clear());
    for (int minute = 0; minute < 1440; ++minute) {
        const render::SkyState old = render::skyAt(minutes(minute));
        CHECK(sameSky(old, render::skyAt(minutes(minute), render::Weather{})));
        // Any kind at nothing is the clear sky too.
        CHECK(sameSky(old, render::skyAt(minutes(minute), nothing)));
        CHECK(old.veil == 0.0F);
        CHECK(old.haloScale == 1.0F);
    }
}

TEST_CASE("fog swallows the district, overcast puts a lid on it, wind clears it and guts the lanterns") {
    const render::SkyState noon = render::skyAt(12 * 3600);
    const render::SkyState night = render::skyAt(0);

    SUBCASE("harbour fog") {
        const render::SkyState noonFog = render::skyAt(12 * 3600, pinned(WeatherKind::Fog, 1.0F));
        // Eight tiles by day at full, the veil the density over the clear day's.
        CHECK(noonFog.fogDistance == doctest::Approx(8.0F).epsilon(0.01));
        CHECK(noonFog.veil == doctest::Approx(1.0F / 8.0F - 1.0F / noon.fogDistance).epsilon(0.01));
        // Milky by day, and the fog IS the sky: the horizon goes to it.
        CHECK(noonFog.fog.r > 0.6F);
        CHECK(noonFog.fog.g > 0.6F);
        CHECK(noonFog.fog.b > 0.6F);
        CHECK(std::fabs(noonFog.skyHorizon.g - noonFog.fog.g) < 1.0e-5F);
        // Flatter light by day: the ambient a shade down, the daylight with it.
        CHECK(noonFog.ambient.g < noon.ambient.g);
        CHECK(noonFog.daylight < noon.daylight);
        CHECK(noonFog.haloScale == 1.0F);

        const render::SkyState nightFog = render::skyAt(0, pinned(WeatherKind::Fog, 1.0F));
        // Six and a half tiles at night; a cold grey-blue that lifts the blacks.
        CHECK(nightFog.fogDistance == doctest::Approx(6.5F).epsilon(0.01));
        CHECK(nightFog.fog.b > nightFog.fog.r);
        CHECK(nightFog.fog.b > night.fog.b);
        CHECK(nightFog.ambient.b > night.ambient.b);

        // Half a fog is half the DENSITY -- between the two, not a tenth of
        // the distance -- and the distance closes monotonically as it comes in.
        const render::SkyState half = render::skyAt(12 * 3600, pinned(WeatherKind::Fog, 0.5F));
        CHECK(half.fogDistance > noonFog.fogDistance);
        CHECK(half.fogDistance < noon.fogDistance);
        CHECK(half.veil == doctest::Approx(noonFog.veil * 0.5F).epsilon(0.01));
        float last = noon.fogDistance;
        for (int step = 1; step <= 10; ++step) {
            const float i = static_cast<float>(step) / 10.0F;
            const render::SkyState easing = render::skyAt(12 * 3600, pinned(WeatherKind::Fog, i));
            CHECK(easing.fogDistance < last);
            last = easing.fogDistance;
        }
    }

    SUBCASE("overcast") {
        const render::SkyState lid = render::skyAt(12 * 3600, pinned(WeatherKind::Overcast, 1.0F));
        // The band flattened: zenith and horizon closer than the clear day's.
        const float clearBand = std::fabs(noon.skyTop.g - noon.skyHorizon.g);
        const float lidBand = std::fabs(lid.skyTop.g - lid.skyHorizon.g);
        CHECK(lidBand < clearBand);
        // Cooler and a touch dimmer.
        CHECK(lid.ambient.r < noon.ambient.r);
        CHECK(lid.daylight < noon.daylight);
        // A little haze, no more than that: a fraction of the fog's veil.
        CHECK(lid.veil > 0.0F);
        const render::SkyState noonFog = render::skyAt(12 * 3600, pinned(WeatherKind::Fog, 1.0F));
        CHECK(lid.veil < noonFog.veil * 0.2F);
        CHECK(lid.haloScale == 1.0F);
    }

    SUBCASE("wind") {
        const render::SkyState blow = render::skyAt(12 * 3600, pinned(WeatherKind::Wind, 1.0F));
        // The air scoured clearer, no veil, the lanterns guttering.
        CHECK(blow.fogDistance > noon.fogDistance);
        CHECK(blow.veil == 0.0F);
        CHECK(blow.haloScale == doctest::Approx(0.7F));
        CHECK(blow.skyTop.b < noon.skyTop.b);
        // And what it does to the ear: the bed's multiplier.
        CHECK(pinned(WeatherKind::Wind, 1.0F).windGain() == doctest::Approx(2.4F));
        CHECK(pinned(WeatherKind::Wind, 0.5F).windGain() == doctest::Approx(1.7F));
        CHECK(pinned(WeatherKind::Fog, 1.0F).windGain() == doctest::Approx(0.65F));
        CHECK(pinned(WeatherKind::Overcast, 1.0F).windGain() == doctest::Approx(1.15F));
        CHECK(pinned(WeatherKind::Clear, 0.0F).windGain() == doctest::Approx(1.0F));
        CHECK(pinned(WeatherKind::Wind, 0.0F).windGain() == doctest::Approx(1.0F));
    }
}

// ---------------------------------------------------------------------------
// the session: the sim does not know
// ---------------------------------------------------------------------------

TEST_CASE("the weather never reaches the sim: fog, overcast and wind run to the clear run's digests") {
    render::Session clear(docksUnder(WeatherKind::Clear));
    CHECK(clear.weather().clear());
    CHECK_FALSE(clear.config().liveWeather);
    walk(clear);
    const std::uint64_t tavern = tavernDigest(clear);
    const std::uint64_t people = peopleDigest(clear);
    render::Framebuffer clearFrame(kWidth, kHeight);
    clear.drawFrame(clearFrame);

    const WeatherKind kinds[] = {WeatherKind::Fog, WeatherKind::Overcast, WeatherKind::Wind};
    for (const WeatherKind kind : kinds) {
        const std::string name(render::weatherKindName(kind));
        CAPTURE(name);
        render::Session under(docksUnder(kind));
        // Pinned at full for the whole run.
        CHECK(under.weather().kind == kind);
        CHECK(under.weather().intensity == 1.0F);
        walk(under);
        CHECK(under.weather().kind == kind);
        // The same steps went to the same place...
        CHECK(under.body().tileX() == clear.body().tileX());
        CHECK(under.body().tileY() == clear.body().tileY());
        CHECK(under.timeOfDay() == clear.timeOfDay());
        // ...and the hashed state is the clear run's, to the bit.
        CHECK(tavernDigest(under) == tavern);
        CHECK(peopleDigest(under) == people);
        // While the picture is a different picture.
        render::Framebuffer frame(kWidth, kHeight);
        under.drawFrame(frame);
        CHECK(frame.pixels() != clearFrame.pixels());
        // sky() is skyAt(the clock, weather()), the one every consumer reads.
        CHECK(sameSky(under.sky(), render::skyAt(under.timeOfDay(), under.weather())));
    }
}

TEST_CASE("two sessions under one fog draw one frame, byte for byte") {
    render::Session a(docksUnder(WeatherKind::Fog));
    render::Session b(docksUnder(WeatherKind::Fog));
    walk(a);
    walk(b);
    render::Framebuffer first(kWidth, kHeight);
    render::Framebuffer second(kWidth, kHeight);
    a.drawFrame(first);
    b.drawFrame(second);
    CHECK(first.pixels() == second.pixels());
    // And the same session again: the frame is a function of its inputs.
    render::Framebuffer again(kWidth, kHeight);
    a.drawFrame(again);
    CHECK(again.pixels() == first.pixels());
}

TEST_CASE("the capture summary says what sky the frame was shot under") {
    render::SmokeRunConfig run;
    run.session = docksUnder(WeatherKind::Fog);
    run.steps = 0;
    run.stamp = false;
    const render::SmokeRunResult fog = render::runSmoke(run);
    REQUIRE(fog.ok);
    CHECK(fog.summary.find("weather=fog 1.00") != std::string::npos);
    // The default is the sky every earlier capture had, and says so.
    run.session.weather = WeatherKind::Clear;
    const render::SmokeRunResult clear = render::runSmoke(run);
    REQUIRE(clear.ok);
    CHECK(clear.summary.find("weather=clear 0.00") != std::string::npos);
    CHECK(clear.summary.find("weather=fog") == std::string::npos);
}

// ---------------------------------------------------------------------------
// the 3D pass: the veil
// ---------------------------------------------------------------------------

TEST_CASE("the chunk version moves with the weather, and a clear version is what it always was") {
    ChunkLighting clear;
    clear.timeOfDaySeconds = 21 * 3600;
    CHECK(weatherVersionKey(clear.weather) == 0U);
    ChunkLighting fog = clear;
    fog.weather = pinned(WeatherKind::Fog, 1.0F);
    CHECK(weatherVersionKey(fog.weather) == (2U << 8) + 100U);
    CHECK(chunkVersion(0, clear) != chunkVersion(0, fog));
    CHECK(chunkVersion(0, fog) != 0U);
    // The intensity to a hundredth: half a fog is a different version, a
    // hair over half is the same one.
    ChunkLighting half = fog;
    half.weather.intensity = 0.5F;
    ChunkLighting hair = fog;
    hair.weather.intensity = 0.504F;
    CHECK(chunkVersion(0, half) != chunkVersion(0, fog));
    CHECK(chunkVersion(0, half) == chunkVersion(0, hair));
    // Nothing at all is clear, whatever the kind says.
    ChunkLighting nothing = fog;
    nothing.weather.intensity = 0.0F;
    CHECK(weatherVersionKey(nothing.weather) == 0U);
    CHECK(chunkVersion(0, nothing) == chunkVersion(0, clear));
}

TEST_CASE("the veil is nested shells round the eye, far to near, and none at all in clear air") {
    REQUIRE(veilShellCount() == 19U);
    CHECK(veilShellRadius(0) == doctest::Approx(1.5F));
    CHECK(veilShellRadius(veilShellCount() - 1) == doctest::Approx(56.0F));
    for (std::size_t shell = 1; shell < veilShellCount(); ++shell) {
        CHECK(veilShellRadius(shell) > veilShellRadius(shell - 1));
    }
    CHECK(veilShellRadius(veilShellCount()) == 0.0F);

    // Clear air: the mesh with nothing in it.
    const MeshData none = buildVeil(render::skyAt(0), 7U);
    CHECK(none.id == kVeilMeshId);
    CHECK(none.version == 7U);
    CHECK(none.vertexCount() == 0U);
    CHECK(none.triangleCount() == 0U);

    // A night fog at full: every shell worth drawing, every one translucent.
    const render::SkyState fogged = render::skyAt(0, pinned(WeatherKind::Fog, 1.0F));
    REQUIRE(fogged.veil > 0.0F);
    const MeshData veil = buildVeil(fogged, 7U);
    REQUIRE(veil.triangleCount() > 0U);
    constexpr std::size_t kRingVertices = 11U * 24U;
    REQUIRE(veil.vertexCount() % kRingVertices == 0U);
    CHECK(veil.vertexCount() / kRingVertices == veilShellCount());
    REQUIRE(veil.colours.size() == veil.vertexCount() * 4U);
    bool opaque = false;
    bool invisible = false;
    for (std::size_t v = 0; v < veil.vertexCount(); ++v) {
        const std::uint8_t alpha = veil.colours[v * 4U + 3U];
        opaque = opaque || alpha == 255U;
        invisible = invisible || alpha == 0U;
    }
    CHECK_FALSE(opaque);
    CHECK_FALSE(invisible);
    // FAR TO NEAR in the index buffer: the outermost shell first, the
    // innermost last, so the blend composes outside in.
    REQUIRE(!veil.indices.empty());
    CHECK(radiusOf(veil, veil.indices.front()) == doctest::Approx(56.0F).epsilon(0.01));
    CHECK(radiusOf(veil, veil.indices.back()) == doctest::Approx(1.5F).epsilon(0.01));
    CHECK(radiusOf(veil, veil.indices.front()) > radiusOf(veil, veil.indices.back()));
    // Below the horizon a ring is the fog's colour; at the zenith the sky's
    // own, so the sky seen through every shell is still the sky.
    const auto closeTo = [&](std::size_t vertex, const render::Rgb& colour) {
        const std::size_t at = vertex * 4U;
        return std::abs(static_cast<int>(veil.colours[at]) - static_cast<int>(colour.r * 255.0F + 0.5F)) <= 1 &&
               std::abs(static_cast<int>(veil.colours[at + 1U]) - static_cast<int>(colour.g * 255.0F + 0.5F)) <= 1 &&
               std::abs(static_cast<int>(veil.colours[at + 2U]) - static_cast<int>(colour.b * 255.0F + 0.5F)) <= 1;
    };
    CHECK(closeTo(0U, fogged.fog));             // the first shell's bottom pole
    CHECK(closeTo(10U * 24U, fogged.skyTop));   // its top pole
}

TEST_CASE("a fog is in the description, a clear description has no veil in it, and twice is once") {
    const sim::TileQuery tiles(docksWorld());
    const render::TileAtlas& atlas = proceduralAtlas();
    const render::Camera eye = spawnCamera();
    WorldSceneParams clearParams;
    clearParams.timeOfDaySeconds = 21 * 3600;

    WorldScene clearWorld(tiles, atlas, nullptr);
    SceneDescription clearScene;
    clearWorld.refresh(clearScene, eye, kAspect, clearParams);
    CHECK(clearScene.veils.empty());
    CHECK(clearScene.findMesh(kVeilMeshId) == nullptr);
    REQUIRE(clearScene.findMesh(kSkyMeshId) != nullptr);
    // A clear dome's version is what it always was: the minute bucket alone.
    CHECK(clearScene.findMesh(kSkyMeshId)->version == skyDomeVersion(21 * 3600));
    const std::uint64_t clearHash = sceneHash(clearScene);

    WorldSceneParams fogParams = clearParams;
    fogParams.weather = pinned(WeatherKind::Fog, 1.0F);
    WorldScene fogWorld(tiles, atlas, nullptr);
    SceneDescription fogScene;
    fogWorld.refresh(fogScene, eye, kAspect, fogParams);
    REQUIRE(fogScene.veils.size() == 1U);
    CHECK(fogScene.veils[0].meshId == kVeilMeshId);
    // The veil rides the eye, as the dome does.
    CHECK(fogScene.veils[0].position.x == doctest::Approx(fogScene.camera.position.x));
    CHECK(fogScene.veils[0].position.y == doctest::Approx(fogScene.camera.position.y));
    CHECK(fogScene.veils[0].position.z == doctest::Approx(fogScene.camera.position.z));
    const MeshData* veil = fogScene.findMesh(kVeilMeshId);
    REQUIRE(veil != nullptr);
    CHECK(veil->triangleCount() > 0U);
    // The dome under the fog is a different dome, and says so in its version.
    REQUIRE(fogScene.findMesh(kSkyMeshId) != nullptr);
    CHECK(fogScene.findMesh(kSkyMeshId)->version != skyDomeVersion(21 * 3600));
    CHECK(sceneHash(fogScene) != clearHash);
    // The chunks recoloured under it: the same vertices, other colours.
    const ChunkKey spawnChunk = chunkOf(sim::docks::kSpawnTileX, sim::docks::kSpawnTileY);
    const MeshData* clearChunk = clearScene.findMesh(chunkMeshId(spawnChunk));
    const MeshData* fogChunk = fogScene.findMesh(chunkMeshId(spawnChunk));
    REQUIRE(clearChunk != nullptr);
    REQUIRE(fogChunk != nullptr);
    CHECK(fogChunk->positions == clearChunk->positions);
    CHECK(fogChunk->colours != clearChunk->colours);

    // Built again from scratch under the same fog: the same description,
    // byte for byte.
    WorldScene again(tiles, atlas, nullptr);
    SceneDescription twice;
    again.refresh(twice, eye, kAspect, fogParams);
    CHECK(sceneHash(twice) == sceneHash(fogScene));

    // And the fog lifting takes the veil with it: the same scene refreshed
    // clear has no veil mesh, no veil instance, and the clear description's
    // own hash -- a clear frame is the frame it was.
    fogWorld.refresh(fogScene, eye, kAspect, clearParams);
    CHECK(fogScene.veils.empty());
    CHECK(fogScene.findMesh(kVeilMeshId) == nullptr);
    CHECK(sceneHash(fogScene) == clearHash);
}

TEST_CASE("the fog is in the 3D frame, headless through rlsw, and the same bytes twice") {
    if (!Backend::headlessCapable()) {
        MESSAGE("skipped: this build renders through a GPU window, not rlsw");
        return;
    }
    const sim::TileQuery tiles(docksWorld());
    const render::TileAtlas& atlas = proceduralAtlas();
    const render::Camera eye = spawnCamera();
    // Noon, where a milky fog over the district is the loudest it gets.
    WorldSceneParams clearParams;
    clearParams.timeOfDaySeconds = 12 * 3600;
    WorldSceneParams fogParams = clearParams;
    fogParams.weather = pinned(WeatherKind::Fog, 1.0F);

    WorldScene world(tiles, atlas, nullptr);
    SceneDescription clearScene;
    world.refresh(clearScene, eye, kAspect, clearParams);
    WorldScene fogWorld(tiles, atlas, nullptr);
    SceneDescription fogScene;
    fogWorld.refresh(fogScene, eye, kAspect, fogParams);
    REQUIRE(fogScene.veils.size() == 1U);

    std::unique_ptr<Backend> video = Backend::open(headlessConfig());
    REQUIRE(video != nullptr);
    const render::Framebuffer clearFrame = drawOnce(*video, clearScene, nullptr);
    SceneStats stats;
    const render::Framebuffer fogFrame = drawOnce(*video, fogScene, &stats);
    REQUIRE(fogFrame.pixels().size() == clearFrame.pixels().size());
    // The veil went through the same mesh path as any instance, last.
    CHECK(stats.instancesDrawn == fogScene.instances.size() + fogScene.veils.size());
    // And it is in the picture: most of the frame moved.
    std::size_t moved = 0;
    for (std::size_t i = 0; i < fogFrame.pixels().size(); ++i) {
        if (fogFrame.pixels()[i] != clearFrame.pixels()[i]) {
            ++moved;
        }
    }
    MESSAGE("fog moved " << moved << " of " << fogFrame.pixels().size() << " pixels");
    CHECK(moved > fogFrame.pixels().size() / 2);
    // The same bytes again.
    const render::Framebuffer again = drawOnce(*video, fogScene, nullptr);
    CHECK(again.pixels() == fogFrame.pixels());
}

// ---------------------------------------------------------------------------
// the ear
// ---------------------------------------------------------------------------

TEST_CASE("the wind bed follows the weather, one way: a blow lifts it, a still fog drops it") {
    auto still = AudioEngine::createNull(17, SoundBank::synthetic());
    auto blown = AudioEngine::createNull(17, SoundBank::synthetic());
    auto fogged = AudioEngine::createNull(17, SoundBank::synthetic());
    for (AudioEngine* engine : {still.get(), blown.get(), fogged.get()}) {
        engine->setTimeOfDay(2 * 3600);
        engine->startBed(BedId::Harbour, 0.1F);
    }
    CHECK(still->wind() == 1.0F);
    blown->setWind(pinned(WeatherKind::Wind, 1.0F).windGain());
    fogged->setWind(pinned(WeatherKind::Fog, 1.0F).windGain());
    for (int i = 0; i < 40; ++i) {
        for (AudioEngine* engine : {still.get(), blown.get(), fogged.get()}) {
            engine->update(0.05F);
            drain(*engine, 2400);
        }
    }
    const float stillWind = still->mixer().proceduralGain(ProcLayer::Wind);
    const float blownWind = blown->mixer().proceduralGain(ProcLayer::Wind);
    const float foggedWind = fogged->mixer().proceduralGain(ProcLayer::Wind);
    MESSAGE("wind bed: still " << stillWind << ", blown " << blownWind << ", fogged " << foggedWind);
    CHECK(blownWind > stillWind);
    CHECK(foggedWind < stillWind);
    CHECK(foggedWind > 0.0F);
    // The mixer's cap still holds over a blow.
    CHECK(blownWind <= 1.0F + 1.0e-4F);
    // The wind's own layer and no other: the water is the water under it.
    CHECK(blown->mixer().proceduralGain(ProcLayer::WaterLap) ==
          doctest::Approx(still->mixer().proceduralGain(ProcLayer::WaterLap)));
    // And the multiplier is clamped to something a mixer can take.
    still->setWind(9.0F);
    CHECK(still->wind() == 4.0F);
    still->setWind(-1.0F);
    CHECK(still->wind() == 0.0F);
}
