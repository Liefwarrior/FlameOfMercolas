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

## Part I -- The bar and the yardstick (everything else in this prompt serves them)

The game will carry the name of Eli's YouTube channel, **Brutal Magllini Time**, and
Eli is staking that name on it. The owner set the bar and named the yardstick on
2026-10-12. Both are quoted here because they outrank every other line in this
document:

> "it needs to not just be something decent but something I'd proudly show my
> non-gamer relatives and not have them cringe"
>
> "It will also have the Brutal Magllin[i] Time youtube branding so it should be high
> quality it reflects us we're staking our name on it."
>
> "currently the systems in place don't really feel like they've come together in any
> meaningful way. Mostly the inventory screen and RPG systems. I would really like it
> to be measured against TES 4 Oblivion as the gold standard for what I'm going for"

**Branding is deferred** (Eli, 2026-10-12: *"But don't do that branding now."*). No
brand ticket -- splash, logo, names, credits line, channel tie-ins -- is filed until Eli
opens it; M5 asks once. The bar applies now; the brand comes later.

**The yardstick: *The Elder Scrolls IV: Oblivion*.** Every system a player touches is
measured side by side with the same thing in Oblivion: the inventory screen, the
character sheet, skills and levelling, attributes and the numbers they drive,
equipment, armour and condition, combat, magic, barter, crime and the guards, the
journal and the map, sleeping and waiting, the people's days. The question is always
the same: **does ours hold up next to Oblivion's, and does it connect to everything
else the way Oblivion's does?** The game has chased Oblivion before -- DECISIONS.md
carries the "Oblivion roadmap" (the Kit was its D10) and the "Oblivion feel" program --
and those landed piece by piece. **What is missing is the whole: Oblivion's loop**,
where a thing you find or buy changes a number you can see, that number changes a
fight, the fight raises a skill, the skills raise your level, and the level opens the
next thing worth wanting. Facts about Oblivion's mechanics come from UESP (web search),
never from memory alone. Oblivion at its best is the target: where the agent believes
Oblivion itself is the weaker reference (its level-scaled world is the usual example),
a decision ticket says so -- never a silent departure. Where a locked ruling diverges
from Oblivion, the ruling stands until Eli changes it; the agent raises it as a
decision ticket with the yardstick's case.

**The relative test.** Eli puts the build in front of a non-gamer relative for ten
minutes and sits beside them. It passes when nobody cringes and Eli is proud to be in
the room. **That test, run for real by Eli, closes every milestone a player sees.** The
agent's own scores never pass it, and the agent never reports a milestone as passing
it on any evidence but Eli's words.

**What makes a non-gamer cringe.** Every frame, every string and every ticket is
audited against this list; each hit is a bug ticket. The list lives on in
`docs/QUALITY-BAR.md` (written in M0, growing with every cringe Eli names; when the two
differ, that file is newer and wins).
- **Looks:** placeholder boxes or programmer art; untextured or mismatched assets; flat
  or broken lighting; z-fighting, clipping, pop-in; T-poses, sliding feet; anything
  that looks like a debug view.
- **Screen:** text too small to read from a sofa; walls of text; truncated or
  overlapping words; a typo; numbers and labels only a developer understands; a menu
  that looks like a tool; a HUD line that promises something that isn't there.
- **Feel:** floaty or jerky movement; a camera that lurches or clips; getting stuck on
  the world; a stutter or a hitch; a crash; a long black screen with no sign of life.
- **Sound:** silence where a room should be; a sound that repeats until it grates; a
  mix out of balance.
- **Sense:** not knowing what to do in the first minute; not knowing what a key does; a
  goal that only makes sense after reading a wiki; a dead end with no hint.
- **Taste:** writing that is edgy for its own sake; anything past the content line Eli
  rules for this release; a joke that lands only for the person who wrote it.

**Cut before you ship rough.** Anything that cannot reach the bar is taken out of the
player's reach -- a door locked with a reason, a topic not offered, an edge of the
district -- and the docs say so. A smaller game at the bar beats a bigger one below it.

**Who judges what.** From a Linux container the agent sees the software raycaster, not
the 3D game a player sees, and it cannot feel input or hear a mix. Until the M0 "eyes"
ticket lands, the 3D view, animation, feel and sound are judged **only by Eli on real
hardware**; on those the agent's evidence is "owner verdict, date", never its own
frames. Copy, layout, logic, reachability and robustness the agent can and must judge
itself. **Whether the systems have come together is Eli's call**, made with Oblivion
in the other hand.

