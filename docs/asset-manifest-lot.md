# LOT asset manifest -- what tools/lot-pipeline stages under content/art/lot/

Generated sections. Each tool under `tools/lot-pipeline/` rewrites its own block between
`lot-pipeline:begin` / `lot-pipeline:end` markers when it runs; do not hand-edit inside them.
`content/art/lot/` is gitignored (`.gitignore:48`): every file listed here is a derivative of a
purchased asset-store pack unless its licence column says otherwise, and none of them are in git.
`content/art/lot/staged.json` is the machine twin of this file (bytes + sha256 per staged file).

Columns: pack | source (relative to the LOT `Assets/`) | staged (relative to `content/art/lot/`) |
bytes | sha256[:12] | purpose | licence.

<!-- lot-pipeline:begin select-icons -->
## Icons -- Artsystack white masks (select-icons.py)

Source `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64` (LOT). 92 of 400 names mapped onto Granadad vocabulary by `tools/lot-pipeline/icons-manifest.json`; staged as one `64/<id>.png` copy per id plus the packed `icons-white.png` sheet. All are 1-colour white alpha masks (asserted within 8/255) meant to be tinted by the row's ink at draw time -- an icon column beside register text, never an icon grid (ASSET-PIPELINE-SPEC 3.3). Licence: asset-store-eula -- inside a build yes, raw in the public repo no.

