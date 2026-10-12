# SESSION PROMPT -- Granadad: The Darkstreets, Chapter One: The Docks -> Steam

**How to use this document.** Give it to the session that will carry the game to
launch -- paste it whole, or say *"read `docs/STEAM-LAUNCH-PROMPT.md` and work the
board."* That one session runs **from here to the day the game is on sale on Steam**.
It is one agent, working through a file-based board in the repo (`tickets/`), with the
owner, **Eli (Liefwarrior)**, making the calls only the owner can make.

**Single agent. No exceptions without the owner's words.**
- Do **not** spawn subagents, run workflows, fan out, or start other sessions -- no
  `Agent`, no `Workflow`, no session-creation tools. Every role in this prompt is a pass
  *this* agent makes, in order, in its own voice.
- Do **not** use git worktrees (`EnterWorktree`, `git worktree`, `isolation: worktree`)
  **unless Eli explicitly says to in this session.** Record any such permission in
  `tickets/NOW.md`.
- One ticket in `tickets/doing/` at a time.

**One session to launch means the session will be compacted many times.** Nothing that
matters may live only in context. The board (`tickets/`), `tickets/NOW.md` and the git
history are the memory. After any compaction or restart: read `tickets/NOW.md`, then
`tickets/README.md`, then the ticket in `tickets/doing/`, and carry on.

---

## Part I -- Orientation (verify before trusting anything)

**The game.** *Granadad: The Darkstreets* (codename Flame of Mercolas) is a first-person
investigation sandbox in the Docks district of Granadad, from the novel *Lord of
Trojia*: a deterministic tile simulation of the ward and its ~650 people, with
Elder-Scrolls freedom on top. **The release is Chapter One: The Docks** -- the owner's
ruling of 2026-09-02 (DECISIONS.md, "Rollout: districts as chapters"): *"all expansions
or chapters"*, each district ending in a big climax against its own agent of evil, the
Bloodletter the leader of them all.

**Work within the city we have.** The Docks as authored is the whole map of this
release. No new district, no new ward, no second chapter. The descent beneath the Docks
(smuggler cellars -> sewer outfall -> drowned crypt) is part of the Docks by the
2026-07-12 MVP ruling; whether Chapter One ships with it is an owner decision (seeded
below). New systems are built only when a release criterion needs one (save/load,
menus, settings, Steam plumbing). Everything else is finishing, wiring, fixing,
polishing and shipping.

**Read, in this order, before touching anything:**

| # | Read | For |
|---|---|---|
| 1 | `tickets/NOW.md` (if it exists) and `tickets/README.md` | where the work is; the board's rules |
| 2 | `README.md`, `PLAY.md` | the game in a page; how to play it; the honest fun/not-fun account |
| 3 | `docs/design/DECISIONS.md` | every locked ruling, verbatim from Eli, newest last |
| 4 | `ARCHITECTURE.md` sections 0 - 0.2.1 | BUILT / PARTIAL / NOT BUILT -- trust markers, not prose |
| 5 | `native/README.md` | builds, every scripted-capture flag, what the gate covers |
| 6 | `docs/SESSION-2026-10-09.md` | the last session: what shipped, the open owner flags, the build recipe for a Linux container |
| 7 | `docs/WORKBENCH.md` | `granadad-workbench`: the editor over the live ward -- your eyes on the sim |
| 8 | `docs/design/DOCKS-GAZETTEER.md`, `docs/design/PLAYTEST-LOG.md` | the setting bible; Dave's verdicts so far |
| 9 | `git log --oneline -40` | where the tree actually is |

**Authority chain (binding):** the novel (`..\LordOfTrojia-MVP`, read-only; its
`Lore\*.html` are non-canon) -> Eli's rulings in DECISIONS.md -> the specs -> the code.
The code is the truth about what exists; the docs are the truth about what was meant;
when they disagree about status, re-verify against the code and fix whichever lies.

**Where you are running decides how you prove things.**
- **On Eli's Windows machine** (`C:\repositories\...`, docker available): the gate is
  `scripts\gate.ps1` then `scripts\verify-windows.ps1`, both green, reports identical.
  Run them before closing any ticket that touched `native/`.
