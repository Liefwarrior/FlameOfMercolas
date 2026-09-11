# The justice build, photographed

Every frame in here is the real `dist\granadad.exe` playing the court through the real verbs
on the gated Windows build. Nothing is staged and nothing is a harness. Each run prints a
summary line beside the picture (`court beats=n/N ... judgment=... row="..."`), and the exact
line that shot every frame is in `shoot-court.log`. A frame that fell short can't pass as one
that did.

Eli, 2026-09-02. *"If the player is tagged as a criminal it should be like Daggerfall where
you can go to court and you can face jail or execution (game over)."*

Shoot the whole set again with `powershell -ExecutionPolicy Bypass -File .\scripts\shoot-court.ps1`.
Every line below is `granadad.exe --smoke=0 --hold --width=960 --height=540 --scale=1 <the line> --screenshot=...`.

## The frames

| frame | line | what it shows |
| --- | --- | --- |
| `court-wanted-960x540.png` | `--court=wanted` | TAGGED. Eight lifts in Watchman Cull's sight in the Gull at eleven. `WANTED  HEAT 64` on the HUD in the tag's red, a witness's own row on the alert. Paper out, nobody's hand on you yet. |
| `court-cull-960x540.png` | `--court=cull --settle-steps=0` | THE ARREST, BEAT ONE. Steel up in Cull's sight, taken at reach, and his own line on the row with his hand on you. `Watchman Cull: There is paper out on you and I am the man holding it. Walk.` The room is still around you and the crosshair promises nothing. Held two and a half seconds before the cut. |
| `court-taken-960x540.png` | `--court=taken --settle-steps=0` | THE ARREST, BEAT TWO. The cut. Black, one line in the rope plate's own register, `TAKEN TO THE MISSION. 23:00.` Held before the page opens. Pinned byte for byte on the rendered frame in `test_hearing_page.cpp`. |
| `court-page-960x540.png` | `--court` | THE HEARING. The body at the Mission's door and the page up. The officer lays the paper, the charge off the sheet (`EIGHT LIFTS`), what the paper asks, the three rows, the priest's opening for a thief the Mission has never seen (`court.paper`), Cull by the wall under the rows (`court.taken`). |
| `court-paper-960x540.png` | `--court=paper` | HEAR THE PAPER. The sheet as phrases, never numbers, before the plea. `THE MISSION DOES NOT KNOW YOU.` matches the opening beside it. |
| `court-armed-960x540.png` | `--court=armed` | I DID IT pressed once. The row lit with `-- SURE?` and the confirm key on its tail, `ESC` live in the nav band, the priest pressing for the answer in place of his opening (`court.plead`). |
| `court-plea-960x540.png` | `--court=plea` | I DID IT weighed. `24 THE FLAME - 20 THE WARD - 1 HEAT MAKES 3`, `+ 6 CONFESSED MAKES 9`, `THE LINES: 38 FINED  14 HELD` (no SPARED on a confession, it was never on the table), BOUND, `BONDSWORN 5 DAYS.`, the priest's word (`court.bound`), the one row `SERVE IT.` The officer's line is gone from a judged page. |
| `court-deny-960x540.png` | `--court=deny` | I DID NOT weighed. `- 2 THE PRIEST IS A MAN MAKES 1`, the lines with SPARED on them, BOUND doubled, `BONDSWORN 10 DAYS.`, the lie in the priest's mouth (`court.lie`). |
| `court-hand-960x540.png` | `--court=hand` | THE HAND. The Skyrunners' oath off Finch first, then the same eight lifts, so the paper asks for the hand. `- 12 THE ROOFS`, `[THE HAND]` in the tag's red, `THE HAND. BONDSWORN 5 DAYS.`, `court.hand`. Real play, no ledger poked. |
| `court-flame-plea-960x540.png` | `--flame --court=plea` | What you gave at the door is the only coin the Flame reads. The flame line first (temple standing 100), the court waits for Cull through the wait page, `+ 24 THE DOOR` in the block, HELD, `TWO NIGHTS. 16 ROYALS.` Every term whole on its row. |
| `court-flame-deny-960x540.png` | `--flame --court=deny` | The same sheet denied and disbelieved. `- 9 THE PRIEST IS A MAN MAKES 18`, HELD doubled, `FOUR NIGHTS. 32 ROYALS.`, `court.lie`. |
| `court-serve-960x540.png` | `--court=serve --settle-steps=30` | JAIL SERVED, THE WORLD MOVED. BOUND taken by its row. `TURNED LOOSE ON THE TARWALK. DAY 6. 23:00.` Five days on the world clock from day 1, the fine forgiven (40 -> 40 Royals), the paper off, `HEAT 12` and no tag on the HUD. |
| `court-flame-serve-960x540.png` | `--flame --court=serve --settle-steps=30` | HELD served. The fine paid (34 -> 18 Royals), two nights on the clock, `TURNED LOOSE ON THE TARWALK. DAY 3. 23:00.` Two nights promised, two days on the calendar, the same clock face. |
| `court-bloodtag-960x540.png` | `--court=bloodtag` | WANTED FOR BLOOD. A killing at eight before the Watch drinks (Tarn Wrenhale, two saw it), the tag on the HUD in the tag's red. Nobody's hand on you yet. |
| `court-ropepage-960x540.png` | `--court=ropepage` | THE ROPE HEARING. The blood reading (`THE WARD SAYS YOU PUT TARN WRENHALE DOWN IN THE GILDED GULL. TWO SAW IT.`, `THE PAPER ASKS FOR THE ROPE.`), `- 30 BLOOD - 8 TWO SAW IT`, `THE LINE: 24 MERCY`, `[THE ROPE]` in the tag's red, `THE ROPE.`, `court.rope`, the one row `1 THE DROP.` offered and not taken. |
| `court-rope-960x540.png` | `--court=rope` | EXECUTED. The drop taken by the player's own row. `HANGED AT THE SALTGATE POST. / BY THE WARD. FOR TARN WRENHALE. / THE FIRST DAY. 22:00.` with `1 - A NEW MAN / 2 - LEAVE` under the plate. No save row, no save exists. |
| `court-newman-960x540.png` | `--court=newman` | A NEW MAN armed by its row, `1 - A NEW MAN -- SURE?` with the confirm key on its tail, then taken. `armed=new-man end=new-man` in the summary. `runEnded()` with `RunEndChoice::NewMan` and no quit, the answer `main()` loops on. The row stays lit once taken, so this frame is not the rope plate. |
| `court-newman-creation-960x540.png` | `--creation` | The creation window `main()` opens next. `run_creation_window` and this capture draw the same `CreationFlow` on its origin step. A fresh run starts with a clean ledger (asserted in `test_hearing_page.cpp`). |