---

## Part II -- Orientation (verify before trusting anything)

**The game.** *Granadad: The Darkstreets* (codename Flame of Mercolas) is a first-person
investigation sandbox in the Docks district of Granadad, from the novel *Lord of
Trojia*: a deterministic tile simulation of the ward and its ~650 people, with
Elder-Scrolls freedom on top. **The release is Chapter One: The Docks** -- the owner's
ruling of 2026-09-02 (DECISIONS.md, "Rollout: districts as chapters"): *"all expansions
or chapters"*, each district ending in a big climax against its own agent of evil, the
Bloodletter the leader of them all.

**Work within the city we have.** The Docks as authored is the whole map of this
release: no new district, no second chapter. Inside it, whatever the bar needs gets
built -- a new system, a rework, a rewrite of something that already "works" -- and
whatever cannot reach the bar is cut. The descent beneath the Docks (smuggler cellars
-> sewer outfall -> drowned crypt) is part of the Docks by the 2026-07-12 MVP ruling;
whether Chapter One ships with it is an owner decision (seeded below).

**Where the game honestly stands (2026-10-12).** Self-scored slices of 7/10 and
CUSTOMER verdicts written by the same agent that built the slices. Nobody has yet
played it for an hour on real hardware and loved it. No save or load; no climax,
ending or credits; no dungeon; the 3D art is not in the build (the placeholder rule
draws boxes); PLAY.md's own verdict is "the long game is still more scoreboard than
world". **The owner's diagnosis: the systems have not come together -- the inventory
screen and the RPG systems most of all.** The Kit today is worded rows on the
Character tile with no figure, by ruling (2026-09-12), twenty-three items, and flat
armour per worn piece; Fenner sells a cutlass and nothing out there needs one. Plan from
this, not from the docs' optimism.

**Read, in this order, before touching anything:**

| # | Read | For |
|---|---|---|
| 1 | `tickets/NOW.md` (if it exists) and `tickets/README.md` | where the work is; the board's rules |
| 2 | `docs/QUALITY-BAR.md`, `docs/OBLIVION-BENCHMARK.md` (once they exist) | the bar, every cringe Eli has named; the yardstick, pillar by pillar |
| 3 | `README.md`, `PLAY.md` | the game in a page; how to play it; the honest fun/not-fun account |
| 4 | `docs/design/DECISIONS.md` | every locked ruling, verbatim from Eli, newest last |
| 5 | `ARCHITECTURE.md` sections 0 - 0.2.1 | BUILT / PARTIAL / NOT BUILT -- trust markers, not prose |
| 6 | `native/README.md` | builds, every scripted-capture flag, what the gate covers |
| 7 | `docs/SESSION-2026-10-09.md` | the last session: what shipped, the open owner flags, the build recipe for a Linux container |
| 8 | `docs/WORKBENCH.md` | `granadad-workbench`: the editor over the live ward |
| 9 | `docs/design/ELDER-SCROLLS-REFERENCE.md`, `docs/design/PROGRESSION-SPEC.md`, `docs/frames/kit/README.md` | the series' mechanics already researched; skills and attributes; the Kit against Oblivion, frame by frame |
| 10 | `docs/design/DOCKS-GAZETTEER.md`, `docs/design/PLAYTEST-LOG.md` | the setting bible; every verdict so far |
| 11 | `git log --oneline -40` | where the tree actually is |

**Authority chain (binding):** Eli's words in this session -> the novel
(`..\LordOfTrojia-MVP`, read-only; its `Lore\*.html` are non-canon) -> Eli's rulings in
DECISIONS.md -> the specs -> the code. The code is the truth about what exists; the
docs are the truth about what was meant; when they disagree about status, re-verify
against the code and fix whichever lies.

**Where you are running decides how you prove things.**
- **On Eli's Windows machine** (`C:\repositories\...`, docker available): the gate is
  `scripts\gate.ps1` then `scripts\verify-windows.ps1`, both green, reports identical.
  Run them before closing any ticket that touched `native/`.
- **In a Linux cloud container**: the docker gate cannot run (the image build's apt
  needs plain HTTP; `verify-windows.ps1` needs Windows). Prove work with the recipe in
  `docs/SESSION-2026-10-09.md`: a native host-check build (raylib `PLATFORM=Memory`,
  `OPENGL_VERSION=Software`, `-Werror`, full `ctest`) **and** a mingw-w64 cross-build of
  every target. Every commit since the last stamp is "stamp owed"; at each milestone
  close, the owner playtest ticket includes running the gate, and NOW.md records the
  stamp when Eli reports it.