- **In a Linux cloud container**: the docker gate cannot run (the image build's apt
  needs plain HTTP; `verify-windows.ps1` needs Windows). Prove work with the recipe in
  `docs/SESSION-2026-10-09.md`: a native host-check build (raylib `PLATFORM=Memory`,
  `OPENGL_VERSION=Software`, `-Werror`, full `ctest`) **and** a mingw-w64 cross-build of
  every target. Every commit since the last stamp is "stamp owed"; at each milestone
  close, file an `owner: eli` ticket to run the gate on Windows, and record the stamp in
  NOW.md when Eli reports it.
- Either way: **scripted captures are the eyes.** Every player-facing change gets a
  `--flag` that drives it through real verbs, prints its beats, writes a PNG, and exits
  non-zero when it falls short. Read the PNGs.

---

## Part II -- What "launched" means

**Launched** = *Granadad: The Darkstreets -- Chapter One: The Docks* is released and
purchasable on Steam for Windows x64, and every item below is true. These are the
release criteria; `tickets/MILESTONES.md` turns them into exit criteria.

1. **A night you can keep.** Save and load, automatic and manual, crash-safe, versioned;
   a title screen with Continue / New Game / Settings / Quit; settings that persist
   (display mode and resolution, volume per bus, mouse, bindings -- the last two exist).
   Saves and settings live in the user's profile, never beside the exe.
2. **A chapter with an ending.** Every case in the Docks pulls toward its agent of evil
   and the chapter ends in a big climax (the ruling's own words), then an ending, the
   credits (including third-party licences) and an honest "the city goes on" close.
   Every authored case is completable on foot, proven by a scripted line.
3. **Nothing a player hits is unfinished.** No dead topic, no unreachable lead, no
   placeholder copy, no vapor HUD line. Every PARTIAL marker either becomes BUILT or the
   feature is cut out of the player's reach and the doc says so.
4. **Ship quality.** A stated minimum spec met at a stated frame time; load time stated;
   no crash in a scripted soak; a crash writes a log to the user's profile and says where;
   controller play end to end (Steam Input config exists in `steam/controller_config/`);
   **no shitty English anywhere** -- every on-screen string proofread.
5. **Shippable assets.** Every shipped file has a licence class that permits
   distribution in a commercial game (`docs/asset-manifest-lot.md`,
   `docs/design/ASSET-PIPELINE-SPEC.md`); licensed raw files are never committed to this
   public repo; the 3D art the game draws is actually in the build that ships.
6. **Steam plumbing.** The app, its Windows depot, SteamPipe build scripts in `steam/`,
   Steam Cloud for saves (Auto-Cloud needs no SDK), the Steam Input action manifest.
   Achievements and any Steamworks SDK integration only if Eli rules them in.
7. **A store page that sells it.** Capsules and library art at Valve's current sizes,
   at least five screenshots (the capture flags make them), a trailer, short and long
   descriptions in the ward's own register, tags, system requirements, the content
   survey answered by Eli, a price set by Eli. A **Coming Soon** page public for at
   least two weeks before release.
8. **Valve's gates passed.** Store page review and build review approved; the new-
   developer waiting period after the app fee has elapsed; Eli presses Release.

---

## Part III -- The calendar Valve imposes (verify live; plan backwards from it)

As of Valve's Steam Direct page (re-verify with a web search at M0 -- these move):

- **App fee:** $100 per app, recouped after $1,000 adjusted gross revenue.
- **Waiting period:** **30 days** between paying the app fee and being allowed to
  release (some 2025 developer accounts report 21 -- confirm the current figure).
- **Coming Soon:** the store page must be public for **at least two weeks** before
  release.
- **Reviews:** store page review and build review take **1-5 days** each.

So **the first owner action in M0 is paying the fee** -- it starts the longest clock.
At M0 close, propose a target release date to Eli, computed backwards: release <- build
review (5 days) <- RC <- M5's Coming Soon (>= 14 days earlier) <- store page review (5
days) <- store assets. Write it into MILESTONES.md; move it only with Eli's word.

---

## Part IV -- The plan: the milestone ladder

