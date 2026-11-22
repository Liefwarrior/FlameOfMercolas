# Phase 9 frames -- weapons and equipment

Before is `1209691a` (main at `76a5051b` plus the three fixes at the bottom of this branch, none of which changes a frame except the ale's size on the table). After is `be04817b` -- phase 9 plus its two critic fix rounds on this branch. Same exe flags, same content, 1280x720, shot on Windows on an RTX 5090 Laptop GPU through the hidden-desktop launcher.

Every shot is

    $env:GRANADAD_CONTENT_DIR = '<tree>\content'
    <tree>\dist\granadad.exe <flags> --screenshot=<name>.png

## Both sets

| name | flags |
| --- | --- |
| street-morning | `--smoke=0 --hold --time=8` |
| street-dusk | `--smoke=0 --hold --time=19` |
| street-night | `--smoke=0 --hold --time=22` |
| taproom | `--threshold=gull --time=20` |
| piers | `--threshold=piers --time=9` |
| look-up | `--smoke=0 --hold --time=8 --pitch=45` |
| look-down | `--smoke=0 --hold --time=8 --pitch=-45` |
| fp-sword | `--smoke=0 --hold --time=9 --hands=sword` |
| fp-club | `--smoke=0 --hold --time=9 --hands=club` |
| figures-looks | `--smoke=0 --hold --time=9 --sheet=looks` |
| pack-equip | `--kit=pack` |

## After only

The base can't wear a loadout, so these have no before. The closest base shots are fp-sword (what a held knife drew), fp-club and figures-looks, same street, same spot.

| name | flags |
| --- | --- |
| loadout-light | `--gear=light --sheet=gear` |
| loadout-guard | `--gear=guard --sheet=gear` |
| loadout-heavy | `--gear=heavy --sheet=gear` |
| firstperson-light | `--gear=light` |
| firstperson-guard | `--gear=guard` |
| firstperson-heavy | `--gear=heavy` |
| taproom-figure | `--gear=gull --sheet=gear` |
| pack-compare | `--gear=compare` |

Light is wool cap, gambeson and cutlass. Guard is iron pot, brigandine, gauntlets, mace, round shield and boots. Heavy is iron pot, brigandine, gauntlets and the maul, two hands. All of it gets picked up off the quay and put on through the pack, same as a player would. `--sheet=gear` stands your own figure about three and a half tiles out, turned three-quarters so the shield and the weapon both show. Gull is the guard set inside the taproom at 20:00.

The light, guard and heavy shots all end on the spawn tile at 09:00, so they line up with each other and with fp-sword, fp-club and figures-looks from before.

One heads-up on first person. With a shield or a two-hander the hands are the plain placeholder arms, not the licensed ones. The licensed arms only have one way to hold the left hand, and it can't grip a shield or a haft. One-handers still get the licensed arms.