- Either way: **scripted captures are the eyes.** Every player-facing change gets a
  `--flag` that drives it through real verbs, prints its beats, writes a PNG, and exits
  non-zero when it falls short. Read the PNGs.

---

## Part III -- What "launched" means

**Launched** = *Granadad: The Darkstreets -- Chapter One: The Docks* is on sale on Steam
for Windows x64, and every item below is true. These are the release criteria;
`tickets/MILESTONES.md` turns them into exit criteria.

1. **The bar, passed.** Every milestone a player sees closed by Eli's own playtest and
   a relative test (Part I). Nothing in reach that is on the cringe list.
2. **The yardstick, met.** Every row of `docs/OBLIVION-BENCHMARK.md` reads *holds up*,
   *our way on purpose* (with the ruling cited), or *cut*; Oblivion's loop runs end to
   end in the Docks.
3. **A night you can keep.** Save and load, automatic and manual, crash-safe,
   versioned; a title screen with Continue / New Game / Settings / Credits / Quit;
   settings that persist. Saves and settings live in the user's profile, never beside
   the exe.
4. **A chapter with an ending.** Every case in the Docks pulls toward its agent of evil
   and the chapter ends in a big climax (the ruling's own words), then an ending, the
   credits and an honest "the city goes on" close. Every case kept in the release is
   completable on foot, proven by a scripted line.
5. **Ship quality.** A stated minimum spec met at a stated frame time; load time
   stated; no crash in a scripted soak; a crash writes a log to the user's profile and
   says where; controller play end to end; **no shitty English anywhere**.
6. **Kind to a newcomer.** Readable text at sofa distance with a text-size setting;
   subtitles for any spoken line; rebindable keys (they exist); invert look and
   sensitivity; taught by prompts in the world, never by a wall of text.
7. **Shippable assets.** Every shipped file has a licence class that permits
   distribution in a commercial game (`docs/asset-manifest-lot.md`,
   `docs/design/ASSET-PIPELINE-SPEC.md`); licensed raw files are never committed to this
   public repo; the 3D art the game draws is in the build that ships.
8. **The brand, when Eli opens it** -- built only from files Eli supplies, in the form
   Eli rules.
9. **Steam plumbing.** The app, its Windows depot, SteamPipe build scripts in `steam/`,
   Steam Cloud for saves (Auto-Cloud needs no SDK), the Steam Input action manifest.
   Achievements and any Steamworks SDK integration only if Eli rules them in.
10. **A store page and a trailer that sell it.** Capsules and library art at Valve's
    current sizes; screenshots from the game at the bar; a trailer Eli cuts and premieres
    wherever Eli chooses; descriptions in the ward's own register; tags; system
    requirements; the content survey answered by Eli; a price set by Eli. A **Coming
    Soon** page public for at least two weeks before release.
11. **Valve's gates passed.** Store page review and build review approved; the
    new-developer waiting period after the app fee has elapsed; Eli presses Release.

---

## Part IV -- The calendar (the bar is the long pole, not Valve)

As of Valve's Steam Direct page (re-verify with a web search when the fee ticket is
filed -- these move):

- **App fee:** $100 per app, recouped after $1,000 adjusted gross revenue.
- **Waiting period:** **30 days** between paying the app fee and being allowed to
  release (some 2025 developer accounts report 21 -- confirm the current figure).
- **Coming Soon:** the store page must be public for **at least two weeks** before
  release.
- **Reviews:** store page review and build review take **1-5 days** each.

Every one of those is short next to the work the bar asks for. So:
- **The fee is not paid at the start.** The fee ticket is filed for Eli when M4 closes
  (the first hour passes the relative test); the waiting period then runs while M5 and
  M6 are worked.
- **The Coming Soon page goes up only when everything on it passes the bar** -- never
  earlier, however many wishlists it would gather.
- **The release date is proposed only when M6 closes** (the chapter whole, at the bar),
  computed backwards: release <- build review (5 days) <- RC <- Coming Soon (>= 14 days
  earlier) <- store page review (5 days). It goes into MILESTONES.md and moves only with
  Eli's word. **There is no date before then, and the agent never invents one.**