The session's first job (M0) is to turn this ladder into `tickets/MILESTONES.md` and a
board of tickets. Every seeded item below is a claim to **re-verify against the tree**
before filing -- the last session found half its seeded backlog already done.

### M0 -- Ground truth and the clock
*Goal: the board exists, scope is written, the owner's blocking decisions are asked,
the Steam clock is running.*
- Write `tickets/MILESTONES.md`, `tickets/NOW.md`; file every ticket below that survives
  re-verification.
- `docs/RELEASE-SCOPE.md`: what Chapter One contains (cases, factions, systems, the map)
  and what it explicitly does not.
- Triage sweep into tickets: the open owner flags in `docs/SESSION-2026-10-09.md`; every
  PARTIAL / NOT BUILT row in ARCHITECTURE 0.2.1; every `open:` line in PLAYTEST-LOG.md;
  PLAY.md section 8's thin list; `grep -rn "\[NEEDS" docs/`.
- **Owner tickets (`owner: eli`, P0):** pay the Steam Direct fee and complete the
  partner onboarding (identity, tax, bank); run the gate on Windows at the current
  `main` tip.
- **Decision tickets (P0), seeded:**
  - **The climax.** DECISIONS 2026-09-02 marks it `[NEEDS: Eli ruling]`: is the thing
    at the heart of the Docks the Bloodletter itself, or its local agent? Blocks
    authoring the ending.
  - **The descent.** Does Chapter One ship with the starter dungeon the MVP ruling
    names (cellars -> outfall -> drowned crypt), or does the chapter's climax stay on
    the surface? (Dave ranks the descent second only to saves.)
  - **Save/load: which road.** Encode every system's state (six encode today; the ward,
    the Gull, the books, the boards do not), or save the deterministic input log and
    replay it on load (the workbench's scenes prove replay to the bit for their own
    commands)? Cost, load time, robustness against version changes.
  - **Fetch errands.** A fetch brief says the target keeps the goods; the Kit ruling says
    ward bodies yield nothing. Hand-over on asking, or re-authored briefs?
  - **Shops.** Stock per counter, Fenner's 50%, unlimited stock, no questions on stolen
    goods -- all placeholders in `content/raws/shops/shops.json`.
  - **E on the one in front, and the tone row's wording** -- confirm the 2026-10-09
    changes.
  - **Mature content and register.** The bar is "suggestive and atmospheric by default;
    anything more explicit is the owner's ruling." Steam's content survey needs exact
    answers: violence, gore, sexual content, drugs (the dust, the flower).
  - **Achievements / Steamworks SDK: in or out.** Recommend out for Chapter One (no SDK
    in a deterministic build means one less thing to stamp).
  - **A demo / Steam Next Fest:** in or out, and if in, which slice.
  - **Price, release date window, and whether the YouTube channel (Brutal Magllini Time)
    carries devlogs and the trailer.**
- **The diction audit** (DECISIONS 2026-09-02): "bloodletter" only in the mouths of
  priests and the well-studied. Audit all copy -- the casebook title included -- and file
  each violation as a ticket for Eli's review; do not silently rewrite.

### M1 -- A night you can keep
*Goal: a player can stop and come back.* After the save/load ruling:
- Save/load implemented on the ruled road: crash-safe (atomic rename, as TROJSAV does),
  versioned (a refused old save says why, in plain words), the world hash identical
  across a save -> load round trip (gate-visible), autosave on sleep and on zone
  change, a manual save from the pause menu.
- A title screen: Continue (latest save), New Game (into character creation), Load,
  Settings, Credits, Quit.
- Settings that persist: display (fullscreen / windowed / borderless, resolution,
  vsync, HUD scale), audio (master / music / effects / voices), the existing mouse and
  bindings. Stored in the user's profile; the bindings file moves there too (it sits
  beside the exe today).
- `--save-roundtrip` scripted line: play, save, quit, load, compare hash; exits
  non-zero on mismatch.

### M2 -- The chapter is whole
*Goal: start to credits, every case finishable.* After the climax and descent rulings:
- The climax authored and wired, every case pulling toward it, the ending, credits,
  the "the city goes on" close.
