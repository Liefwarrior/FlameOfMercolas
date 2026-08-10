#pragma once

// GENERATED FILE -- DO NOT EDIT BY HAND.
//
// Produced by tools/scripts/gen_docks_signs.py from the `place_sign`
// markers in content/maps/src/docks_surface.tmx. Re-run the script and
// commit the diff if that .tmx changes.
//
// 83 signs: every real name a mapper already authored for a
// Docks building door or street post. World tile x/y = local + 32, world
// band = local z-group + 8 -- the same VOID-border rule docks.hpp's own
// kPlaces table uses (docks.hpp:11-13), verified against sign_k03_gilded_gull
// producing an exact match for kPlaces' "THE GILDED GULL" rect.

#include <cstdint>
#include <cstddef>

namespace granadad::sim::docks {

enum class SignKind { Door, Way };

struct Sign {
    const char* id;      // the .tmx object's own name, for debugging/lookup
    const char* place;   // real display name authored in the .tmx
    const char* what;    // one-line flavour text
    SignKind kind;
    float anchorX;       // world tile: where the sign itself stands
    float anchorY;
    std::int32_t band;
    std::int32_t x0, y0, x1, y1;  // world tile footprint rectangle
};

inline constexpr Sign kSigns[] = {
    {"sign_c1_quayward", "The Quayward Compound", "Ceffa Quayward holds the charge", SignKind::Door, 104.500F, 137.500F, 20, 40, 129, 103, 147},
    {"sign_c2_netter_house", "The Netter house", "hers; the ground is not", SignKind::Door, 160.500F, 111.500F, 19, 148, 98, 159, 125},
    {"sign_c2_netters", "The Netters' Compound", "the charge is pledged at Fenner's", SignKind::Door, 171.500F, 97.500F, 19, 148, 98, 191, 125},
    {"sign_c3_saltgate_terrace", "Saltgate Terrace", "Tarl Saltgate holds the charge", SignKind::Door, 143.500F, 132.500F, 20, 116, 133, 157, 147},
    {"sign_c4_gullet", "The Gullet Compound", "the charge is vacant; ask Mag", SignKind::Door, 209.500F, 97.500F, 19, 198, 98, 222, 125},
    {"sign_k01_weighhouse", "The Weighhouse", "harbormaster and customs house", SignKind::Door, 95.500F, 65.500F, 19, 88, 66, 103, 82},
    {"sign_k02_impound", "Impound Yard", "seized cargo behind a spike fence", SignKind::Door, 94.500F, 84.500F, 19, 88, 85, 100, 90},
    {"sign_k03_gilded_gull", "The Gilded Gull", "captains' tavern", SignKind::Door, 153.500F, 65.500F, 19, 146, 66, 160, 79},
    {"sign_k04_bilge", "The Bilge", "sailor dive on the Tarwalk", SignKind::Door, 137.500F, 65.500F, 19, 132, 66, 143, 75},
    {"sign_k05_lantern_room", "The Lantern Room", "the neutral-ground house", SignKind::Door, 95.500F, 97.500F, 19, 90, 98, 102, 109},
    {"sign_k06_harls_yard", "Harl's Yard", "shipwright: slipway and saw pit", SignKind::Door, 171.500F, 67.500F, 19, 168, 68, 191, 78},
    {"sign_k07_ropewalk", "The Ropewalk", "the shed where cable is laid", SignKind::Door, 35.500F, 117.500F, 19, 36, 114, 99, 122},
    {"sign_k08_branns", "Brann's Chandlery", "ship stores", SignKind::Door, 59.500F, 97.500F, 19, 56, 98, 63, 106},
    {"sign_k09_pitchfield", "Pitchfield", "the tar and pitch yard", SignKind::Door, 47.500F, 67.500F, 19, 34, 68, 61, 90},
    {"sign_k10_dawnstalls", "Dawnstalls", "the dawn fish auction", SignKind::Door, 78.500F, 69.500F, 19, 69, 68, 85, 80},
    {"sign_k11_saltrow", "Salt Row", "gutting sheds and smokehouses", SignKind::Door, 77.500F, 81.500F, 19, 70, 82, 83, 90},
    {"sign_k12_kingsbond", "The King's Bond", "bonded warehouse", SignKind::Door, 121.500F, 65.500F, 19, 114, 66, 130, 78},
    {"sign_k13_drowned_hold", "The Drowned Hold", "condemned; officially empty 9 years", SignKind::Door, 209.500F, 81.500F, 19, 210, 66, 221, 86},
    {"sign_k14_wrackhouse", "Wrackhouse", "salvage broker", SignKind::Door, 200.500F, 65.500F, 19, 196, 66, 205, 74},
    {"sign_k15_fenners", "Fenner's Pawn", "pawn, and wage-advance in the back", SignKind::Door, 157.500F, 83.500F, 19, 154, 84, 160, 90},
    {"sign_k16_drowned_name_wall", "The Drowned-Name Wall", "the sailors' shrine", SignKind::Door, 127.500F, 59.500F, 19, 126, 57, 128, 59},
    {"sign_k17_mission", "Mission of the Flame", "Divine Light almshouse", SignKind::Door, 120.500F, 97.500F, 19, 114, 98, 130, 112},
    {"sign_k18_bathhouse", "Squall's Bathhouse", "copper boilers and steam", SignKind::Door, 139.500F, 97.500F, 19, 134, 98, 144, 110},
    {"sign_k19_rows", "The Rows", "hammock-space by the night", SignKind::Door, 136.500F, 83.500F, 19, 132, 84, 151, 90},
    {"sign_k20_merles", "Merle's Boats", "boathouse and waterman's hire", SignKind::Door, 118.500F, 58.500F, 19, 114, 46, 125, 57},
    {"sign_k21_watchpost", "Saltgate Watch-Post", "the ward's one watch station", SignKind::Door, 104.500F, 152.500F, 21, 94, 149, 103, 158},
    {"sign_k22_netmenders", "Netmenders' Arcade", "needles, and the ward's memory", SignKind::Door, 77.500F, 65.500F, 19, 70, 66, 84, 67},
    {"sign_k23_coopers", "Cooper and Blockmaker", "barrels and pulley-blocks", SignKind::Door, 78.500F, 97.500F, 19, 72, 98, 85, 108},
    {"sign_k24_eelpots", "The Eel-Pots", "night food stalls", SignKind::Door, 129.500F, 65.500F, 19, 116, 62, 143, 64},
    {"sign_k25_kennelrow", "Kennel Row", "the rat-catchers' yard", SignKind::Door, 199.500F, 79.500F, 19, 196, 80, 206, 87},
    {"sign_k26_sailmaker", "Sailmaker's Loft", "net-mending and sail repair", SignKind::Door, 43.500F, 101.500F, 19, 40, 102, 47, 109},
    {"sign_k27_hardtack", "The Hardtack Oven", "ship's-biscuit bakery", SignKind::Door, 67.500F, 101.500F, 19, 64, 102, 70, 110},
    {"sign_k28_slopchest", "The Slop-Chest", "sailors' clothing and dry-goods", SignKind::Door, 165.500F, 89.500F, 19, 162, 90, 167, 96},
    {"sign_k29_longstore", "The Long Store", "general dry-goods warehouse", SignKind::Door, 119.500F, 113.500F, 19, 111, 114, 129, 124},
    {"sign_k30_kestrel", "The Kestrel", "moored at the fishbone pier", SignKind::Door, 98.500F, 52.500F, 19, 94, 50, 103, 54},
    {"sign_k31_breggas_promise", "Bregga's Promise", "moored at the fishbone pier", SignKind::Door, 96.500F, 45.500F, 19, 90, 42, 103, 48},
    {"sign_k32_deep_keel", "The Deep Keel", "moored at the fishbone pier", SignKind::Door, 95.500F, 36.500F, 19, 88, 33, 103, 40},
    {"sign_k33_widows_grief", "The Widow's Grief", "a wreck under the condemned pier", SignKind::Door, 159.500F, 46.500F, 19, 157, 42, 161, 50},
    {"sign_k34_guardhouse", "Guardhouse", "Militia Watch: nine steel cells", SignKind::Door, 138.500F, 111.500F, 19, 132, 112, 144, 124},
    {"sign_k36_counting_house", "The Royal Counting-House", "Master Gilt's; never once robbed", SignKind::Door, 186.500F, 79.500F, 19, 182, 80, 191, 91},
    {"sign_w_beaching_strand_east", "The Beaching Strand", "it shades into mudflats at low tide", SignKind::Way, 188.500F, 56.500F, 18, 179, 42, 195, 60},
    {"sign_w_beaching_strand_west", "The Beaching Strand", "shingle beach; hulls are careened", SignKind::Way, 166.500F, 52.500F, 18, 162, 42, 178, 60},
    {"sign_w_fishbone_finger_one", "First Finger", "a finger of the fishbone pier", SignKind::Way, 98.500F, 41.500F, 19, 92, 41, 123, 41},
    {"sign_w_fishbone_finger_three", "Third Finger", "a finger of the fishbone pier", SignKind::Way, 98.500F, 55.500F, 19, 94, 55, 113, 55},
    {"sign_w_fishbone_finger_two", "Second Finger", "a finger of the fishbone pier", SignKind::Way, 98.500F, 49.500F, 19, 94, 49, 113, 49},
    {"sign_w_fishbone_pier", "The Fishbone Pier", "the spine's water-ward continuation", SignKind::Way, 107.500F, 44.500F, 19, 104, 33, 111, 57},
    {"sign_w_glebe_eastfield", "Flame ground", "no charge is let; this is the glebe", SignKind::Way, 175.500F, 136.500F, 20, 176, 133, 220, 143},
    {"sign_w_glebe_gallows", "Flame ground", "no charge is let; this is the glebe", SignKind::Way, 41.500F, 153.500F, 21, 40, 148, 213, 158},
    {"sign_w_glebe_quayback", "Flame ground", "no charge is let; this is the glebe", SignKind::Way, 113.500F, 87.500F, 19, 114, 84, 129, 91},
    {"sign_w_gullet_bottom", "The Gullet", "half its doors nailed shut", SignKind::Way, 212.500F, 90.500F, 19, 194, 89, 223, 91},
    {"sign_w_gullet_g1n", "The Gullet", "wastrel territory: narrow, unlit", SignKind::Way, 192.500F, 68.500F, 19, 192, 62, 193, 76},
    {"sign_w_gullet_g1s", "The Gullet", "wastrel territory: narrow, unlit", SignKind::Way, 192.500F, 84.500F, 19, 192, 77, 193, 91},
    {"sign_w_gullet_g2", "The Gullet", "wastrel territory: narrow, unlit", SignKind::Way, 208.500F, 78.500F, 19, 208, 66, 209, 90},
    {"sign_w_gullet_g3", "The Gullet", "half its doors nailed shut", SignKind::Way, 196.500F, 112.500F, 19, 196, 98, 197, 127},
    {"sign_w_herring_lane_north", "Herring Lane", "fish market and salting sheds", SignKind::Way, 65.500F, 72.500F, 19, 64, 66, 67, 78},
    {"sign_w_herring_lane_south", "Herring Lane", "an offal drain down its gutter", SignKind::Way, 65.500F, 85.500F, 19, 64, 79, 67, 91},
    {"sign_w_long_quay_east", "The Long Quay", "stone seawall quay, 3 berths", SignKind::Way, 92.500F, 59.500F, 19, 80, 58, 103, 59},
    {"sign_w_long_quay_mid", "The Long Quay", "mooring posts and a crane gantry", SignKind::Way, 62.500F, 59.500F, 19, 56, 58, 79, 59},
    {"sign_w_long_quay_west", "The Long Quay", "stone seawall quay, 3 berths", SignKind::Way, 44.500F, 59.500F, 19, 32, 58, 55, 59},
    {"sign_w_outfall", "The Outfall", "the sewer mouth in the seawall", SignKind::Way, 203.500F, 58.500F, 19, 202, 57, 205, 58},
    {"sign_w_pier_row", "Pier Row", "four timber finger-piers", SignKind::Way, 131.500F, 56.500F, 19, 130, 36, 156, 57},
    {"sign_w_ropewynd_e1", "Ropewynd", "a long straight run", SignKind::Way, 120.500F, 94.500F, 19, 112, 92, 136, 97},
    {"sign_w_ropewynd_e2", "Ropewynd", "a long straight run", SignKind::Way, 148.500F, 94.500F, 19, 137, 92, 161, 97},
    {"sign_w_ropewynd_e3", "Ropewynd", "the fitting trades, one block up", SignKind::Way, 174.500F, 94.500F, 19, 162, 92, 185, 97},
    {"sign_w_ropewynd_e4", "Ropewynd", "where the paving gives out", SignKind::Way, 198.500F, 94.500F, 19, 186, 92, 209, 97},
    {"sign_w_ropewynd_w1", "Ropewynd", "the fitting trades, one block up", SignKind::Way, 48.500F, 94.500F, 19, 36, 92, 68, 97},
    {"sign_w_ropewynd_w2", "Ropewynd", "the fitting trades, one block up", SignKind::Way, 84.500F, 94.500F, 19, 69, 92, 101, 97},
    {"sign_w_saltgate_rise_cross", "Saltgate Rise", "the chokepoint road", SignKind::Way, 107.500F, 88.500F, 19, 104, 78, 111, 97},
    {"sign_w_saltgate_rise_head", "Saltgate Rise", "the notice board and the gibbet cage", SignKind::Way, 107.500F, 153.500F, 21, 104, 149, 111, 159},
    {"sign_w_saltgate_rise_mid", "Saltgate Rise", "climbing to the Inner Wall gate", SignKind::Way, 106.500F, 104.500F, 19, 104, 98, 109, 112},
    {"sign_w_saltgate_rise_quay", "Saltgate Rise", "the chokepoint road", SignKind::Way, 107.500F, 67.500F, 19, 104, 58, 111, 77},
    {"sign_w_saltgate_rise_terrace", "Saltgate Rise", "climbing to the Inner Wall gate", SignKind::Way, 107.500F, 137.500F, 20, 104, 129, 111, 147},
    {"sign_w_saltgate_rise_upper", "Saltgate Rise", "climbing to the Inner Wall gate", SignKind::Way, 106.500F, 120.500F, 19, 104, 113, 109, 127},
    {"sign_w_tarwalk_bend1", "Tarwalk", "crowded by day, fog-blind by night", SignKind::Way, 168.500F, 64.500F, 19, 162, 62, 178, 67},
    {"sign_w_tarwalk_bend2", "Tarwalk", "crowded by day, fog-blind by night", SignKind::Way, 188.500F, 64.500F, 19, 179, 62, 195, 67},
    {"sign_w_tarwalk_e1", "Tarwalk", "quays one side, warehouses the other", SignKind::Way, 116.500F, 60.500F, 19, 112, 60, 128, 65},
    {"sign_w_tarwalk_e2", "Tarwalk", "the working spine", SignKind::Way, 140.500F, 60.500F, 19, 129, 60, 145, 65},
    {"sign_w_tarwalk_gull", "Tarwalk", "crowded by day, fog-blind by night", SignKind::Way, 152.500F, 60.500F, 19, 146, 58, 161, 65},
    {"sign_w_tarwalk_quay", "Tarwalk", "quays one side, warehouses the other", SignKind::Way, 96.500F, 62.500F, 19, 88, 60, 103, 65},
    {"sign_w_tarwalk_w1", "Tarwalk", "the working spine", SignKind::Way, 46.500F, 62.500F, 19, 32, 60, 59, 65},
    {"sign_w_tarwalk_w2", "Tarwalk", "the working spine", SignKind::Way, 74.500F, 62.500F, 19, 60, 60, 87, 65},
    {"sign_w_tarwalk_worn", "Tarwalk", "crowded by day, fog-blind by night", SignKind::Way, 210.500F, 61.500F, 19, 196, 58, 223, 65},
    {"sign_w_wormwood_pier", "Wormwood Pier", "condemned and rotten", SignKind::Way, 155.500F, 56.500F, 19, 154, 36, 156, 57},
};

inline constexpr std::size_t kSignCount = sizeof(kSigns) / sizeof(kSigns[0]);

}  // namespace granadad::sim::docks