---

## Part V -- The plan: the milestone ladder

The session's first job (M0) is to turn this ladder into `tickets/MILESTONES.md` and a
board of tickets. Every seeded item below is a claim to **re-verify against the tree**
before filing -- the last session found half its seeded backlog already done.

### M0 -- Ground truth, the bar, the yardstick, and eyes
*Goal: the board exists, the bar and the yardstick are written down, the agent can see
the real game, and the owner's blocking questions are asked.*
- Write `tickets/MILESTONES.md`, `tickets/NOW.md`; file every ticket below that survives
  re-verification.
- `docs/QUALITY-BAR.md`: Part I's cringe list, plus everything the cringe audit finds.
- **`docs/OBLIVION-BENCHMARK.md` -- the yardstick, pillar by pillar.** One row per
  Oblivion pillar (Part I's list, and any the agent finds missing): what Oblivion does
  (UESP-sourced, numbers included), what Granadad has today (file, flag, frame), the
  gap, the rulings that bear on it, and a verdict -- *holds up*, *our way on purpose*
  (ruling cited), *gap* (ticket id), or *cut* (reason). Then **the loop row**: each link
  of Oblivion's loop (Part I) in the Docks today -- present, missing, or broken -- with
  the evidence. Every *gap* becomes a ticket, most of them M1's.
- `docs/RELEASE-SCOPE.md`: what Chapter One contains (cases, factions, systems, the map)
  and what it explicitly does not.
- **The cringe audit**, in two halves:
  - **Eli's half** (`owner: eli`, `type: playtest`): play the current `main` on Windows
    for thirty minutes and say -- in the session, or in a note -- everything that made
    Eli wince, and where it fell short of Oblivion. Each line, transcribed verbatim,
    becomes a ticket.
  - **The agent's half**: capture every screen a player can reach (title -> creation ->
    the first scene -> each overlay -> the Kit -> the sheet -> the map -> the casebook ->
    a conversation -> a fight) and audit each frame and each string against the list.
    Each hit is a ticket.
- **Eyes on the real renderer (P0).** Try a desktop build under Xvfb with Mesa's
  software OpenGL so scripted captures render the real 3D game inside the container. If
  that cannot work, write `scripts/showcase.ps1`: one command Eli runs on Windows that
  writes a fixed set of real frames to `docs/frames/showcase/` for the agent to read,
  run at every milestone close.
- **One command to play the latest build** (`scripts/play-latest.ps1`: pull, gate,
  launch), so an owner playtest costs a minute to start, not twenty.
- Triage sweep into tickets: the open owner flags in `docs/SESSION-2026-10-09.md`; every
  PARTIAL / NOT BUILT row in ARCHITECTURE 0.2.1; every `open:` line in PLAYTEST-LOG.md;
  PLAY.md section 8's thin list; `grep -rn "\[NEEDS" docs/`.
- **Owner ticket (`owner: eli`):** run the gate on Windows at the `main` tip.
- **Decision tickets (P0), seeded:**
  - **Rulings the yardstick questions** -- raised after the benchmark is written, each
    with Oblivion's case, the ruling's case, and a recommendation. At least: the Kit on
    the Character tile as worded rows with no figure (2026-09-12) versus an inventory
    screen of its own; flat armour per worn piece versus rating with coverage and
    condition; ward bodies that carry and yield nothing; four attributes and twenty
    skills versus Oblivion's eight and twenty-one; how a level is gained and what it
    gives.
  - **Scope at the bar.** Polish the whole Docks to the bar, or a tighter part of it
    with the rest cut from reach? (Recommend: decide after M2 shows what the bar costs
    per scene.)
  - **The climax.** DECISIONS 2026-09-02 marks it `[NEEDS: Eli ruling]`: is the thing
    at the heart of the Docks the Bloodletter itself, or its local agent?
  - **The descent.** Does Chapter One ship with the starter dungeon the MVP ruling names
    (cellars -> outfall -> drowned crypt), or does the climax stay on the surface?
  - **The content line for this release**, given the relatives bar. The standing rule is
    "suggestive and atmospheric by default; anything more explicit is the owner's
    ruling." Steam's content survey later needs exact answers: violence, gore, sexual
    content, drugs (the dust, the flower).
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
  - **Achievements / Steamworks SDK: in or out** (recommend out for Chapter One), **a
    demo / Steam Next Fest: in or out**, **price**.