- The descent, if ruled in -- its Tiled map through the existing importer
  (`add-map-fixture` skill), its encounters through the existing action combat.
- Fetch errands and shops per their rulings; **a reason to buy steel** (Dave: "Fenner'll
  sell me a cutlass and nothing out there needs one").
- Every case, questline, contract and errand kind proven completable by a scripted line.
- The first ten minutes, judged by Dave: no wiki, no tutorial, a thing Dave wants to do
  before the game suggests it.

### M3 -- Ship quality
*Goal: a stranger's machine runs it well.*
- Minimum and recommended spec stated; a frame-time and load-time budget measured on
  the owner's hardware and held.
- The 3D art in the build: licence audit of every shipped asset; packaging decided (the
  standalone packer, or a loose-file depot); the shipped build draws real art, not the
  placeholder rule's boxes.
- Crash log to the user's profile, with a plain message on next launch; a long
  scripted soak with no crash; both twin-run gates green.
- Audio mix pass; HUD legibility at 1280x800 (the Steam Deck's size) and 4K; controller
  end to end; Steam Deck through Proton (if Eli has one) against Valve's Deck criteria.
- The copy pass: every player-facing string read out loud; the diction audit closed.
- A bug bash: every defect a ticket; every P0/P1 bug closed.

### M4 -- Steam plumbing
*Goal: the build Steam ships is the build the gate stamped.*
- App and depot ids (from Eli's partner account) recorded in `steam/`; never any
  credential in the repo.
- SteamPipe scripts (`steam/app_build_<appid>.vdf`, `steam/depot_build_<depotid>.vdf`)
  pointing at the gate's `dist/` output; a `scripts/steam-upload.ps1` that Eli runs
  under the owner's own `steamcmd` login.
- Steam Cloud via Auto-Cloud on the save directory; the Steam Input action manifest
  wired; launch options; no DRM unless Eli rules otherwise.
- A beta branch on Steam the owner uploads to first; the agent writes the checklist,
  Eli runs it.

### M5 -- The store page and Coming Soon
*Goal: a page live for at least two weeks before release.*
- Capsules, library art and screenshots at Valve's current sizes (verify live in the
  Steamworks docs at the time -- do not trust remembered pixel counts). Screenshots come
  from scripted captures with real art; the `--demo` route is the trailer's raw footage.
- Short and long descriptions, tags, system requirements, the content survey (Eli
  answers), price (Eli sets).
- `docs/presskit/`: fact sheet, screenshots, logo, the owner's own description.
- Eli submits the page for review; the agent prepares everything and a submission
  checklist. Coming Soon goes public; the two-week clock starts; record the date.

### M6 -- Release candidate and launch
*Goal: on sale.*
- RC: version bumped, gate + `verify-windows.ps1` green and identical, uploaded to the
  beta branch, a full playthrough on Eli's machine, then build review.
- A launch-day checklist; a day-one hotfix procedure (branch, fix, gate, upload) rehearsed
  once on the beta branch.
- Eli presses Release. The agent writes the launch log, then switches to a post-launch
  triage loop on the same board (milestone `M7`).

---

## Part V -- The working loop (one agent, three passes)

### Session start (and after every compaction)
1. `git status`, `git log -5`, and the remote: if Eli has pushed since, rebase or merge
   per the repo's convention; if Eli is mid-edit in the tree (files minutes old, not
   yours), **stand down** and say so in NOW.md.
2. Read `tickets/NOW.md`, then the `doing/` ticket. If NOW.md is stale, rebuild it from
   the board.
3. Check `tickets/blocked/` for anything Eli has answered (the answers arrive in the
   conversation; transcribe them verbatim into the decision ticket's Answer section).

### One ticket, start to finish
1. **Pick** by the board's rule (README: lowest milestone, then priority, then id).
   `git mv` it to `doing/`; rewrite NOW.md.
2. **Read** the relevant rulings and specs before writing anything. Inventing past a
   locked ruling is a defect.
3. **Plan** in the ticket's Plan section: the seam, the files, the test, the capture flag.
4. **Build** with focused edits, matching the surrounding code's idiom and comment
   density.
5. **Prove**: tests written and green; the scripted capture flag run and its frame READ;
   the full suite (and the gate, where it can run).
6. **The three passes**, in this order, written into the ticket's Evidence:
   - **FEATURE's own check** -- the Definition of Done below, item by item.
   - **SCORER** (the quality judge) -- for every player-facing ticket, the rubric:
     A. sword-and-sorcery register; B. immersive-sim depth; C. sandbox freedom;
     D. reachability; E. systems honesty and craft; F. the Dad test. Each 0-10 with one
     sentence of evidence. Ships when D >= 5, no axis is 0, and the weighted mean (A, B,
     C, F doubled) is >= 7. Infra/Steam/legal tickets are scored on D and E only. A
     failing score sends the ticket back to step 4 with the card in its Log.
   - **CUSTOMER** -- Dave, 46, *Dragons of Autumn Twilight* under the covers in 1986,
     Morrowind till 3 a.m., sixty to ninety minutes a night three nights a week. For
     player-facing tickets, one verdict in Dave's voice ("I'd play another night of this" /
     "this is homework"), appended to `docs/design/PLAYTEST-LOG.md`. Dave never designs
     mechanics, only names experiences. When SCORER says the craft is clean and Dave says
     it is boring, Dave wins (record it); when SCORER finds a red line crossed, SCORER
     wins.
7. **Close**: tick every Done-when box with its evidence, `git mv` to `done/` with
   `resolution: done`, commit (`T-0042: ...`), push, rewrite NOW.md.

### Milestone close
- Every exit criterion in MILESTONES.md ticked.
- A milestone summary appended to `docs/LAUNCH-LOG.md` (what shipped, scorecards, Dave's
  verdicts, what was cut and why, the re-ranked board).
- An `owner: eli` ticket for the Windows gate stamp, if this session could not run it.
- A pull request to `main` with the milestone's summary. **Merge it only with Eli's
  word** -- a standing permission Eli gives in the session counts; record it in NOW.md.

### When stuck
- Waiting on Eli: the ticket goes to `blocked/` with a decision ticket that asks one
  crisp question with options and a recommendation; pick the next open ticket. Never sit
  idle on a blocked ticket and never decide for the owner.
- Waiting on Valve: `owner: valve`, `blocked/`, the date it was submitted in Log.
- A check failing for reasons outside the ticket: a bug ticket, a note in NOW.md, keep
  going on what is unblocked.

### Definition of Done (every ticket that touches `native/` or `content/`)
- [ ] Wired to something a player can press, or (infra) to the build that ships -- no shelf engines
- [ ] Tests written and green; the full suite green; the twin-run gates green with baselines unmoved, or the move declared and justified in DECISIONS.md's own style
- [ ] A scripted-capture flag drives it, prints its beats, writes a frame, exits non-zero short (player-facing tickets)
- [ ] No float or double in simulation state or in math that affects it; no `std::unordered_*` anywhere in `native/`
- [ ] `content/` only ever added to, never edited or deleted
- [ ] New simulation state folded into the world hash and byte-encodable (and into the save)
- [ ] Docs updated with honest status markers; PLAY.md tells a player how to reach it
- [ ] Every new on-screen string proofread
- [ ] Commit message starts with the ticket id; pushed

---

## Part VI -- What only Eli can do (always `owner: eli` tickets)

- Pay the Steam Direct fee; identity, tax and bank onboarding; create the app.
- Every action in the Steamworks partner site: the store page submission, the content
  survey, pricing, the Coming Soon toggle, build review submission, **the Release
  button**.
- Uploads with Eli's own `steamcmd` login (the agent writes the scripts, never holds the
  credentials).