| pack | source | staged | bytes | sha256 | purpose | licence |
|---|---|---|---|---|---|---|
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/caution_64.png` | `icons/64/alert.png` | 963 | `3ee1de397f66` | the alert row | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/store_1_64.png` | `icons/64/barter.png` | 857 | `ac6ce441f84e` | compound counters / barter | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/broken_heart_64.png` | `icons/64/bloodied.png` | 1337 | `4b93a23cca97` | the BLOODIED state | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/bridge_1_64.png` | `icons/64/bridge.png` | 1154 | `19b9957e5201` | a bridge crossing | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/friend_64.png` | `icons/64/companion.png` | 1004 | `9d916749df8c` | companions | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/Mushroom_64.png` | `icons/64/contraband_dust.png` | 1765 | `f31c235e6c09` | DUST (twists) | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/leaf_1_64.png` | `icons/64/contraband_flower.png` | 952 | `c8f292a8c054` | FLOWER | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/gemstone_64.png` | `icons/64/contraband_pieces.png` | 1657 | `a2ab54667713` | PIECES (Artifact) | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/vial_64.png` | `icons/64/contraband_quayfire.png` | 1561 | `b077a2260a75` | QUAYFIRE (moonshine jar) on the sack row and contract board | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/skull_64.png` | `icons/64/contraband_scalps.png` | 1273 | `cf6758e37fd9` | SCALPS | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/wind_spell_64.png` | `icons/64/craft_body.png` | 1514 | `f7bedbfad1fe` | Steady the Hand / Set the Shoulders / Clear the Head / Sap the Step (body-tuning axis) | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/snowflake_64.png` | `icons/64/craft_chill.png` | 2390 | `450397d5dd7c` | Draw the Chill (TEMPERATURE axis) | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/health_kit_64.png` | `icons/64/craft_close.png` | 1600 | `5fe37656f328` | Close the Cut (HEAL axis) | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/increse_health_64.png` | `icons/64/craft_heal_beat.png` | 595 | `7709fc1b9ddb` | increse_health beats on the HP line | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/potion_1_64.png` | `icons/64/craft_knit.png` | 1999 | `4bdf6a2793d4` | Knit the Skin (HEAL axis) | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/energy_64.png` | `icons/64/craft_sting.png` | 783 | `c5fe25acacb8` | Sting | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/tinder_1_64.png` | `icons/64/craft_tinder.png` | 1125 | `9b9af0e8b06e` | the Flame's own tinder / cast-ready marker | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/flame_64.png` | `icons/64/craft_warm.png` | 1408 | `0e46125f9470` | Warm the Hands / Lend the Warmth / Scald (TEMPERATURE axis, WHILE_ACTIVE) | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/crown_1_64.png` | `icons/64/crown.png` | 1374 | `e45e7d44fb7a` | rank / faction standing | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/pin_1_64.png` | `icons/64/cursor.png` | 550 | `e461c86d8960` | place cursor on the map | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/sun_64.png` | `icons/64/day.png` | 2076 | `9a657dbed5ca` | daytime | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/dice_64.png` | `icons/64/dice.png` | 1512 | `7b7a0cfbe0d7` | a chance / gamble row | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/health_drink_64.png` | `icons/64/drink.png` | 1569 | `d17fd3e247a7` | drink / a tavern round | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/mark_exclamation_64.png` | `icons/64/exclaim.png` | 895 | `c0299dd0167f` | something new on a tile | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/explore_64.png` | `icons/64/explore.png` | 1376 | `2c9bd4cff48e` | unexplored / travel verb | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/profile_64.png` | `icons/64/face.png` | 802 | `0a00a56bec0d` | a person's card / face row | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/meat_piece_1_64.png` | `icons/64/food.png` | 1695 | `1cbc6fe1b75a` | food on the sack row | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/gate_64.png` | `icons/64/gate.png` | 1442 | `e9f7e7423cb1` | a ward gate / compound gate | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/book_open_64.png` | `icons/64/grimoire.png` | 945 | `3dca9518f950` | grimoire / spellbook pane title | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/anchor_64.png` | `icons/64/harbour.png` | 1215 | `d74600c088a9` | the docks / harbour district | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/compass_64.png` | `icons/64/here.png` | 2155 | `ed0d0b6702c3` | HERE marker | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/eye_hide_64.png` | `icons/64/hidden.png` | 1417 | `b9b959202f8e` | out of sight / concealed | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/home_1_64.png` | `icons/64/house.png` | 1001 | `f01ed708b620` | a house / lodging row | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/Heart_64.png` | `icons/64/hp.png` | 1088 | `11e4338f1e61` | HP bar label | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/mark_info_64.png` | `icons/64/info.png` | 834 | `a0dd84b6fd94` | a plain detail row | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/book_64.png` | `icons/64/journal.png` | 573 | `636b3f64b0b4` | Journal / casebook tile | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/castle_64.png` | `icons/64/keep.png` | 1293 | `554867d89988` | the keep / watch-house | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/key_64.png` | `icons/64/keys.png` | 1158 | `4d52470699e3` | keys page | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/lantern_64.png` | `icons/64/lantern.png` | 1203 | `22d6e3ded191` | lamp / light row | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/document_64.png` | `icons/64/lead_cold.png` | 577 | `a7c418902d29` | casebook lead, cold (drawn dim) | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/check_64.png` | `icons/64/lead_followed.png` | 991 | `2e912517498e` | casebook lead, followed | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/paper_64.png` | `icons/64/lead_open.png` | 739 | `dc029ea35559` | casebook lead, open | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/mail open_64.png` | `icons/64/letter_read.png` | 724 | `1f5055961c91` | Letters tile, read | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/mail closed_64.png` | `icons/64/letter_unread.png` | 761 | `b10157fde474` | Letters tile, unread | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/list_1_64.png` | `icons/64/list.png` | 742 | `41c18ee21e70` | a list tile | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/lock_1_64.png` | `icons/64/lockpick.png` | 806 | `38605c64fc70` | lockpick / locked | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/eye_show_64.png` | `icons/64/look.png` | 1095 | `7d1f4de785a3` | Look reach / legend lookRangeBonus | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/map_64.png` | `icons/64/map.png` | 697 | `59ac627d2ea6` | Map tile | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/medal_64.png` | `icons/64/medal.png` | 1293 | `02a035132295` | a reputation or merit line | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/wolf_64.png` | `icons/64/nemesis.png` | 1668 | `313b464a654d` | nemesis | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/moon_64.png` | `icons/64/night.png` | 1182 | `debea3054945` | night | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/write_1_64.png` | `icons/64/note_write.png` | 894 | `de5ec617d0ac` | a note the player writes / signs | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/bag_1_64.png` | `icons/64/pack_bag.png` | 1513 | `ece357c17842` | carried pack / inventory tile | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/user_1_64.png` | `icons/64/people.png` | 845 | `e42054e3a8b3` | People tab | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/diamond_64.png` | `icons/64/piece_cut.png` | 1405 | `c80a77c943bf` | a cut piece / valuable | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/crystal_1_64.png` | `icons/64/piece_raw.png` | 2138 | `b05fe6466f7a` | a raw piece / lightstone shard | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/map_location_64.png` | `icons/64/place.png` | 1332 | `f625762a600e` | a named place row | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/wallet_64.png` | `icons/64/purse.png` | 1039 | `ab497b0a5f16` | coin purse on a body / a mark's take | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/mark_question_64.png` | `icons/64/question.png` | 1052 | `62f43c9ce7a6` | an unknown / unasked | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/quit_64.png` | `icons/64/quit.png` | 1429 | `d81be3688ea4` | quit row | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/rope_64.png` | `icons/64/rope.png` | 1844 | `47523a6e0a51` | rope / rigging item | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/coin_64.png` | `icons/64/royals.png` | 2000 | `bc7cf590fdf7` | Royals readout, right-aligned in the master/detail header; every '(COST)' commit verb | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/newspaper_64.png` | `icons/64/rumor.png` | 1601 | `dada34773f7c` | rumors heard ward-wide | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/money_bag_64.png` | `icons/64/sack.png` | 879 | `fbd3f40b9ab9` | the stash / sack row | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/scroll_64.png` | `icons/64/scroll.png` | 1386 | `4ad9f3e127e5` | a posted notice / contract sheet | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/search_64.png` | `icons/64/search.png` | 1189 | `384f4998a1b2` | search / investigate verb | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/setting_1_64.png` | `icons/64/settings.png` | 1498 | `75d760c7596b` | options tile | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/armor_1_64.png` | `icons/64/slot_armor.png` | 1389 | `7ef8514735a3` | 'a - Armor: ... DR' sheet line | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/boots_64.png` | `icons/64/slot_feet.png` | 1552 | `19c7125a6092` | feet slot if the sheet grows one | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/shield_64.png` | `icons/64/slot_guard.png` | 986 | `86690bca7085` | SHIELDWALL guard / block skill row | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/Gloves_64.png` | `icons/64/slot_hands.png` | 1323 | `d9039c8ac603` | hands slot if the sheet grows one | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/cap_64.png` | `icons/64/slot_head.png` | 974 | `ccff8a8f1abf` | head slot if the sheet grows one | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/Ring_64.png` | `icons/64/slot_trinket.png` | 2106 | `28f214bb2ab6` | 't - Trinket' line ('no trinket' stays worded, icon dimmed) | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/chest_1_64.png` | `icons/64/stash_chest.png` | 2386 | `e0bab46e7f06` | a stash chest or lockbox row | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/foot_print_64.png` | `icons/64/stealth.png` | 1645 | `31f710b4956d` | stealth | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/talk_1_64.png` | `icons/64/talk.png` | 1145 | `49cd9f69a1f1` | conversation verb | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/target_64.png` | `icons/64/target.png` | 1195 | `fc4a039163d8` | the current mark / target | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/think_64.png` | `icons/64/think.png` | 699 | `45a961fe2156` | a hunch / the player's own note | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/danger_64.png` | `icons/64/threat.png` | 1282 | `f45b2772b51f` | a hostile / escalated row | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/clock_64.png` | `icons/64/time.png` | 1714 | `45026bc94917` | clock readout | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/stopwatch_64.png` | `icons/64/timer.png` | 1144 | `c95ef8aa0c7f` | a timed contract / deadline | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/tools_64.png` | `icons/64/tools.png` | 1718 | `713356c10939` | tools item / repair verb | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/sand_glass_1_64.png` | `icons/64/travel_cost.png` | 1461 | `7d46d4104062` | travel cost '(1 MIN)' | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/lock_2_64.png` | `icons/64/unlocked.png` | 890 | `a2bf47b254ff` | unlocked | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/siren_64.png` | `icons/64/watch.png` | 939 | `2c1ef0766cd6` | Watch notice / alarm | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/wand 2_64.png` | `icons/64/weapon_blunt.png` | 970 | `30abf2ca4e45` | Weapon::Blunt -- the plain CUDGEL (knobbed club silhouette) | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/sword_1_64.png` | `icons/64/weapon_cutlass.png` | 1311 | `d9cbfd1c496d` | an Edged sub-kind if the sheet ever names the cutlass | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/dagger_64.png` | `icons/64/weapon_edged.png` | 1159 | `49487b94cb36` | Weapon::Edged -- knife / boat-hook / cutlass class | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/mine_64.png` | `icons/64/weapon_evictor.png` | 2145 | `3136a670788e` | Weapon::Evictor -- 'THE EVICTOR n-n IMPACT' (spiked head) | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/fist_64.png` | `icons/64/weapon_fists.png` | 1127 | `add63b6097b7` | Weapon::Fists -- 'FISTS 1-3 IMPACT' sheet row | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/axe_64.png` | `icons/64/weapon_hatchet.png` | 1326 | `28dc13b9a540` | an Edged sub-kind for the hatchet / boat-hook | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/bottle_64.png` | `icons/64/weapon_improvised.png` | 747 | `9a4e763d2462` | Weapon::Improvised -- 'a bottle still corked' | asset-store-eula |
| Artsystack - Fantasy RPG GUI | `tools/lot-pipeline/icons-manifest.json` | `icons/icons-white.json` | 2244 | `7a37aabab6c0` | cell index for icons-white.png (id -> cell) | generated |
| Artsystack - Fantasy RPG GUI | `Artsystack - Fantasy RPG GUI/ResourcesData/Sprites/flaticon/white/64/* (the mapped names)` | `icons/icons-white.png` | 108086 | `76fd5cde0641` | packed sheet of all 92 cells (16 cols x 64 px); keyed by icons-white.json | asset-store-eula |

