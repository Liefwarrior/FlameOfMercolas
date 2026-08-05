# #80 — somebody lives on the roofs, and something eats the rats

Two gaps the population round flagged in its own source and did not close. Both
were stated honestly in-code rather than papered over, and both are closed here.

Every frame in this folder was **looked at** before it was committed, and every
one was taken with the shipped `dist\granadad.exe` on this Windows host.

---

## A — the roof slum was unpopulated

**Was.** `ward_roster.cpp` said it out loud, in the middle of the C4 Gullet
block:

> a ward actor has no climb verb, so a body homed up there could never walk to
> its own bed. The roof huts are therefore UNPOPULATED, said plainly, and the
> Gullet's thieves keep two of its ground-level condos instead.

Read against the canon that is bigger than it sounds:

* **DOCKS-GAZETTEER §2.5** puts Trojian housing on a wealth gradient that runs
  from courtyard compounds up to **rooftop slums** — tents and mud huts on a
  walled deck, let by the house-owner beneath. That is where the poorest of a
  ward live, and there were none of them.
* The same section rules rooftops socially unseemly, which is *why* the poor are
  up there and *why* burglars use the deck as a highway. The ward's criminal
  faction is called the **Skyrunners**. **K35 The Skyrunner's Roost** is an
  authored site on the Gullet's roof-slum deck. Their whole territory was empty.
* **§2.6** counts 8,132 standable cells the walking rules cannot reach and files
  their isolation as design *"until the law/economy layers learn to climb"*.

**Now.** `PathFinder::find` takes a `Gait`. `Gait::Climb` offers a **mantle**
(cost 70 — seven ordinary steps) and a **drop** (30 + 20 a level, three levels)
for a neighbour the *walking* rule already refused, orthogonals only. It reuses
`TileQuery::mantleBand` / `landingBand`, which the player has climbed on since
S5, rather than inventing a second opinion about what a wall is.

`wardTypeClimbs` is the ward's poor and its beasts — wastrel, urchin, thief,
cat, stray. **Not the Watch**: §2.5's social rule is exactly why §2.6 keeps the
roof-slums outside the law, and a watchman in a coat of plates does not go up
the Gullet's wall after a cutpurse. That is not a gap; it is why the Gullet is
the Gullet.

**The stranding guard is the load-bearing part.** "Reachable by climbing" is a
statement about getting **up**. A bed you can climb to and not climb from is a
tenant standing on a deck until the day it starves, and nothing about a frame or
a census would say so. So every roof bed is planned **both ways** with the real
router in the real gait before anybody is put in it, and a bed that fails is
refused and counted (`roofHomesRefused`).

Three ways a roof tenant could still have been stranded, all closed:

| how | closed by |
|---|---|
| the household draw puts a **serf** in a roof hut | the mix is forced to a climbing trade on a deck |
| an establishment **hires** a roof wastrel — claiming rewrites the type | the hiring loop skips anybody bedded on a deck (also §2.8: they are not his tenants, they are his tenants' tenants) |
| a **settle** places a body on a standable cell nobody can reach | the settle now requires a reachable cell, and searches twice as far |

---

## B — beasts did not hunt

**Was.** Stated in `actPursue`:

> the Java build's predator/prey lock — the sense probe, the chase budget, the
> futile-chase backoff and the prey revive — is NOT ported. Cats and gulls
> wander and feed; they do not hunt the mice.

**Now.** `BeastHuntPolicy` ported whole: a throttled sense probe over the mice's
**contiguous id range** (thirty-two ids, not six hundred and seventy-eight — the
mice are the last thing the roster spawns and that is what keeps the hunt off
the O(n²) path), a lock that is never abandoned mid-chase, a 100-tick chase
budget, a 500-tick futile backoff, and a caught mouse off the board for an
eighth of a day. Prey inside six tiles has its safety driven to nothing and
runs on its own `FLEE`.

**And the reason it would otherwise have been dead code.** The den nibble pays
+1,500 hunger on every wander arrival, and on *this* engine's clock a leg is
three tiles at a tile a second — so a cat was gaining hundreds of points a tick
against a decay of a quarter of one. **The cats were never hungry.** A hunt
shipped beside that would have passed every structural test and moved no mouse.
A predator's scrap now tops it up to `kScavengeCeiling` (2,500) and no further,
which is *under* the hunger band: a predator with nothing to catch hovers
permanently hungry and permanently looking and never starves, and only a catch
actually feeds one.

**The gull.** The Java build's `feral` row is captioned *Harbor Gull*; this build
wears it on the **Stray**, because the owner's own art for the `feral` query is a
feral **dog** (`actor_feral_dog_0`, tagged `actor/beast/vermin`) and the art is
canon where a comment is not — the note on `WardType::Stray` recorded that
correction in #78. There is no bird in `content/art/sprites` to hang a
seventeenth type on, so the gull's *behaviour* (feral.json's long leash, and the
hunt) lands on the body the district actually draws. **I did not add a gull.**

---

## The numbers

<!-- FILLED IN FROM THE GREEN BUILD -->

## The frames

<!-- FILLED IN FROM THE GREEN BUILD -->