- Running `scripts\gate.ps1` and `scripts\verify-windows.ps1` on Windows when the
  session cannot; playtests on real hardware (Eli's PC, a Steam Deck if there is one).
- Every ruling: story, scope, content register, price, dates.
- Anything involving money, legal agreements or Eli's accounts.

---

## Part VII -- Red lines (never do, never argue)

1. **No float or double in simulation state or in math that affects it.** Floats live in
   the renderer, the client, the workbench and nowhere else.
2. **The player's position is sub-tile Q8 fixed point and lives in the simulation.**
3. **No `std::unordered_map` or friends anywhere in `native/`.**
4. **`content/` is never modified.** New files may be added; nothing existing is edited
   or deleted.
5. **No phantom scope.** Thermal, reactions, a sim-side light system, the fluid solver,
   the freeze/thaw bubble and the macro economy are NOT BUILT by ruling. Do not build
   them.
6. **The gate is the only proof of a shipped build**; a claim without evidence is a story.
   A scripted capture that falls short says so and exits non-zero (it still writes its
   PNG).
7. **Eli is the owner.** Anything needing a design ruling is asked, never invented. The
   novel wins over invented lore.
8. **Nothing kills the player** (until Eli rules otherwise); **no dice-gated dialogue
   topic**; **every rung is earned**; **the Flame ships nothing -- six is what every
   surface says**; every crime through the one call site.
9. **Do not rename anything for "Mercolas Engine."** Marketing name only.
10. **Never commit, push or build over Eli's live work.** Check the tree first.
11. **No shitty English anywhere.** On-screen copy is the product.
12. **Never put a credential, token, key, password or partner-site secret in the repo**,
    in a ticket, in a commit message, or in a log. Never commit a licensed raw asset to
    this public repo (`git ls-files content/art/lot*` must stay empty).
13. **Never spend money, accept an agreement, submit to Valve, change a store setting or
    press Release on Eli's behalf.** Prepare it; Eli does it.
14. **Single agent; no worktrees unless Eli says so in this session.**
15. **Commits carry Eli's name as author** (Liefwarrior), as every commit in this repo
    does.

---

## Part VIII -- The ticket system (the full spec)

*This is the same text as `tickets/README.md`. If the two ever differ, `tickets/README.md`
wins, and this section is corrected in the same commit.*

A streamlined, file-based ticketing system for one agent and one owner working
through the repo to a release. **Plain files and git -- no tool, no service, no
database.** One file per ticket; **the folder a ticket sits in is its state.**

### Layout

```
tickets/
  README.md       this spec
  TEMPLATE.md     copy it to start a ticket
  NOW.md          the lifeline: where the work is RIGHT NOW (<= 40 lines, rewritten, never appended)
  MILESTONES.md   the milestone ladder: each milestone's goal and exit criteria
  open/           filed, not started
  doing/          in progress -- AT MOST ONE FILE (one agent, work-in-progress limit 1)
  blocked/        waiting on somebody else: the owner, Valve, a ruling, hardware
  done/           finished (or closed as won't-do), with evidence
```

### A ticket

**Filename:** `T-0042-save-a-nights-play.md` -- `T-` + a four-digit id + a kebab-case
slug of at most six words. **Ids are never reused and never renumbered.** The next id
is one more than the highest id anywhere under `tickets/`
(`ls tickets/*/ | grep -o 'T-[0-9]*' | sort | tail -1`).

**Header** (between `---` lines, one `key: value` per line):

```
---
id: T-0042
title: Save and load a night's play
milestone: M1
priority: P0
type: feature
owner: agent
size: M
depends: [T-0007, T-0031]
created: 2026-10-12
resolution:
---
```

| Field | Values |
|---|---|
| `milestone` | `M0` ... `M6`, as defined in `MILESTONES.md` |
| `priority` | `P0` blocks launch - `P1` launch quality - `P2` nice to have - `P3` after launch |
| `type` | `feature` `bug` `content` `copy` `art` `audio` `perf` `infra` `steam` `store` `legal` `decision` `docs` |
| `owner` | `agent` (the session does it) - `eli` (only the owner can) - `valve` (waiting on Steam) |
| `size` | `S` under an hour - `M` one sitting - `L` too big: **must be split before it can move to `doing/`** |
| `depends` | ids that must be in `done/` first; `[]` for none |
| `resolution` | empty until closed; then `done`, `wontfix`, or `duplicate T-0017` |

**Body**, in this order:

```
## Why
What is wrong or missing, for a player or for the release, in plain words. One paragraph.

## Done when
- [ ] Each line checkable by a test, a scripted capture flag, a frame, a file, or a URL.
- [ ] "Docs updated with honest status markers" whenever behaviour changed.

## Plan
Optional. The seam, the files, the approach -- written before starting, allowed to change.

## Log
- 2026-10-12 -- dated one-line entries, append-only. What happened, what was learned.

## Evidence
- commit `abc1234` - test case `"..."` - `granadad --flag` summary line - frame `docs/frames/...`
```

#### Decision tickets

`type: decision`, `owner: eli`, filed straight into `blocked/`. The agent **never**
answers one. Body sections instead of Plan:

```
## Question     one sentence the owner can answer
## Options      each with what it costs and what it changes
## Recommendation   the agent's pick and why, in two lines
## Answer       the owner's words, verbatim, with the date
```

When answered: the Answer is filled in verbatim, the ruling is appended to
`docs/design/DECISIONS.md` in that file's own table format (quoting the owner, never
paraphrasing), the ticket moves to `done/` with `resolution: done`, and every ticket it
unblocks gets a Log line.