94 files, 227,092 bytes.
<!-- lot-pipeline:end select-icons -->

<!-- lot-pipeline:begin pack-vfx -->
## Hit VFX -- Piloto Studio cells (pack-vfx.py)

16 cells cut from five Piloto textures by `tools/lot-pipeline/vfx-manifest.json`: one steel glint (block), one impact fleck (landed), one splash (kill), a 4-frame dust puff (body down), the 8-frame Flame cast, and the optional hard-swing crescent. Cut to 32 px for `WorldRenderer::drawSprite`'s textured path (hard cutout at alpha >= 128); every cell keeps >= 4% of its texels after the cutout (the `coverage` column in staged.json). No tint is baked: `mask`/`luma` cells are white and take `SpriteInstance.colour`; `rgba` cells (puff, flame) keep their own colour. Hues, step counts and sizes are the spec's (ASSET-PIPELINE-SPEC 3.4) and are not wired. Licence: asset-store-eula.

| pack | source | staged | bytes | sha256 | purpose | licence |
|---|---|---|---|---|---|---|
| Piloto Studio | `Piloto Studio/Textures/FireFlip_4x3_Fireball.png` | `vfx/cells/flame_0.png` | 522 | `34515ae8c97c` | the Flame cast, frame 0 of 8: one tile ahead on the look-ray, colour white (orange/yellow baked), 2 steps per frame, halfWidth 0.25 capped at 48 px on screen | asset-store-eula |
| Piloto Studio | `Piloto Studio/Textures/FireFlip_4x3_Fireball.png` | `vfx/cells/flame_1.png` | 739 | `61fa5eebb946` | the Flame cast, frame 1 of 8 | asset-store-eula |
| Piloto Studio | `Piloto Studio/Textures/FireFlip_4x3_Fireball.png` | `vfx/cells/flame_2.png` | 885 | `7205107b292b` | the Flame cast, frame 2 of 8 | asset-store-eula |
| Piloto Studio | `Piloto Studio/Textures/FireFlip_4x3_Fireball.png` | `vfx/cells/flame_3.png` | 1076 | `572496cd5c2d` | the Flame cast, frame 3 of 8 | asset-store-eula |
| Piloto Studio | `Piloto Studio/Textures/FireFlip_4x3_Fireball.png` | `vfx/cells/flame_4.png` | 1080 | `340702a61125` | the Flame cast, frame 4 of 8 | asset-store-eula |
| Piloto Studio | `Piloto Studio/Textures/FireFlip_4x3_Fireball.png` | `vfx/cells/flame_5.png` | 1088 | `b181d56d6102` | the Flame cast, frame 5 of 8 | asset-store-eula |
| Piloto Studio | `Piloto Studio/Textures/FireFlip_4x3_Fireball.png` | `vfx/cells/flame_6.png` | 904 | `47095047b830` | the Flame cast, frame 6 of 8 | asset-store-eula |
| Piloto Studio | `Piloto Studio/Textures/FireFlip_4x3_Fireball.png` | `vfx/cells/flame_7.png` | 720 | `0ec236521f30` | the Flame cast, frame 7 of 8 | asset-store-eula |
| Piloto Studio | `Piloto Studio/Textures/Hit_Punch.png` | `vfx/cells/fleck_landed.png` | 665 | `dbfc99735b5e` | warm impact fleck for a landed punch: struck body at chest height, tinted bone (0.92,0.86,0.70) x punchLandedPulse, 8 steps | asset-store-eula |
| Piloto Studio | `Piloto Studio/Textures/Glints_Pickbook_4x4.png` | `vfx/cells/glint_block.png` | 962 | `3b783c59b1f3` | steel glint when a guard catches the blow: chest of the striker on the look-ray, tinted the block wash's (0.52,0.60,0.70) x blockPulse, 8 steps, halfWidth 0.12 tile. (Spec named cell 5; that one is a soft dot that cuts to nothing at >= 128 -- cell 0 is the 4-point star.) | asset-store-eula |
| Piloto Studio | `Piloto Studio/Textures/ToonSmokePuff_3x3_Contrasted.png` | `vfx/cells/puff_0.png` | 681 | `aa4e7f00a6ac` | dust puff frame 0 of 4 when a body goes down: at its feet, z 0.05, grey x ground light, 3 steps per frame, halfWidth 0.20 | asset-store-eula |
| Piloto Studio | `Piloto Studio/Textures/ToonSmokePuff_3x3_Contrasted.png` | `vfx/cells/puff_1.png` | 1031 | `36f9b09c3e96` | dust puff frame 1 of 4 | asset-store-eula |
| Piloto Studio | `Piloto Studio/Textures/ToonSmokePuff_3x3_Contrasted.png` | `vfx/cells/puff_2.png` | 1227 | `a7e34da19333` | dust puff frame 2 of 4 | asset-store-eula |
| Piloto Studio | `Piloto Studio/Textures/ToonSmokePuff_3x3_Contrasted.png` | `vfx/cells/puff_3.png` | 1466 | `ef51ebdd406f` | dust puff frame 3 of 4 | asset-store-eula |
| Piloto Studio | `Piloto Studio/Textures/AnimeSlash_HalfRing.png` | `vfx/cells/slash_hard.png` | 607 | `ae31b47835ab` | crescent for a hard (held) swing at the struck body, tinted the hard-charge warm (0.95,0.55,0.20), 8 steps. Optional -- the first VFX to cut if it reads loud | asset-store-eula |
| Piloto Studio | `Piloto Studio/Textures/Water_splash_1.png` | `vfx/cells/splat_kill.png` | 648 | `b9bd49e0af6f` | hard-edged splash for a kill / top-band hit: at the body, tinted the taken wash's own red (0.58,0.10,0.08) x light, 12 steps, halfWidth 0.16. Source is blue-baked, so RGB is forced white here and the red comes from sprite.colour | asset-store-eula |
| Piloto Studio | `tools/lot-pipeline/vfx-manifest.json` | `vfx/vfx.json` | 2017 | `4e41bbf52334` | cell rects + animation frame lists for vfx.png | generated |
| Piloto Studio | `Piloto Studio/Textures/* (the cells' sources)` | `vfx/vfx.png` | 12725 | `67565ce704d9` | packed sheet of all 16 cells on a 32 px grid; keyed by vfx.json | asset-store-eula |

18 files, 29,043 bytes.
<!-- lot-pipeline:end pack-vfx -->
