// The lamp bake: the scanner that reads the authored markers, the file it
// writes, and the 27 lights that are actually in the Docks.

#include <doctest/doctest.h>

#include <algorithm>
#include <map>
#include <string>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/lamps.hpp"
#include "granadad/sim/docks.hpp"

using namespace granadad::render;

namespace {

/// A miniature .tmx exercising everything the real one does: nested groups, a
/// z-group naming convention with a non-zero minimum, an object layer that is
/// NOT `markers`, a non-light object, a light with no luminance, one out of
/// range, and the fire/lantern naming split.
constexpr const char* kFixtureTmx = R"TMX(<?xml version="1.0" encoding="UTF-8"?>
<map version="1.10" tiledversion="1.10.2" orientation="orthogonal" width="64" height="64"
     tilewidth="16" tileheight="16">
 <group id="1" name="world">
  <group id="2" name="z:+3">
   <objectgroup id="10" name="props">
    <object id="1" name="lamp_decoy" type="light_source" x="16" y="16">
     <properties><property name="luminance" value="20"/></properties>
    </object>
   </objectgroup>
   <objectgroup id="11" name="markers">
    <object id="2" name="lamp_quay_door" type="light_source" x="160" y="320">
     <properties><property name="luminance" value="18"/></properties>
    </object>
    <object id="3" name="brazier_plaza" type="light_source" x="32.5" y="48.75">
     <properties><property name="luminance" value="16"/></properties>
    </object>
    <object id="4" name="spawn_point" type="marker" x="80" y="80"/>
    <object id="5" name="lamp_broken" type="light_source" x="96" y="96"/>
    <object id="6" name="lamp_overbright" type="light_source" x="112" y="112">
     <properties><property name="luminance" value="99"/></properties>
    </object>
   </objectgroup>
  </group>
  <group id="3" name="z:+5">
   <objectgroup id="12" name="markers">
    <object id="7" name="lamp_roof" type="light_source" x="0" y="0">
     <properties><property name="luminance" value="26"/></properties>
    </object>
   </objectgroup>
  </group>
 </group>
</map>
)TMX";

}  // namespace

TEST_CASE("the marker name decides fire from lantern, the way the authoring does") {
    CHECK(isFireLampName("brazier_tarwalk_mid"));
    CHECK(isFireLampName("lamp_pitchfield_cauldron"));
    CHECK(isFireLampName("lamp_hardtack_oven"));
    CHECK(isFireLampName("lamp_shrine_candles"));
    CHECK(isFireLampName("TORCH_upper"));
    CHECK(isFireLampName("hall_hearth"));
    CHECK(isFireLampName("watchfire"));
    CHECK_FALSE(isFireLampName("lamp_gull_door"));
    CHECK_FALSE(isFireLampName("lamp_eelpot_01"));
    CHECK_FALSE(isFireLampName(""));
    CHECK(lampWarmthName(LampWarmth::Fire) == "fire");
    CHECK(lampWarmthName(LampWarmth::Lantern) == "lantern");
}

TEST_CASE("the tmx scanner reads only the markers layer, and only usable lights") {
    const std::vector<Lamp> lamps = scanTmxLightSources(kFixtureTmx);
    REQUIRE(lamps.size() == 3);

    // Only the `markers` object layer counts: the identically-typed object on
    // the `props` layer is ignored.
    for (const Lamp& lamp : lamps) {
        CHECK(lamp.name != "lamp_decoy");
    }
    // A light with no luminance property, and one outside 0..31, are both
    // dropped rather than guessed at.
    for (const Lamp& lamp : lamps) {
        CHECK(lamp.name != "lamp_broken");
        CHECK(lamp.name != "lamp_overbright");
    }

    // Placement: pixel/16 floored, plus the world's one-chunk VOID border, and
    // z relative to the SMALLEST z-group in the file (here z:+3).
    CHECK(lamps[0].name == "lamp_quay_door");
    CHECK(lamps[0].x == 32 + 10);
    CHECK(lamps[0].y == 32 + 20);
    CHECK(lamps[0].z == 8 + 0);
    CHECK(lamps[0].luminance == 18);
    CHECK(lamps[0].warmth == LampWarmth::Lantern);

    // A fractional pixel coordinate floors to its tile.
    CHECK(lamps[1].name == "brazier_plaza");
    CHECK(lamps[1].x == 32 + 2);
    CHECK(lamps[1].y == 32 + 3);
    CHECK(lamps[1].warmth == LampWarmth::Fire);

    // The higher z-group lands two levels up.
    CHECK(lamps[2].name == "lamp_roof");
    CHECK(lamps[2].z == 8 + 2);
    CHECK(lamps[2].luminance == 26);
}