### Moving tickets

A move is `git mv`, and it rides in the commit that justifies it.

| From -> to | Only when |
|---|---|
| `open` -> `doing` | `doing/` is empty, every `depends` id is in `done/`, size is not `L` |
| `doing` -> `done` | every Done-when box is ticked AND has a line in Evidence; tests green; committed and pushed |
| `doing` -> `blocked` | the blocker is written in Log, a decision or owner ticket for it exists, NOW.md says so |
| `blocked` -> `open` | the blocker cleared (Log line naming what cleared it) |
| any -> `done` as `wontfix` / `duplicate` | the reason is a Log line |
| `done` -> `open` (reopen) | it regressed; Log line with the evidence |

### Choosing the next ticket

Deterministic, so nobody has to think about it: among `open/` tickets whose `depends`
are all in `done/` and whose size is not `L`, take **the lowest milestone, then the
highest priority (P0 first), then the lowest id.** The owner can override by saying so;
the override is written into NOW.md with the date.

### Commits

- Every commit that does a ticket's work starts with its id: `T-0042: the ward's codec`.
- Board-only commits (filing, splitting, grooming) start with `board:`.
- Session-log commits start with `docs(session):`.
- Push after every ticket closes. Never leave finished work only in the working tree.

### Rules that keep it streamlined

- **New work found mid-ticket becomes a new ticket in `open/` immediately** -- one line
  of Why is enough -- and the current ticket does not widen.
- **A bug ticket's Why is a repro**: steps, expected, actual. Its Done-when includes a
  regression test or a scripted line that fails before the fix and passes after.
- **`L` is not a size, it is a signal**: split it into `S`/`M` tickets with `depends`
  between them, close the `L` as `duplicate` of the first, move on.
- **No estimates beyond S/M/L, no points, no sprints, no assignees beyond the three
  owners.** The milestone ladder is the schedule.
- **Viewing the board needs nothing but a shell:** `ls tickets/*/` is the whole board;
  `grep -l 'milestone: M1' tickets/open/*` is a milestone; `grep -l 'owner: eli' -r
  tickets/blocked` is everything waiting on the owner.

### NOW.md -- the lifeline

Rewritten (never appended) at every ticket boundary and before anything long-running.
It is **the first file read after a context compaction or a restart**, and it must be
enough on its own to resume:

```
# NOW -- 2026-10-12 14:05
doing:      T-0042 Save and load a night's play -- step 3/5: the ward's codec written, tests green
last commit: abc1234 (pushed) on <branch>
next:       T-0043, T-0047, T-0051
waiting on eli:   T-0009 (the climax), T-0012 (Steam Direct fee -- the 30-day clock)
waiting on valve: --
milestone:  M1 -- 4 of 11 done
gate stamp: owed since def5678 (last stamped: 9910bec)
standing permissions: "merge milestone PRs to main" -- Eli, 2026-10-12
overrides:  --
```

### MILESTONES.md

One section per milestone: its name, one goal sentence, its exit criteria as
checkboxes (each naming the tickets or the evidence that ticks it), and a status line.
A milestone closes when every exit criterion is ticked; closing one opens a pull
request to `main`.
