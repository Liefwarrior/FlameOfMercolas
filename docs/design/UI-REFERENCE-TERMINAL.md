# The terminal-panel register

The owner named the target and then showed it: **CultGame** (Steam 2345980), plus
the Warsimlike family generally. This file is the grammar read off **ten** of his
reference frames, so no phase has to guess at it: the Dominion/Fire stacked panel,
the Summon Demon art panel, the Create Homunculus art panel, the tile-selection
map frame, the master/detail Theology screen in both its unowned and its owned
state, the longer Beliefs list, the actor sheet, and — most directly applicable of
all — two frames of the game actually being played, showing a skill check
resolving and a pending decision.

Binding context, in his words: *"I know we have a first person view but it can
work with ASCII menus."* The first-person world render **stays**. These panels
layer over it. This is not a return to top-down.

And what is NOT in scope, also his words: *"Your flavor and texture is awesome, I
love the font and all that."* The typeface and glyph rendering are untouched.

## The frame

One full-screen frame, divided into stacked panels by horizontal rules. Every
rule — outer border and interior divider alike — is the same alternating motif:

```
+~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-+
```

`+` at the corners and junctions, then alternating `~` and `-`. The vertical
edges **alternate per row** between `|` and `!`:

```
+~-~-~-~-~-~-~-~-~-+
!Actions / Rituals |
|Beseech the dark  !
!auspices to enter |
+~-~-~-~-~-~-~-~-~-+
```

That alternation is the texture. It is not decoration for its own sake — a
straight `|` column reads as a hard modern border, and the flicker of `|`/`!`
reads as a terminal. Keep it.

## The stack, top to bottom

### 1. Breadcrumb header

The nav path, slash-separated, leaf highlighted in the colour of the thing being
looked at:

```
!Cult Creation / Dominion / Fire
!Actions / Rituals / Summon Demon
```

This is how a deeply nested text menu stays navigable. **Every panel in this
project should carry one.** Where the panel is a task rather than a location, the
header states the task instead, as an instruction:

```
!Select a tile of the Pearl Lands to preach the word of Xaleon to!
```

### 2. The hero panel

The largest region — roughly half to two-thirds of the vertical space — holding
either large ASCII art of the current subject, or the map.

Details worth stealing:

- **Art takes its subject's colour when the subject has one, and is plain
  otherwise.** The fire dominion is drawn in red; the summoned demon and the
  homunculus apparatus are plain white on black. Colour is meaningful here, not
  decorative — do not tint art that has no identity colour to claim.
- **A stippled ground is an option, not a rule.** The Summon Demon frame fills
  its dead space with a faint field of `.` and `'` marks and draws the art over
  it; the Create Homunculus frame, same panel type, leaves plain black. Reach for
  the stipple when a piece of art would otherwise float in a large empty panel;
  skip it when the art fills its space. (An earlier draft of this file stated the
  stipple as a rule. It is not — two reference frames of the same panel type
  disagree, so it is a per-scene judgement.)
- **Art can carry structure, not just silhouette.** The homunculus frame draws an
  apparatus — rails, corner clusters, a table, an egg — using `@@`/`@Oo@` corner
  blocks, `|`/`I`/`1` as uprights and `___`/`===` as rails. Text art here depicts
  a *mechanism* the player is about to operate, which is a better use of a hero
  panel than a decorative emblem.

### Panels size to content at the bottom of a stacked frame

The Create Homunculus frame ends immediately after its two-line action list;
there is no padding out to a fixed height. Contrast the master/detail frames,
which *do* hold their height with empty bordered rows.

The rule is about cursor movement, not about panels in general: **hold height
where moving the cursor swaps the content** (so nothing jumps as you arrow
through a list), and **size to content where the content is static** (a stacked
frame you entered deliberately and will leave deliberately).

### 3. The prose panel

Description in plain white, then the mechanical consequence, colour-coded:

```
!Dominion over fire imparts command over the primal element of fire, a symbol of
|creation, destruction, and spirit. Fire gods are often courageous warriors and
!dreamers who believe in following a burning passion to forge one's own destiny.
|Burning Spirit: Your devotees gain a combat damage bonus equal to their faith
!+10% appeal to artisans and warriors
```

The order is **flavour → named effect → number**. The perk name takes an accent
colour, the effect and the numeric line take green. This is how the reference
shows consequence without becoming a spreadsheet: the fiction leads, the number
closes, and the colour does the sorting.

### 4. The option list

Numbered rows, direct-select, laid out in **columns** to use the horizontal
space:

```
!1 - Water      5 - Blood
|2 - Earth      6 - Nothing
!3 - Fire       0 - Back
|4 - Air        Enter - Accept
```

- Each option carries its own identity colour (Water blue, Earth green, Fire
  red, Blood dark red).
- **Selection is an inverted highlight** — background filled in the option's
  colour, text knocked out dark. Not an arrow, not a bracket. The fill is the
  affordance and it is unmistakable at a glance.
- Navigation verbs live in the same list as the content options: `0 - Back`,
  `Enter - Accept`, `1 - Commence`. There is no separate "controls" area.
- **Column count follows content, not a fixed setting.** Four short elemental
  names (`Water`, `Earth`, `Fire`, `Air`) take two columns; nine long ones
  (`Homunculus Theory`, `Martial Practices`, `Wildspeaking`) take one. Pick the
  count from the longest entry against the available width — which is another
  reason every list must be width-aware rather than authored at a fixed layout.

### The effect block scales with what it has to say

Two registers appear, and both are correct in their place:

```
• Devotion to Nature: Earn faith each month equal to      <- named effect + body
  your devotion to nature, which can be increased ...

• +0.1 Evangelism                                          <- terse, number-led
• +1 Preaching Bonus
```

When an effect needs explaining it takes an accent-coloured name, a colon and a
green body. When it is just a number it gets a bare number-led green bullet and no
prose at all. Do not pad a terse effect into a sentence to match a neighbour, and
do not compress an effect that genuinely needs a clause.

## The map frame, specifically

This is the most directly useful reference, because it answers the owner's
in-game complaint. His words: *"It should be easy to do the core gameplay actions
like popping open the map, going to a place, finding the person or thing I want
at that place."*

The reference does it with **four stacked panels**:

```
+~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-+
!Select a tile of the Pearl Lands to preach the word of Xaleon to!
|
!Enter - Select tile
|0 - Back
+~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-+
|            [ the map, coloured glyphs, most of the frame ]
|            [ the SELECTED cell wears a yellow box cursor ]
+~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-+
!Selected tile: (28,13) The Poisoned Dusts
|u - Update map   i - Infrastructure   [o - Overview]   p - People   l - Legend
+~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-+
!Faction:              Independents
|Major Religion:       Agnosticism
!Terrain:              desert
|Population:           483
+~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-+
```

The load-bearing parts:

1. **A moveable cursor on the map** — a box outline in the accent colour around
   the current cell. The map is not a static picture you read; it is a thing you
   drive.
2. **The selection is named in prose underneath** — `Selected tile: (28,13) The
   Poisoned Dusts`. You never have to work out what you are pointing at.
3. **A tab row of views over the same selection** — Overview / People /
   Infrastructure / Legend, current tab inverted-highlighted. **This is the
   answer to "finding the person or thing I want at that place":** a People view
   over the selected place is exactly that question, answered.
4. **An aligned key/value detail panel** for the selection. Labels left, values
   at a common column. Facts, not paragraphs.

Note what this buys that a pile of floating nameplates cannot: it scales. A
crowded quarter does not get more crowded, because the names are not competing
for map space — the map shows *shape and cursor*, and the panel below answers
*what is this*.