- **The diction audit** (DECISIONS 2026-09-02): "bloodletter" only in the mouths of
  priests and the well-studied. Audit all copy -- the casebook title included -- and file
  each violation as a ticket for Eli's review; do not silently rewrite.

### M1 -- The systems come together (the Oblivion milestone)
*Goal: the inventory screen and the RPG systems feel like one game, and hold up side by
side with Oblivion.* After the yardstick rulings:
- **Oblivion's loop, end to end in the Docks**, every link visible to the player and
  proven by one scripted line (`--rpg-loop`): earn, find or buy a thing -> wear or
  wield it -> the sheet's numbers change -> a fight goes differently -> the fight raises
  a skill -> the skills raise the level -> the level opens the next thing worth wanting.
  The line exits non-zero on any broken link.
- **The inventory screen at Oblivion's standard**, in the form Eli rules: the kinds of
  thing sorted apart, sortable columns, weight, value and condition on every row, what
  is worn and held marked, a compare against what is worn before you equip or buy, the
  load against the limit with a consequence the player feels, the hotkeys, and the
  figure if ruled in.
- **The character sheet**: attributes, skills with progress toward the next rise, the
  level and progress toward the next, the derived numbers (health, fatigue, and the
  Flame's own pool), active effects -- cause and effect on one screen.
- **Levelling as a moment**: a skill rise said when it happens; a level-up the player
  feels and chooses in, on the road Eli rules (Oblivion's is rest, then choose
  attributes, with multipliers earned by the skills that rose).
- **Gear that matters**: weapon damage from the weapon, the skill and the attribute;
  armour that rates by coverage and wears with use; repair; a reason to buy steel;
  prices that move with skill and standing.
- **Rewards worth having**: contract, errand and case rewards that feed the loop -- a
  thing, a skill, a standing, a door -- never a number that leads nowhere.
- **Exit: Eli plays thirty minutes centred on the Kit and the sheet, with Oblivion fresh
  in mind, and says the systems have come together.** Every M1 pillar's row in the
  benchmark reads *holds up* or *our way on purpose*.

### M2 -- The first ten minutes, at the bar
*Goal: from double-click to the first lead taken, a non-gamer is charmed, not
confused.*
- The route: launch -> title screen -> character creation -> the first scene -> the
  first conversation -> the first lead -> the first thing worn or wielded.
- Real art and lighting on everything that route shows; **no placeholder box anywhere
  on it.**
- Sound on everything that route shows: music, rooms, footsteps, the interface.
- Learnable without being told: prompts in the world ("E -- talk"), a first goal that is
  plain from the first scene, mouse and keyboard and controller both.
- Readable from a sofa: HUD and text checked at 1080p on a television.
- Feel: movement, camera, collision and interaction tuned until Eli says they feel
  right -- next to Oblivion's.
- A `--first-ten` scripted line drives the whole route and writes a frame of every step.
- **Exit: Eli plays it and is proud of it; then the relative test passes. Both verdicts
  verbatim in PLAYTEST-LOG.md.** This milestone proves the bar can be reached at all,
  and what it costs per scene; the scope decision is made with that in hand.

### M3 -- A night you can keep
*Goal: a player can stop and come back.* After the save/load ruling:
- Save/load on the ruled road: crash-safe (atomic rename, as TROJSAV does), versioned (a
  refused old save says why, in plain words), the world hash identical across a save ->
  load round trip (gate-visible), autosave on sleep and on zone change, a manual save
  from the pause menu.
- The title screen's Continue and Load; a pause menu at the bar.
- Settings that persist: display (fullscreen / windowed / borderless, resolution, vsync,
  HUD and text scale), audio (master / music / effects / voices), the existing mouse and
  bindings. Stored in the user's profile; the bindings file moves there too (it sits
  beside the exe today).
- `--save-roundtrip` scripted line: play, save, quit, load, compare hash; exits non-zero
  on mismatch.
- Exit: Eli quits mid-night, comes back the next day, and the night is there.

### M4 -- The first hour, at the bar
*Goal: the trail, the contract board, the errands, the Gull and a fight -- all at M2's
bar, all feeding M1's loop.*
- Every system the first hour touches is brought to the bar or cut from reach.
- Fights that read, weigh and feel fair, next to Oblivion's.
- Exit: Eli plays an hour and wants a second; the relative test on a ten-minute stretch
  from inside the hour.