TEST_CASE("the bake round-trips, byte for byte") {
    const std::vector<Lamp> lamps = scanTmxLightSources(kFixtureTmx);
    const std::string text = writeLampBake("fixture", "maps/src/fixture.tmx", lamps);
    CHECK(text.back() == '\n');

    const std::vector<Lamp> back = readLampBake(text);
    REQUIRE(back.size() == lamps.size());
    for (std::size_t i = 0; i < lamps.size(); ++i) {
        CAPTURE(i);
        CHECK(back[i].name == lamps[i].name);
        CHECK(back[i].x == lamps[i].x);
        CHECK(back[i].y == lamps[i].y);
        CHECK(back[i].z == lamps[i].z);
        CHECK(back[i].luminance == lamps[i].luminance);
        CHECK(back[i].warmth == lamps[i].warmth);
    }
    // Writing what was read reproduces the same bytes, so a re-bake with no
    // authoring change is an empty diff.
    CHECK(writeLampBake("fixture", "maps/src/fixture.tmx", back) == text);
}

TEST_CASE("a missing or broken bake degrades to darkness, never to a crash") {
    // The Java loader's one genuinely right decision: a presentation nicety
    // must not be able to stop the game booting.
    CHECK(loadLamps("/definitely-not-a-directory", "docks_surface").empty());
    CHECK(loadLamps(granadad::content::contentDir(), "no_such_world").empty());
    CHECK_THROWS(readLampBake("not json at all"));
    CHECK_THROWS(readLampBake("{\"schemaVersion\":1}"));
}