This composes with, rather than replaces, the owner's Daggerfall Unity note
(*"buildings are large and use more screen with the names being inside the
building's shape"*). Big legible footprints with names set inside them, **plus** a
cursor, **plus** a named-selection line, **plus** tabbed views over that
selection. The footprint labels answer "where am I looking"; the panels answer
"what is there and who is in it".

## The master/detail frame

The fourth reference frame uses a different and, for our purposes, more important
layout: **a narrow list on the left, a rich detail pane on the right**, with a tab
row above and global nav below.

```
 Theology              [d - Dominions]      b - Beliefs          Faith: 9001*  !
~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-◆
[1 - Earth  200*]      !Earth                                                   !
 2 - Fire   200*       |                                                        |
 3 - Blood             !Dominion over earth gives power over the land and       !
                       |its flora and fauna. Earth gods are often loyal         |
                       !guardians of nature and favor those who selflessly      !
                       |dedicate themselves to nurturing and protecting the     |
                       !world.                                                  !
                       |                                                        |
                       !• Devotion to Nature: Earn faith each month equal to    !
                       |your devotion to nature, which can be increased         |
                       !through cultivating gardens, performing earth           !
                       |rituals, and preserving nature.                         |
                       !• Unlocks rituals:                                      !
                       |      ◦ Bloom                                           |
                       !      ◦ Summon Phantom Dryad                            !
                       |                                                        |
                       |e - Establish (Cost: 200*)                              |
~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-◆
 s - Cult Stats                                                                 |
 0 - Back                                                                       |
~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-◆
```

New elements this frame introduces:

- **A tab row in the header**, current tab inverted-highlighted (`d - Dominions`),
  siblings plain (`b - Beliefs`). Screen title sits left of them.
- **A persistent resource readout, right-aligned in the header** — `Faith: 9001*`.
  The number you are spending is always on screen while you choose.
- **Cost shown inline in the list row** — `1 - Earth  200*` — so the list is
  scannable for affordability without entering each entry. On the selected row the
  cost gets its own highlight.
- **A bullet hierarchy in the detail pane**: `•` for effects, indented `◦` for
  sub-items (`Unlocks rituals:` → `Bloom`, `Summon Phantom Dryad`). Effect names
  take a brighter shade than their bodies.
- **The commit action sits at the foot of the detail pane and restates the cost** —
  `e - Establish (Cost: 200*)`. The irreversible act is adjacent to the
  information justifying it, not parked in a global bar.
- **Global nav at the very bottom**, below its own rule, separated from anything
  contextual.
- Junctions may render as `◆` rather than `+`. Either is in register; be
  consistent within a screen. In the master/detail frames `◆` sits at all four
  corners and at every rule junction, and the **interior column divider alternates
  `|`/`!` exactly like the outer edges** — the texture runs through the whole
  frame, not just its border.

### State changes the row, the label and the verb

A second frame of the same screen, with the entry already acquired, pins down how
state is expressed. All three of these move together:

```
!1 - Earth  200*        !Blood
|2 - Fire   200*        |
![3 - Blood]            !The dominion of blood proclaims that strength can be
                        |gained proportional to a sacrifice. ...
                        |
                        |• Blood: Your cult may collect blood to use in
                        !powerful rituals
                        |• Unlocks rituals:
                        !      ◦ Sacrifice
                        |      ◦ Summon Blood Demon
                        !      ◦ Rite of Fleshgrafting
                        |
                        !Owned
                        |
                        |r - Recant
```

- **The list row drops its cost.** `1 - Earth 200*` and `2 - Fire 200*` still price
  themselves; `3 - Blood` shows no number, because there is nothing left to pay.
- **A state label replaces the price in the detail pane** — a plain `Owned` sitting
  where the cost would argue for itself.
- **The commit verb becomes the inverse action** — `e - Establish (Cost: 200*)`
  becomes `r - Recant`. The action line is contextual to state, never a disabled
  greyed-out button.
- **Selection fill takes the entity's own accent** — Blood's selected row is filled
  bright green with dark knocked-out text, matching its list colour rather than a
  single global highlight hue.
- **The effect name takes the subject's accent, the body takes green** — `• Blood:`
  renders red against a green effect body. Name and body are coloured differently
  on purpose; do not flatten them to one colour.
- **The panes hold their height.** The detail pane keeps its full box with empty
  bordered rows below the content. Nothing reflows or collapses as you move the
  cursor between entries of different lengths — the frame is stable and only the
  text inside it changes. This matters more than it sounds: a list whose layout
  jumps as you arrow through it feels broken.

### Why this layout matters here

Master/detail is the right shape for three surfaces in this project, and phases
should reach for it rather than inventing something:

1. **The chargen quiz.** Answers as the left list, the consequence of the
   highlighted answer in the right pane. The owner spent his choices blind; this
   is the fix, and it is already drawn for us.
2. **The casebook.** Leads as the left list with cold/open/followed state, the
   highlighted lead's detail — place, who, what was found — on the right, and the
   "go here" action at the foot of the detail pane. This is the direct answer to
   the Bloodletter stall.
3. **Any place or person browser** the map's People/Overview tabs need.

The pattern also composes with the stacked-panel frame: header and global nav are
the same in both; only the middle divides into two columns instead of stacking.

## Fixed layouts, cleanly composed — NOT modular

A Morrowind-style modular pane system (open several, drag to resize, arrangement
persists) was raised and then **explicitly ruled out** by the owner: *"Alright
well then dont make it modular"* — *"but clean"*.

So there is **no** player-draggable pane system, no per-pane move/resize, and no
saved arrangement. Every screen is a **fixed, deliberately composed layout**, like
every one of the CultGame reference frames — which are themselves fixed
compositions, not user-arranged workspaces.

"Clean" is the actual requirement, and it means:

- **One considered composition per screen**, designed rather than accumulated.
  Panels sit where they sit because that is the right place for them.
- **Aligned edges and consistent gutters.** Rules line up, columns share a left
  edge, key/value pairs share a value column. Misalignment is what makes a text
  interface look amateur, and it is free to get right.
- **Generous, deliberate emptiness.** The reference frames leave large bordered
  areas blank rather than cramming them. Empty space inside a frame is
  composition, not waste.
- **Stable geometry.** Panels hold their size and position as the cursor moves
  through them; nothing jumps, reflows or collapses between entries.
- **No clutter.** If an element is not earning its space, cut it rather than
  shrinking it.

### Width-awareness is still required — for resolution, not for dragging

Layouts must still adapt, because the game runs at different window sizes and
scales (`--width`, `--height`, `--scale`, and the 960x540 baseline the
real-estate ruler uses). So a rule is still drawn to its container's width, an
option list still picks its column count from the space available, and prose still
wraps to its panel.

The distinction that matters: **layouts respond to the WINDOW, never to the
player dragging a pane edge.** That removes the persistence format, the drag
handling, the minimum-size drag clamps, and the controller/keyboard resize scheme
entirely — none of that gets built.

What the first-person view keeps from the discarded idea: the world still renders
behind and around these surfaces where a screen is not a full takeover, per
*"I know we have a first person view but it can work with ASCII menus."*

## The gameplay frame — the most directly applicable reference

Two of the owner's frames show CultGame *being played* rather than being
configured, and they matter more to this project than the menus do, because they
are the shape of an in-world action surface.

```
◆~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-◆
!CRUSADERS: Jeff (11/12)                                          |
|                                                                 !
◆-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-◆-~-~-~-~-~-~-~-~-~-◆
!Jeff preaches to Frog:                        |  . . • .         !
|"I know it's a hard sell, but I worship a     !  . ♠ . f .       |
!blood deity. Would you like to know more?"    |  . . @ . .       !
|                                              !  . . . . S       |
![Preaching Check]                             |  . ♠ . ✖ . S     !
|6 + 4.559999 = 10.559999                      !  • . . . .       |
!DC: 10                                        |                  !
|                                              !                  |
![SUCCESS]   d - details   Tab - View Awareness |                  !
|                                              !                  |
!Frog is moved by the sermon!                  |                  !
|The frog has converted to Cult of the magic conch!               |
◆~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-◆
!Arrow keys - Move   p - Preach   l - Loot       c - Crusaders    |
|w - Wait            f - Flirt    L - Loot all   b - Backpack     !
!t - Throw           a - Attack                                   |
|                    i - Interact                                 !
◆~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-◆
```

Three zones: a **status header**, a **main split** of event log beside a spatial
view, and a **contextual action zone** along the bottom.

### The check is shown, and this is the biggest idea in the set

```
![Preaching Check]              <- bracketed label, magenta
|6 + 4.559999 = 10.559999       <- the actual arithmetic, yellow
!DC: 10                         <- the threshold, cyan
|
![SUCCESS]                      <- verdict, inverted green fill
```

The player is shown the check's *name*, its *arithmetic*, the *threshold* it was
measured against, and the *verdict* — every input to the outcome, in four lines.
Nothing is hidden behind a black box.

**This project should steal it outright.** Granadad is an investigation game whose
whole premise is that skills like streetwise, linkcraft and channeling change what
you can find out. A player who is never shown the check cannot tell a failed roll
from absent content — which is precisely the confusion that made the owner think
the Bloodletter case had ended at the Weighhouse.

Details worth copying exactly:

- **Degrees of outcome, same badge treatment.** `SUCCESS` and `CRITICAL SUCCESS`
  both render as inverted green fills; the critical is simply a longer badge on a
  larger margin over the DC. Reuse the selection-fill idiom as a status badge.
- **Mechanics first, then consequence in prose.** The roll resolves, *then*
  "Frog is moved by the sermon! The frog has converted..." Never mix them.
- **Contextual verbs sit beside the result they act on** — `d - details`,
  `Tab - View Awareness` — not in the global command bar, and they vary by
  situation (the critical-success frame offers only `d - details`).

### The bottom zone is contextual, not a static help bar

Normally it is the full command palette: every verb visible at once, keyed, in
aligned columns. Nothing is hidden in a submenu.

But when a decision is pending it **becomes the decision**:

```
!Will you accept Skeleton Warrior as a devotee?     <- asked in the log panel
◆~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-◆
!1 - Yes                                           |
|2 - No                                            !
!3 - View Skeleton Warrior's stats                 |
◆~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-◆
```

The question is asked where the narration lives; the answer is given where the
verbs live. And the third option is *"View Skeleton Warrior's stats"* — the player
can inspect before committing, from inside the prompt. Offer the informed choice.

### The spatial view is a panel, not the screen

The map occupies roughly a quarter of the width beside the log, and it
**highlights the current interaction target** (the `S` being preached to wears a
fill). For this project the equivalent is that the first-person view is a region
of a composed frame in these surfaces, and whatever you are interacting with
should be marked.

### What this maps to here

| CultGame | Granadad |
|---|---|
| event log panel | the dialogue / narration surface |
| `[Preaching Check]` block | any skill check — streetwise, linkcraft, channeling |
| spatial view panel | the first-person view, or the ward map |
| command palette | the core verbs, always visible |
| pending-decision zone | dialogue choices, the casebook's "go here" |
| `View X's stats` option | inspecting an actor before committing |

## The character sheet — bars, hotkeys and dense facts

A ninth frame shows the actor sheet, which maps straight onto this project's
`actor_sheet.cpp` and the creation flow's review and customize steps.

```
◆~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-◆
!Jeff  > Idle                                                     |
◆-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~◆
!HP    ████████████  11/11        Devotion  ██▒▒▒▒▒▒▒▒     10     |
|Luck      0                                                      !
!                                                                 |
|ATTRIBUTES                       SKILLS                          !
!1 Strength       8  ███▒▒▒▒▒▒    5 Preaching       1.6 █▒▒▒▒▒▒   |
|2 Agility       11  ████▒▒▒▒▒    6 Criminal        2   ▒▒▒▒▒▒▒   !
!3 Intelligence   7  ██▒▒▒▒▒▒▒    7 Flirting        0   ▒▒▒▒▒▒▒   |
|4 Charisma      14  █████▒▒▒▒    8 Ritual Casting  5.5 ██▒▒▒▒▒   !
!                                                                 |
|EQUIPMENT                        DETAILS                         !
!w - Weapon:   ? cane  1-6 Impact  v - View traits (Con Artist +1) |
|a - Armor:    i business casual  0.5 DR    Race: Human           !
!t - Trinket:  no trinket                                         |
|                                           Languages: Common     !
◆~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~Cult of the magic conch (1/1)◆
!r - Rename                                                       |
|0 - Back                                                         !
◆~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-◆
```

Technique worth taking wholesale:

- **Bars have a stippled remainder track, never an empty gap.** The filled part
  is a solid block run in the stat's own colour; the unfilled part is a dotted
  texture. Same instinct as the stippled art grounds — emptiness is textured, not
  blank. The numeric value sits beside the bar, so you get shape and exact figure
  together.
- **Every stat carries its own colour** — Strength red, Agility green,
  Intelligence cyan, Charisma magenta — so the sheet is scannable by hue before
  you read a word.
- **Numbering runs CONTINUOUSLY across the two columns.** Attributes are 1-4 on
  the left, skills are 5-8 on the right: one unbroken hotkey sequence over a
  two-column layout. The columns are a *visual* arrangement; the selection model
  underneath stays a single flat list. Copy this — it is what keeps a dense
  two-column sheet keyboard-drivable without a cursor that has to understand
  columns.
- **Letter mnemonics where numbers would be arbitrary** — `w` weapon, `a` armor,
  `t` trinket, `v` view traits, `r` rename. Numbers for enumerable rows, letters
  for named slots and verbs.
- **A value is shown with its mechanical meaning inline** — `? cane 1-6 Impact`,
  `i business casual 0.5 DR`. Never a bare item name the player has to go and
  look up.
- **Sub-views advertise what is inside them** — `v - View traits (Con Artist +1)`
  tells you the payoff before you press it. A door with a label on it.
- **Empty states are worded, not blank** — `no trinket`, `Luck 0`. Absence is
  stated so the reader knows it was considered.
- **Text can ride the horizontal rule.** The affiliation and count
  (`Cult of the magic conch (1/1)`) sit right-aligned *in* the divider between the
  body and the footer, rather than consuming their own row. A cheap way to place a
  persistent fact without spending a line on it.
- **The header is subject then status** — `Jeff > Idle`, the state in its own
  colour. What this is, and what it is currently doing.

### Where this lands here

`actor_sheet.cpp` is the obvious target, but the same grammar answers the creation
flow's review step and the quiz's consequence problem: attributes and skills as
coloured bars with their numbers, so a player watching a quiz answer move
STREETWISE can *see* the bar move. That is a far better answer to "show the
consequence" than a line of text reporting a delta.

## Colour discipline

| role | colour |
|---|---|
| ground | near-black / very dark navy |
| prose | white / light grey |
| keys, actions, verbs | yellow |
| mechanical benefit, numbers | green |
| entity accents | per-subject (fire red, water blue, earth green) |
| current selection | inverted — accent fill, dark knocked-out text |

## What to carry into this project

- The `+~-~-` rules and the alternating `|`/`!` edges, everywhere.
- A breadcrumb or instruction header on every panel.
- Numbered direct-select options, in columns, nav verbs in the same list.
- Inverted-fill selection, never an arrow.
- Flavour → named effect → number, colour-sorted.
- Textured panel grounds rather than empty black.
- For anything spatial: cursor on the map, selection named below it, tabbed
  views over the selection, aligned key/value details.

## What NOT to carry

- The typeface. Ours is already loved; this is about layout and structure.
- The literal colours where they fight the ward's established palette — the art
  register is DF-translated Kenney on true black (DECISIONS.md, Art register
  row), and these panels sit over a first-person view, not over a tile grid.
  Take the *discipline* (one colour per role) rather than the exact hues.
- Full-screen takeover for things that should be glanceable. The reference is a
  menu-driven 4X where full-screen is always right; we have a first-person game
  where the crosshair prompt and the alert row must stay light. The
  real-estate ruler in `docs/HUD-REAL-ESTATE.md` still governs.