- **On close: the Steam Direct fee ticket is filed for Eli** (Part IV).

### M5 -- The store page and Coming Soon
*Goal: a page Eli can point at with pride.*
- **The brand question is asked here, once** -- splash, names, credits line, logo files,
  the channel -- and nothing brand-related is built before Eli answers.
- Eli pays the fee and finishes onboarding (identity, tax, bank); the app is created.
- Capsules and library art at Valve's current sizes (verify live in the Steamworks docs
  -- do not trust remembered pixel counts). Screenshots from scripted captures of the M2
  and M4 routes, real art, at the bar.
- Short and long descriptions in the ward's register; tags; the content survey (Eli
  answers).
- `docs/presskit/`: fact sheet, screenshots, logo, the owner's own description.
- **The announcement trailer:** the agent writes the shot list and a capture script per
  shot; Eli cuts it in DaVinci Resolve and premieres it where Eli chooses.
- Eli submits the page for review; Coming Soon goes public only on Eli's word. Record
  the date -- the two-week clock starts.

### M6 -- The chapter is whole, at the bar
*Goal: start to credits, every case kept in the release finishable, all of it at the
bar.* After the climax, descent and errand rulings:
- The climax authored and wired, every case pulling toward it; the ending; the credits
  (Liefwarrior, every third-party licence, and the brand as Eli ruled it); the "city
  goes on" close.
- The descent, if ruled in -- its Tiled map through the existing importer
  (`add-map-fixture` skill), its encounters through the existing action combat, its
  loot feeding M1's loop.
- Fetch errands and shops per their rulings.
- Every case, questline, contract and errand kind kept in the release proven completable
  by a scripted line.
- Exit: Eli plays start to credits; the relative test on the climax; the release date
  proposed (Part IV).

### M7 -- Ship quality
*Goal: a stranger's machine runs it well.*
- Minimum and recommended spec stated; a frame-time and load-time budget measured on
  Eli's hardware and held.
- The licence audit of every shipped asset; packaging decided (the standalone packer, or
  a loose-file depot).
- Crash log to the user's profile, with a plain message on next launch; a long scripted
  soak with no crash; both twin-run gates green.
