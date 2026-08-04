# PLAY.md — Granadad: The Darkstreets

How to start it, what to try, and what is and is not fun yet.
Written for the S10 demo build, `0.10.0`.

---

## 1. Start it

```
.\dist\granadad.exe
```

That is the whole thing. No arguments, no launcher, no config file. It opens a
window on the Docks of Granadad at eight in the morning, with your casebook up
and one lead in it.

If `dist\granadad.exe` is not there or you want to be sure it is current:

```
docker compose run --rm --build build
powershell -ExecutionPolicy Bypass -File .\scripts\verify-windows.ps1
```

The first compiles and tests everything and puts a native Windows binary in
`dist\`. The second finishes the determinism gate on this machine — it is the
half docker cannot run, because executing a Windows binary under wine would
prove something about wine.

**Sanity checks that need no window:**

```
.\dist\granadad.exe --selftest     # nine integer primitives, no display needed
.\dist\granadad.exe --version      # which binary you are holding
.\dist\granadad.exe --help         # every flag, and there are a lot of them
```

---

## 2. The first ten minutes

You are on the **Tarwalk**, the working quay, six tiles from the door of the
**Gilded Gull**. It is dawn. The district is at work and it will keep working
whether you watch it or not.

The notes are open on your first frame. They say:

> A BODY CAME UP AGAINST THE OUTFALL GRATE AT LOW TIDE.
> THE MISSION TOOK IT IN FOR RITES.
>
> `? THE BODY`

**Walk, and the notes put themselves away.** They never open again on their own.

Three keys are the whole game:

| | |
|---|---|
| **`Q`** | **look at what is here.** The investigation verb. |
| **`J`** | **your casebook.** Everything you have been told, and where to go. |
| **`F1`** | **the keys.** All of them, in the game, paged nine at a time. |

Everything else is on `F1`. You do not need this file.

**Where to go first:** the corner of the screen says. The bottom-left row reads
`CASE 0/1 > MISSION OF THE FLAME` and it always names the next place the trail
wants you. The Mission is south and east of the spawn, up off the quay.

Stand in it and press **`Q`**.

---

## 3. Ten things to try, roughly in order

1. **Follow the trail.** Press `Q` at the Mission, read what it opens, press
   `J`, go where it points. The trail is twelve leads long and it closes in ten
   without leaving the quayside plane. It takes about fifteen minutes of
   walking if you know the district and considerably longer if you do not.

2. **Walk to the Outfall on the seawall and press `Q`.** The grate is corroded
   shut *from outside*. That is a **dead end** — it opens nothing — and it is
   the most important thing in the case: the sea is what the whole ward
   believes, and the sea is wrong. Your casebook strikes it through with an `X`
   and keeps it, because a thing you have ruled out is worth having ruled out.

3. **Try pressing `Q` somewhere nobody has sent you.** You get "NOTHING HERE
   WORTH WRITING DOWN", standing directly on a clue. That is the design law of
   this investigation, taken from the gazetteer: *the gate is knowing where to
   ask*. You cannot fail a lead. You can fail to hear about it.

4. **Go into the Gilded Gull and talk to people.** `E`. Sixteen people keep real
   hours: Master Venn on the stair, Gerta behind the bar, two bouncers on an
   overlapping rota, Father Maell for an hour in the evening, Captain Wake from
   seven, Watchman Cull off duty from nine, Finch in the snug after ten. Every
   word any of them says was written by you, in `content/raws/barks/barks.json`.

5. **Buy a drink, then haggle for a bed.** `E`, pick the topic. How far the
   landlord comes down is your STREETWISE against his, and haggling is how
   STREETWISE goes up.

6. **Put a hand in somebody's purse and get caught.** `T`. The whole room sees
   it, the docker will not speak civilly to you again, and the ward hears about
   it — top-right corner.

7. **Come back at two in the morning and rob the place properly.** `C` to
   crouch, and watch the top-right row change from `SEEN` to `HIDDEN`. Up the
   stair with `SPACE`, stand at a bed-foot that is not yours and press `G`. The
   wire goes into a strongbox and **the keyboard belongs to the lock**: `W`/`S`
   aim, `SPACE` probes, `F` puts a shoulder to it.
   Your first box will probably cost you every pick you own — see §5.

8. **Get on the roofs.** `SPACE` mantles onto a wall you are facing; over the
   Gull's north wall is the guest floor, then the lead. `X` steps off and takes
   the fall. Two thirds of the district is only reachable up there.

9. **Sign on with the Skyrunners.** Finch, in the snug, after ten. Then look at
   what `J` says the ward calls you. It changes.

10. **Lose a fist fight.** `F` at a bouncer. He puts you out of the door, and
    the man who did it goes on rising in the ward for the rest of the game —
    a rung, a trade house with real members, a permanent cut of every price in
    the district. He does it whether or not you ever come back.

---

## 4. What the corners of the screen mean

The HUD hugs all four edges and leaves the middle alone. That is a rule, not a
preference, and there is a test that fails if anything creeps inwards.

| Where | What |
|---|---|
| top centre | compass ribbon, and the place you are standing in |
| top right | the hour, your purse, what the ward thinks of you, what the Watch has heard, what is in your sack, and **whether anybody can see you** |
| bottom left | health, and **`CASE 4/9 > THE DROWNED HOLD`** — where the trail stands and where it wants you |
| bottom right | the room you are in, and the man who last put you on the floor |
| bottom centre | the lock under your wire, and whatever was just said to you |

The `CASE` row turns **red** when you have frightened the ward badly enough
that nobody walks the Gullet alone. You do that by solving the case.

---

## 5. Frames from a real session

In `docs/frames/`, all captured from the shipped binary with no window:

| | |
|---|---|
| `s10-01-first-run.png` | the opening page of a new game |
| `s10-02-controls.png` | `F1`, in the game |
| `s10-03-the-body.png` | the Mission, one lead read |
| `s10-04-the-ledger.png` | the Weighhouse, four read |
| `s10-05-casebook.png` | the notes with the case closed: nine followed, three struck through |
| `s10-06-drowned-hold.png` | where the surface trail ends |
| `s10-07-the-gull.png` | the Gilded Gull at nine at night |
| `s10-08-the-lock.png` | the wire in a guest's strongbox |
| `s10-09-the-taproom.png` | back down among the people who heard you |
| `s10-10-skyrun.png` | the Skyrunner line, nine stages, working again |

Every one is reproducible:

```
.\dist\granadad.exe --smoke=0 --hold --time=8 --trail=start --screenshot=x.png
.\dist\granadad.exe --trail          # walks the whole case and prints what it found
.\dist\granadad.exe --burgle=lock    # the burglary, ending on the lockpicking row
```

---

## 6. An honest assessment: what is fun, and what is not

### What works

**The trail is a real investigation and it holds up.** Twelve leads, three of
which strike through, four of which independently point at the same warehouse —
which is what corroboration feels like. Standing on a clue you have not been
told about and getting nothing is the best thing in the build: it makes the
district feel like it is keeping something from you rather than waiting for
you.

**The Outfall is the best beat in the game.** It costs a walk to the far east
seawall, it is the theory everybody in the ward holds, and it is wrong. The
casebook keeps it with an X through it.

**The Gilded Gull is still the best room.** Sixteen people with hours, opinions
and a memory, every line of it authored. It was the best thing in the build in
S2 and it is still the best thing in the build.

**The lockpicking is a genuine minigame now.** It was not in S9 — the numbers
made it unwinnable and every scripted run ended with a boot through the lid.
Three constants moved and one rule changed (strain is per-pin), and a hand that
has worked eighteen probes hears which way it was wrong and can bisect a lock
open in four probes a pin. Your first box still costs you the roll. That is the
arc, and it is taught rather than explained.

**Crouching in the dark actually works and you can watch it work**, in one row
of the HUD, live.

### What is thin

**There is nobody in the district.** This is the big one. The Docks have 692
actors with jobs, homes and relationships — and they live in an economic roll
that ticks in the background with no position on the map. The only bodies you
can walk up to are the Gilded Gull's sixteen. So the trail is walked through a
beautifully authored ward that is, physically, empty. The clue sites read as
places rather than as people: you find Harl's scratch-marks, you do not meet
Harl.

**Stealth only exists inside the Gull**, for exactly that reason: there is
nobody outside it to hide from. The HUD's stealth row does not draw in the
street, which is correct and also tells you what is missing.

**The interiors are brown boxes.** The renderer draws the real baked geometry
and the geometry is honest, but a shop interior is a dim rectangle. The
frontages and the quay at dawn look good; the insides do not yet.

**The two dead ends are the only surprises.** Once you have read a lead the
casebook tells you exactly where to go next, so the middle of the trail is
navigation rather than deduction. There is no wrong turn you can take that
costs you anything.

**The long game is a scoreboard more than a game.** Five tracks, twenty rungs,
and three of them buy something you can feel — extra picks in a set, the
Flame's eye reaching a tile further, a discount at a counter. The other two
give you a title and nothing else. It reads as progression; it is not yet a
reason to play a fourth hour.

### What does not exist and is not pretended to

- **The dedicated first-person combat screen.** Deferred three times now. Fists,
  shoves and bouncer ejections resolve in-world; draw a blade and the rule says
  it becomes a different kind of fight, and that fight has no screen. This is
  the largest hole in the build.
- **The starter dungeon.** The trail ends at the breach in the Drowned Hold's
  floor. There is nothing under it. `docs/design/DOCKS-GAZETTEER.md` §6 has
  three levels designed and none built.
- **Saving.** There is no save file. A session is a session.
- **Two of the twelve leads sit on the strand plane** (Brann's back-cellar and
  the Beaching Strand, where Tarry Jek sleeps). They are one band down; the
  scripted walk does not climb, so it reads ten and leaves those two open. On
  foot with `X` you should be able to reach them, and that is **not verified**.

### The honest verdict

The first fifteen minutes are good. You arrive somewhere with a name, you are
told one thing, and following it takes you across a district that was designed
by somebody who cared. The middle hour is good in the Gull and thin outside it.
There is no third hour yet, because there is nothing under the Drowned Hold and
nobody in the streets.

What it needs next, in order: **bodies in the district**, then **the combat
screen**, then **the cellars**.
