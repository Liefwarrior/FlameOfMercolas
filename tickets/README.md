# Tickets -- the repo's board

A streamlined, file-based ticketing system for one agent and one owner working
through the repo to a release. **Plain files and git -- no tool, no service, no
database.** One file per ticket; **the folder a ticket sits in is its state.**
This file is the spec. `docs/GOLD-MASTER-PROMPT.md` carries the same text; if the
two ever differ, this file wins and the other is corrected.

## Layout

```
tickets/
  README.md       this spec
  TEMPLATE.md     copy it to start a ticket
  NOW.md          the lifeline: where the work is RIGHT NOW (<= 40 lines, rewritten, never appended)
  MILESTONES.md   the milestone ladder: each milestone's goal and exit criteria
  open/           filed, not started
  doing/          in progress -- AT MOST ONE FILE (one agent, work-in-progress limit 1)
  blocked/        waiting on somebody else: the owner, a ruling, hardware
  done/           finished (or closed as won't-do), with evidence
```

## A ticket

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
| `milestone` | `M0` ... `M7`, as defined in `MILESTONES.md` |
| `priority` | `P0` blocks gold - `P1` gold quality - `P2` nice to have - `P3` after gold |
| `type` | `feature` `bug` `content` `copy` `art` `audio` `perf` `infra` `legal` `brand` `playtest` `decision` `docs` |
| `owner` | `agent` (the session does it) - `eli` (only the owner can) |
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

### Decision tickets

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

### Playtest tickets

`type: playtest`, `owner: eli`, filed into `blocked/` when the build is ready. The body
says what to play, in what order, for how long, what to watch for, and -- where one
applies -- the Oblivion moment to hold it against. When Eli reports back, the verdict
goes **verbatim** into `docs/design/PLAYTEST-LOG.md` and the ticket's Log, every
complaint becomes a new ticket before anything else happens, and the ticket moves to
`done/`. A "no" re-opens the milestone it was closing.

## Moving tickets

A move is `git mv`, and it rides in the commit that justifies it.

| From -> to | Only when |
|---|---|
| `open` -> `doing` | `doing/` is empty, every `depends` id is in `done/`, size is not `L` |
| `doing` -> `done` | every Done-when box is ticked AND has a line in Evidence; tests green; committed and pushed |
| `doing` -> `blocked` | the blocker is written in Log, a decision or owner ticket for it exists, NOW.md says so |
| `blocked` -> `open` | the blocker cleared (Log line naming what cleared it) |
| any -> `done` as `wontfix` / `duplicate` | the reason is a Log line |
| `done` -> `open` (reopen) | it regressed; Log line with the evidence |

## Choosing the next ticket

Deterministic, so nobody has to think about it: among `open/` tickets whose `depends`
are all in `done/` and whose size is not `L`, take **the lowest milestone, then the
highest priority (P0 first), then the lowest id.** The owner can override by saying so;
the override is written into NOW.md with the date.

## Commits

- Every commit that does a ticket's work starts with its id: `T-0042: the ward's codec`.
- Board-only commits (filing, splitting, grooming) start with `board:`.
- Session-log commits start with `docs(session):`.
- Push after every ticket closes. Never leave finished work only in the working tree.

## Rules that keep it streamlined

- **New work found mid-ticket becomes a new ticket in `open/` immediately** -- one line
  of Why is enough -- and the current ticket does not widen.
- **A bug ticket's Why is a repro**: steps, expected, actual. Its Done-when includes a
  regression test or a scripted line that fails before the fix and passes after.
- **`L` is not a size, it is a signal**: split it into `S`/`M` tickets with `depends`
  between them, close the `L` as `duplicate` of the first, move on.
- **No estimates beyond S/M/L, no points, no sprints, no assignees beyond the two
  owners.** The milestone ladder is the schedule.
- **Viewing the board needs nothing but a shell:** `ls tickets/*/` is the whole board;
  `grep -l 'milestone: M1' tickets/open/*` is a milestone; `grep -l 'owner: eli' -r
  tickets/blocked` is everything waiting on the owner.

## NOW.md -- the lifeline

Rewritten (never appended) at every ticket boundary and before anything long-running.
It is **the first file read after a context compaction or a restart**, and it must be
enough on its own to resume:

```
# NOW -- 2026-10-12 14:05
doing:      T-0042 Save and load a night's play -- step 3/5: the ward's codec written, tests green
last commit: abc1234 (pushed) on <branch>
next:       T-0043, T-0047, T-0051
waiting on eli:   T-0009 (the climax), T-0012 (playtest: the Kit and the sheet)
milestone:  M1 -- 4 of 11 done
gate stamp: owed since def5678 (last stamped: 9910bec)
standing permissions: "merge milestone PRs to main" -- Eli, 2026-10-12
overrides:  --
```

## MILESTONES.md

One section per milestone: its name, one goal sentence, its exit criteria as
checkboxes (each naming the tickets or the evidence that ticks it), and a status line.
A milestone closes when every exit criterion is ticked; closing one opens a pull
request to `main`.