TEST_CASE("the shipped Docks bake carries the 28 authored light sources") {
    const std::vector<Lamp> lamps =
        loadLamps(granadad::content::contentDir(), granadad::sim::docks::kWorldName);
    REQUIRE(lamps.size() == 28);

    std::map<std::int32_t, int> byBand;
    int fire = 0;
    for (const Lamp& lamp : lamps) {
        CHECK(lamp.luminance >= 0);
        CHECK(lamp.luminance <= 31);
        CHECK_FALSE(lamp.name.empty());
        ++byBand[lamp.z];
        if (lamp.warmth == LampWarmth::Fire) {
            ++fire;
        }
    }
    // Overwhelmingly a quayside district: 20 lights on Tarwalk and the piers,
    // 2 on the mid slope, 4 up the rise -- and, since District Phase B, ONE
    // above the roofline.
    //
    // The quayside count was 21 and the band count was 3. Phase B (Thresholds)
    // raised the Mission of the Flame a lantern-turret and took its doctrinal
    // night lamp up with it, z19 -> z23: same lamp, same name (which is what
    // keeps world_renderer.cpp's beacon exception finding it), same (x, y)
    // bracket cell, four bands higher. So one light leaves the quayside row and
    // arrives on a band of its own -- and z23 is the top authorable level of
    // this world, which is the whole point: the ward's doctrinal flame is now
    // the highest lit thing in it. The ward's other beacon, lamp_weighhouse_mast,
    // is already a z21 signal-mast lamp rather than a door lamp; this is that
    // same idea carried as far as the format allows.
    //
    // DISTRICT PHASE C (Quarters) adds the twenty-eighth, and it is the one Phase B's own
    // report flagged and declined to author. The 7.2 light-law audit found the law holding
    // everywhere (waterline mean luminance 14.0 against up-slope 17.5, and ZERO lights inside
    // the Gullet box, which is the darkness the owner ruled is authored identity) except at
    // the Mission of the Flame, whose alms-hall doors went dark when the doctrinal beacon
    // rode the turret up to z23. lamp_mission_door restores the ward's own answer to that
    // shape: the Weighhouse has carried a mast lamp AND a ground brazier since it was
    // authored. Same bracket cell the beacon left, quayside band, so the quayside row goes
    // 20 -> 21 and no other band moves. The name is deliberately NOT the beacon's --
    // world_renderer.cpp's fog exception matches lamp names exactly, and a door lamp must not
    // be able to steal the tag.
    CHECK(byBand[granadad::sim::docks::kBandQuayside] == 21);
    CHECK(byBand[granadad::sim::docks::kBandMidSlope] == 2);
    CHECK(byBand[granadad::sim::docks::kBandUpper] == 4);
    CHECK(byBand[23] == 1);
    CHECK(byBand.size() == 4);
    // Eight open flames -- four braziers, a cauldron, an oven, the shrine
    // candles and the watchpost brazier. The rest are shielded lanterns, and the
    // new Mission door lamp is one of them: "lamp_mission_door" carries none of
    // the fire words isFireLampName matches on, so the fire count does not move.
    CHECK(fire == 8);
    CHECK(static_cast<int>(lamps.size()) - fire == 20);

    // Named landmarks, at the tiles the authored map puts them on. If the
    // border offset or the pixel-to-tile floor ever drifts, these move.
    const auto find = [&lamps](const std::string& name) -> const Lamp* {
        for (const Lamp& lamp : lamps) {
            if (lamp.name == name) {
                return &lamp;
            }
        }
        return nullptr;
    };
    const Lamp* mast = find("lamp_weighhouse_mast");
    REQUIRE(mast != nullptr);
    CHECK(mast->x == 96);
    CHECK(mast->y == 67);
    CHECK(mast->z == 21);
    CHECK(mast->luminance == 26);  // the brightest thing in the district

    // The other beacon, and since Phase B the HIGHEST authored light in the
    // world. Pinned by name and by band because both are load-bearing: the name
    // is what world_renderer.cpp's fog exception matches on, and the band is the
    // deliverable -- a beacon at the skyline rather than over a doorway.
    const Lamp* mission = find("lamp_mission_night");
    REQUIRE(mission != nullptr);
    CHECK(mission->x == 120);
    CHECK(mission->y == 97);
    CHECK(mission->z == 23);
    CHECK(mission->warmth == LampWarmth::Lantern);
    for (const Lamp& lamp : lamps) {
        CHECK(lamp.z <= mission->z);
    }

    // The ground half of the Mission's pair: same (x, y) as the beacon, four bands
    // under it, and dimmer on purpose -- the Weighhouse's mast (26) outshines its
    // plaza brazier (16), and this pair keeps that ordering at 22 against 18.
    const Lamp* missionDoor = find("lamp_mission_door");
    REQUIRE(missionDoor != nullptr);
    CHECK(missionDoor->x == mission->x);
    CHECK(missionDoor->y == mission->y);
    CHECK(missionDoor->z == granadad::sim::docks::kBandQuayside);
    CHECK(missionDoor->luminance == 18);
    CHECK(missionDoor->luminance < mission->luminance);
    CHECK(missionDoor->warmth == LampWarmth::Lantern);

    const Lamp* eelpot = find("lamp_eelpot_01");
    REQUIRE(eelpot != nullptr);
    CHECK(eelpot->y == 64);
    CHECK(eelpot->z == granadad::sim::docks::kBandQuayside);
    CHECK(eelpot->warmth == LampWarmth::Lantern);

    const Lamp* brazier = find("brazier_tarwalk_mid");
    REQUIRE(brazier != nullptr);
    CHECK(brazier->warmth == LampWarmth::Fire);
    CHECK(brazier->z == granadad::sim::docks::kBandQuayside);
}