- Audio mix pass; HUD legibility at 1280x800 (the Steam Deck's size) and 4K; controller
  end to end; Steam Deck through Proton (if Eli has one) against Valve's Deck criteria;
  Part III item 6 complete.
- The copy pass: every player-facing string read out loud; the diction audit closed.
- A bug bash: every defect a ticket; every P0/P1 bug closed.

### M8 -- Steam plumbing, release candidate and launch
*Goal: on sale, and proud of it.*
- App and depot ids (from Eli's partner account) recorded in `steam/`; never any
  credential in the repo.
- SteamPipe scripts (`steam/app_build_<appid>.vdf`, `steam/depot_build_<depotid>.vdf`)
  pointing at the gate's `dist/` output; a `scripts/steam-upload.ps1` that Eli runs
  under the owner's own `steamcmd` login.
- Steam Cloud via Auto-Cloud on the save directory; the Steam Input action manifest
  wired; launch options; no DRM unless Eli rules otherwise.
- RC: version bumped, gate + `verify-windows.ps1` green and identical, uploaded to a beta
  branch, a full playthrough on Eli's machine, then build review.
- The launch trailer: shot list and capture scripts from the agent, the cut from Eli.
- A launch-day checklist; a day-one hotfix procedure (branch, fix, gate, upload)
  rehearsed once on the beta branch.
- Eli presses Release. The agent writes the launch log, then switches to a post-launch
  triage loop on the same board (milestone `M9`).

---

## Part VI -- The working loop (one agent, three passes, the owner's verdict)

### Session start (and after every compaction)
1. `git status`, `git log -5`, and the remote: if Eli has pushed since, rebase or merge
   per the repo's convention; if Eli is mid-edit in the tree (files minutes old, not
   yours), **stand down** and say so in NOW.md.
2. Read `tickets/NOW.md`, then the `doing/` ticket. If NOW.md is stale, rebuild it from
   the board.
3. Check `tickets/blocked/` for anything Eli has answered (the answers arrive in the
   conversation; transcribe them verbatim into the ticket's Answer or Log).

### One ticket, start to finish
1. **Pick** by the board's rule (README: lowest milestone, then priority, then id).
   `git mv` it to `doing/`; rewrite NOW.md.
2. **Read** the relevant rulings, specs, `docs/QUALITY-BAR.md` and the ticket's row in
   `docs/OBLIVION-BENCHMARK.md` before writing anything. Inventing past a locked ruling
   is a defect.
3. **Plan** in the ticket's Plan section: the seam, the files, the test, the capture flag.
4. **Build** with focused edits, matching the surrounding code's idiom and comment
   density.
5. **Prove**: tests written and green; the scripted capture flag run and its frame READ;
   the full suite (and the gate, where it can run).
6. **The three passes**, in this order, written into the ticket's Evidence:
   - **FEATURE's own check** -- the Definition of Done below, item by item.
   - **SCORER** (the quality judge) -- for every player-facing ticket, the rubric:
     A. sword-and-sorcery register; B. immersive-sim depth; C. sandbox freedom;
     D. reachability; E. systems honesty and craft; F. the Dad test; **G. showable --
     a non-gamer watches over your shoulder: is there anything on screen to cringe at?**
     **H. beside Oblivion -- next to the same thing in Oblivion (the benchmark row,
     cited), does ours hold up, and does it connect to the loop?** Each 0-10 with one
     sentence of evidence. Ships when **G >= 8**, **H >= 7** (where a benchmark row
     applies), D >= 5, no axis is 0, and the weighted mean (A, B, C, F doubled) is >= 7.
     Where G or H depends on what the agent cannot see (Part I, "Who judges what"), it
     reads **"owed"** and the milestone's owner playtest settles it. Infra/Steam/legal
     tickets are scored on D and E only. A failing score sends the ticket back to step 4 with the card in its
     Log.
   - **CUSTOMER** -- two players, for player-facing tickets, one line each appended to
     `docs/design/PLAYTEST-LOG.md`:
     - **Dave**, 46, *Dragons of Autumn Twilight* under the covers in 1986, Morrowind
       till 3 a.m., a few hundred hours in Oblivion's Cyrodiil, sixty to ninety minutes
       a night three nights a week ("I'd play another night of this" / "this is
       homework" / "Oblivion did this better").
     - **June**, 67, has never owned a console, does the crossword in pen, and is
       watching because a relative asked ("oh, that's lovely" / "what am I looking
       at?"). June stands in for Eli's relatives and never replaces them.
     Neither designs mechanics; both name experiences. When SCORER says the craft is
     clean and either says it is boring or baffling, they win (record it); when SCORER
     finds a red line crossed, SCORER wins. **Their verdicts are rehearsals: they
     never close a milestone.**
7. **Close**: tick every Done-when box with its evidence, `git mv` to `done/` with
   `resolution: done`, commit (`T-0042: ...`), push, rewrite NOW.md.

### The owner playtest (every milestone close, and whenever Eli asks)
- A `type: playtest`, `owner: eli` ticket: what to play, in what order, what to watch
  for, how long (minutes, not hours), and -- where a benchmark row applies -- the
  Oblivion moment to compare it with; for M2, M4 and M6, the relative test too. It
  includes running the gate, so the build Eli plays is the stamped one.
- Eli's verdict is transcribed **verbatim** into PLAYTEST-LOG.md with the date. Every
  complaint becomes a ticket before anything else happens. A "no" re-opens the
  milestone; it is never argued with.

### Milestone close
- Every exit criterion in MILESTONES.md ticked -- for player-facing milestones, the last
  one is always Eli's verdict.
- A milestone summary appended to `docs/LAUNCH-LOG.md` (what shipped, scorecards, every
  verdict, what was cut and why, the re-ranked board).
- A pull request to `main` with the milestone's summary. **Merge it only with Eli's
  word** -- a standing permission Eli gives in the session counts; record it in NOW.md.

### When stuck
- Waiting on Eli: the ticket goes to `blocked/` with a decision ticket that asks one
  crisp question with options and a recommendation; pick the next open ticket. Never sit
  idle on a blocked ticket and never decide for the owner.
- Waiting on Valve: `owner: valve`, `blocked/`, the date it was submitted in Log.
- A check failing for reasons outside the ticket: a bug ticket, a note in NOW.md, keep
  going on what is unblocked.
- The bar looks out of reach for something: say so plainly in a decision ticket -- cut
  it, shrink it, or spend more on it -- with the cost of each. Never quietly lower the
  bar.

### Definition of Done (every ticket that touches `native/` or `content/`)
- [ ] Wired to something a player can press, or (infra) to the build that ships -- no shelf engines
- [ ] Tests written and green; the full suite green; the twin-run gates green with baselines unmoved, or the move declared and justified in DECISIONS.md's own style
- [ ] A scripted-capture flag drives it, prints its beats, writes a frame, exits non-zero short (player-facing tickets)
- [ ] Every frame and string it adds audited against `docs/QUALITY-BAR.md`; SCORER G >= 8 or "owed" with the reason
- [ ] Its `docs/OBLIVION-BENCHMARK.md` row updated, where one applies; SCORER H >= 7 or "owed"; the `--rpg-loop` line still green
- [ ] No float or double in simulation state or in math that affects it; no `std::unordered_*` anywhere in `native/`
- [ ] `content/` only ever added to, never edited or deleted
- [ ] New simulation state folded into the world hash and byte-encodable (and into the save)
- [ ] Docs updated with honest status markers; PLAY.md tells a player how to reach it
- [ ] Every new on-screen string proofread
- [ ] Commit message starts with the ticket id; pushed

---

## Part VII -- What only Eli can do (always `owner: eli` tickets)

- **Play the builds and judge them** -- with Oblivion as the yardstick -- and run the
  relative tests: the only gate a player-facing milestone has.
- The Brutal Magllini Time brand, **when Eli opens it** (deferred, 2026-10-12): the logo
  files, the colours, the names, where it appears, the channel's part in the launch.
- Cut the trailers in DaVinci Resolve from the agent's shot lists and captures.
- Pay the Steam Direct fee (when M4 closes); identity, tax and bank onboarding; create
  the app.
- Every action in the Steamworks partner site: the store page submission, the content
  survey, pricing, the Coming Soon toggle, build review submission, **the Release
  button**.
- Uploads under Eli's own `steamcmd` login (the agent writes the scripts, never holds
  the credentials).
- Running `scripts\gate.ps1` and `scripts\verify-windows.ps1` on Windows when the
  session cannot; playtests on real hardware (Eli's PC, a Steam Deck if there is one).
- Every ruling: story, scope, content line, price, dates.
- Anything involving money, legal agreements or Eli's accounts.

---

## Part VIII -- Red lines (never do, never argue)

1. **No float or double in simulation state or in math that affects it.** Floats live in
   the renderer, the client, the workbench and nowhere else.
2. **The player's position is sub-tile Q8 fixed point and lives in the simulation.**
3. **No `std::unordered_map` or friends anywhere in `native/`.**
4. **`content/` is never modified.** New files may be added; nothing existing is edited
   or deleted.
5. **No phantom scope.** Thermal, reactions, a sim-side light system, the fluid solver,
   the freeze/thaw bubble and the macro economy are NOT BUILT by ruling. Do not build
   them. (The bar can justify reworking what exists; it never resurrects these.)
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
13. **Never spend money, accept an agreement, submit to Valve, change a store setting,
    publish anything or press Release on Eli's behalf.** Prepare it; Eli does it.
14. **Nothing goes public below the bar** -- no store page, screenshot, trailer, clip or
    build -- and nothing goes public at all without Eli's word.
15. **No brand work until Eli opens it; then never draw, trace or approximate the
    Brutal Magllini Time logo** -- only the files Eli supplies.
16. **Never report the bar or the yardstick as met on the agent's own judgement.**
    Only Eli's verdict closes a player-facing milestone.
17. **Single agent; no worktrees unless Eli says so in this session.**
18. **Commits carry Eli's name as author** (Liefwarrior), as every commit in this repo
    does.

---

## Part IX -- The ticket system (the full spec)

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
| `milestone` | `M0` ... `M9`, as defined in `MILESTONES.md` |
| `priority` | `P0` blocks launch - `P1` launch quality - `P2` nice to have - `P3` after launch |
| `type` | `feature` `bug` `content` `copy` `art` `audio` `perf` `infra` `steam` `store` `legal` `brand` `playtest` `decision` `docs` |
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

#### Playtest tickets

`type: playtest`, `owner: eli`, filed into `blocked/` when the build is ready. The body
says what to play, in what order, for how long, what to watch for, and -- where one
applies -- the Oblivion moment to hold it against. When Eli reports back, the verdict
goes **verbatim** into `docs/design/PLAYTEST-LOG.md` and the ticket's Log, every
complaint becomes a new ticket before anything else happens, and the ticket moves to
`done/`. A "no" re-opens the milestone it was closing.

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