## The two paths, kept apart

A combat defeat is the nemesis ruling and you live. `--nemesis` on the same build still lands
7/7, the quay revive under it. THE ROPE is the one place the player ends, and it is reached only
through the bench. `test_court.cpp` asserts both directions (a served sentence never touches the
NemesisBook, a defeat after a sentence still rises and revives, the rope alone sets `executed_`).

## What the drive can't reach, said plainly

The smoke's man is a nobody. Eight witnessed lifts put paper on him and take the ward's opinion
to its floor (`- 20 THE WARD`), and a killing in a lit room does the same. Four states need a
warmer ward than any real verb sequence gives him in one evening, so they are proved in the
suite and not on a frame.

- **SPARED.** Needs 55 on a denial. With `--flame` the sheet reaches 27 and the band tops out
  at +10. Proved in `test_court.cpp` (the band's top spares, the thief's own SPARED) and
  `test_hearing_page.cpp`.
- **COMMUTED.** Needs 24 on the rope tier. The killing costs `- 30 BLOOD - 8 TWO SAW IT` and the
  witnesses take the ward to `- 18`, so even `+ 24 THE DOOR` lands at `- 2`. Proved in
  `test_hearing_page.cpp` ("commutes the devout", the Shepherd's own sheet) and `test_court.cpp`.
- **CONDEMNED on the HUD.** Needs a commutation first. Proved in `test_hearing_page.cpp` (the tag
  test draws all three tags on the frame's corner).
- **I HAVE NOTHING TO SAY (mercy once).** Needs a commuted man taken for blood a second time.
  Proved in `test_hearing_page.cpp` ("mercy is given once on the page"). That page draws no
  `[THE PRIEST WEIGHS]` badge and no lines, and the suite pins it byte for byte.
- **A pure FINE.** Needs 38 on the same nobody. HELD still charges the fine (34 -> 18 Royals on
  `court-flame-serve`). FINED itself, `PAY IT.` and `TURNED LOOSE AT THE MISSION'S DOOR`, is
  proved in `test_hearing_page.cpp` and `test_court.cpp`.
- **The creation window after A NEW MAN** is photographed by `--creation`, not by a windowed
  loop. The headless line ends where `main()`'s loop begins. The loop is fifteen lines and is
  read, not driven.

## Every court row, for the redline

Sixteen `court.*` tables in `content/raws/barks/contract_barks.json`. The openings are keyed
by the case and rotate only inside the case, so every row of a table is true of every hearing
that table can open. No row names a gift, a rope, a corpse or a night the sheet may not carry.
The learned word is never said to a layman, not even nearly.

**court.taken** (the officer walking you in, under the rows)
- Through the door and sit where he points. The priest asks the questions in here. I only carry the paper.
- Mind the step. Men have gone down it in irons and come out with a fine; men have gone down it free and come out for the post.
- I lay it on the table and I stand by the wall. What he does with it is between you and the lamp.
- You will call him Father and you will not lie to him. The second one is not my rule, but I have watched it enforced.

**court.paper** (a thief the Mission has never seen)
- The Watch brings me paper and asks for a cell. I ask the man. Speak.
- Sit. The Flame has heard worse than a thief tonight and will hear worse tomorrow.
- I have never seen your face at this door. The paper is all I have of you. Give me more.

**court.paper.door** (a thief who has given at the door, THE DOOR above zero)
- I have read what you gave at this door. It is the only reason we are talking.
- I know your face from the alms bowl. I did not think I would see it across this table. Speak.
- You have given at this door. The Flame weighs that; the Watch does not. Tell me what you did.

**court.blood** (a killing)
- A dead man on the roster and your name beside his. The Flame hands no man to the post unheard. Speak.
- A man on the Gull's floor will not get up. The Watch wants your neck for it. I want the truth first.
- The lamp does not go out because you put a man under it. It goes out when I stop asking. Speak.

**court.roofs.hand** (a Skyrunner's first, the ask is the hand)
- The roofs, and the Watch wants the hand. I have overruled a sergeant before. Give me a reason.
- You climbed. Half this quay has climbed. The half that was not caught is not sitting here. Speak.
- A first time on the roofs and the sergeant wants a hand for it. Whether he gets it is mine to say.

**court.roofs.rope** (the second rung, the ask is the rope, no corpse)
- The rope is for the second rung and you are standing on it. I will hear you anyway.
- Twice on the roofs with paper out. The ward has one answer for twice. I am the last man who can give another.
- The sergeant wants the post and the rule is on his side. Speak well. There is nothing else on yours.

**court.nothing** (a commuted man back for the rope, mercy once)
- I gave you mercy once and told you it was once. I do not have a second.
- You will say nothing, because there is nothing left to say. The ward may have you.
- Mercy was a bargain and the Flame kept its side. You were told what a second time costs. It costs this.

**court.plead** (the priest pressing once a plea is armed)
- Well? The lamp is lit and I am an old man. Did you, or did you not?
- Say it plainly. The Flame has no ear for a long story and neither have I, at this hour.
- That is your answer, then. Give it to me once, out loud, and it is yours for good.
- I will not ask twice. The Watch would; the Watch is paid by the hour.

**court.spared**
- Go. The paper is torn and the ward will not remember why. I will.
- I believe you, which the Watch does not. Walk out of my door before the sergeant finds his tongue.
- Spared. Do not thank me; I did not do it for you. Go, and keep out of the sergeant's way.

**court.fined**
- You will pay the ward for its evening and you will pay me nothing. Go.
- A fine, and no cell. The Flame does not want your nights; the ward wants your coin.
- Pay it and be gone. If the purse is short, the yard will take the rest in days.

**court.held**
- A cell, and the nights the ward asks for. Sleep in it. Most men do.
- The Watch has its cell. I have your name, and I will keep it longer than they keep you.
- Held. When you come out the paper will be gone and the heat will not. That is the ward, not me.

**court.bound**
- No fine. The Flame has better use for you than a purse. Five days in my yard, carrying the pot.
- Bondsworn to this Mission. You will be fed, preached at, and worked. Two of those are a kindness.
- The yard. Nobody has ever died of it, though several have asked to.

**court.hand**
- The hand. The rule is the hand, and I will not pretend the sergeant is wrong. It is done quickly.
- A Skyrunner's first. The ward takes the hand so it need not take the rest. Hold it out.
- I would spare it if the weighing let me. It does not, and the scale is not mine to bend.

**court.commuted**
- The rope does not un-take the hand. The hand, twelve days in my yard, and your life. Remember it.
- The Flame does not want you dead. The ward may still. Twelve days, and you walk out of here marked.
- Commuted. Once. There is no word in this Mission for twice.

**court.rope**
- The Flame does not want you. The ward may have you.
- I have nothing to weigh you against. Go with the sergeant.
- It is the rope. I will say the words at the post; I will not say them here.

**court.lie** (a denial disbelieved, any shape of sentence)
- You lied to the Flame's face. The Mission will remember the lie longer than the sentence.
- Denied, and disbelieved. The sentence doubles and so does the memory of it.
- You told me you did not, and the lamp went dim as you said it. Twice what it would have been.

The page's own fixed literals (not spoken, the consequence pane). Cell and hand tiers, I DID IT:
`A CONFESSION IS WEIGHED AS IT IS GIVEN. THE FLAME'S ANSWER IS FIXED BEFORE YOU SPEAK IT. NEVER
DOUBLED, AND NEVER SPARED.` Rope tier, I DID IT: the same up to `MERCY OR THE ROPE, AND NOTHING
ELSE.` Rope tier, I DID NOT: `A DENIAL IS WEIGHED WITH THE PRIEST'S OWN DOUBT IN IT. TEN POINTS
EITHER WAY. MERCY OR THE ROPE, AND NEVER SPARED.` No rope is named on a charge that has none on
the table.

## Baselines on this exe

`granadad-twin-gate --tavern --ticks 900`: `0x837E94019BC49C25` (run A == run B). Unmoved by
the fix pass. The one declared move of the justice build, `0x86E05F527E54E795` ->
`0x837E94019BC49C25`, stands as re-blessed in DECISIONS.md and BASELINE-WORLD-HASH.md.
`granadad-twin-gate --population --population-hour 16 --ticks 7200`: `0x2646C1AAA2BA38DF`
(run A == run B, 18,772 bytes). Unmoved.
