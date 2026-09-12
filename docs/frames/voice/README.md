# The street talks

Every frame in here is the real `dist\granadad.exe` on the gated Windows build (branch
`lane/voice`, commit `36411ac4`), playing the voice channel through the real verbs. Nothing
is staged. Each run prints a summary line next to the picture
(`voice end=... beats=n/N ... key=... speaker="..." row="..."`) and the exact line that shot
each frame is in the table. A run that fell short says so and can't pass as one that didn't.

You said it on the 10th. "I'd expect the watch and crowd to react appropriately to the violence
they're witnessing." And the Oblivion thing where talking to somebody feels like talking to a
person. They speak first, they say goodbye, they turn to you, they react in words. Before this
nobody in the Docks said a word unless you pressed E, and LEAVE closed the panel on nothing.

Every line below is `dist\granadad.exe --smoke=0 --hold --width=960 --height=540 --scale=1 --music-off --settle-steps=0 <the line> --screenshot=docs\frames\voice\<frame>`.
`--settle-steps=0` matters. A spoken line holds four to nine seconds and the default four second
rest would photograph it fading out. Run them from the repo root.

The nine `*-640.png` frames already in this folder are the older copy pass (the UI's own voice,
commit `fdbd177b`). They aren't part of this set and they weren't reshot.

## Frames

| frame | line | what it shows |
| --- | --- | --- |
| `voice-pass-960x540.png` | `--voice=pass` | A serf walks by on the Tarwalk at four in the afternoon. The smoke's own walk west from the spawn until somebody passes within talk reach, then the walk turns to look at her. `PETRA BARNACRE: CRANE LINE IS MOVING. STAND CLEAR OF IT.` sits on the voice row above the health bar, her name in the body accent and the row in dim ink, on one plate. Nobody pressed E. The crosshair still names her (`PETRA BARNACRE  SERF  E - TALK`) so the line and the prompt agree on who spoke. |
| `voice-overheard-960x540.png` | `--voice=overheard` | Two bodies talking to each other, overheard from the street. Held at the spawn, then walked, until two people standing together within six tiles and a sight line get heard, and the walk turns to look at them. Three serfs by the Eel-Pots, and one side of the exchange in the speaker's own trade. `QUENNA GROYNE: ...BONDSWORN BY THE SPRING IF THE LANES STAY SHUT. MARK ME...` (`overheard.serf`). |
| `voice-farewell-960x540.png` | `--voice=farewell` | The goodbye. The `--street` placement beside a rope hand, the talk key, then LEAVE by its own topic. The panel closes and `LISBET HEMPSON: GO ON. THE CRANE LINE WILL NOT WAIT FOR EITHER OF US.` is on the voice row (`farewell.serf`). Her greeting has stood down off the alert row, so the goodbye is the last thing she said. |
| `voice-moved-960x540.png` | `--voice=moved` | The worded reaction. Same placement, then THE VANISHED CLERK heard out, which warms her a hair. The panel's dateline row reads `(THE FACE SOFTENS A HAIR)` under her reply, dim, for four seconds. Never a number. She holds still and faces you for the talk. |
| `voice-react-960x540.png` | `--voice=react` | A witness to a tap in the Gull at eight. Walk to the bar, tap the nearest patron until it connects (the `--punch` beat), and the room says what it saw. The one you hit joins the fight on the alert row in his own words (`brawl.join`, from the feel build). A patron who saw it (the sim's three clause witness rule put `Struck` on his memory) speaks on the voice row in his own trade's mouth. `CAPTAIN IVO WAKE: KNIVES CHECKED AT THE DOOR, AND STILL THIS.` (`react.maritime.struck`). The shot turns to the witness. Bouncers never take this row, their ladder speaks for them. |
| `voice-watch-960x540.png` | `--voice=watch` | The Watch approach readback. The feel build's `--watch-halt=halt` line up to the halt (steel up in Cull's sight, a swing at a patron, Cull Closing on VIOLENCE with `watch.halt` on the alert row), then his next word on the voice row a breath later as he crosses the room. `WATCHMAN CULL: PUT YOUR HANDS DOWN. DOWN. I WILL NOT SAY IT A THIRD TIME.` (`watch.approach.violence`). The halt is still up under it. It's the halt, extended. |

## The channel

One emitter on the render side (`native/src/render/street_voice.cpp`). `Session::step` steps it
once per movement step after the room and the ward have moved, and it draws on one HUD row
through its own `EasedToggle`. It reads state and writes none. No draw, no sim field, no hash
contact. The room's and the ward's digests are byte for byte what they were before it ran
(`test_street_voice.cpp` pins it). Tavern twin `0x837E94019BC49C25` and population
`0x2646C1AAA2BA38DF` are unmoved, twice each, off the gated exe.

Five channels, ranked. A higher one replaces a lower one on the row. A lower one waits.

| channel | what fires it | keys | cooldown |
| --- | --- | --- | --- |
| watch | a watchman's eyes on you crouched or with your hands up, a watchman passing somebody the post has heard of (heat 24 and up) or has paper on, the deference table to a presented Wielder, and the readback as a Closing watchman crosses the room after his halt | `watch.notice` `watch.heat` `watch.paper` `watch.wielder` `watch.approach.<violence\|paper\|goods>` | 5 s on the channel, 20 s per watchman. The readback fires 45 steps after the halt and again at reach |
| react | a witness's memory moving with Struck, DrewSteel or Slew on it (`SocialLedger::witness`, read by comparing public state, same shape as `lastPlayerHp_`). Never the man in the fight, never a bouncer (his ladder speaks for him), never the Closing watchman (his halt does) | `react.<family>.<struck\|steel\|slew>` | 4 s on the channel, 15 s per witness. A witness who couldn't get the row keeps the right for 4 s |
| farewell | LEAVE by its topic, the back key, or walking off past five tiles mid talk | `farewell.<type>.<attitude>` and `farewell.away.<family>` | none, it's an event |
| pass | the nearest body within two tiles that hasn't spoken to you lately, once | `pass.<type>.<attitude>` | 12 s on the channel, 2 min per mouth |
| overheard | two bodies standing together within six tiles and a sight line. The room when you're inside, the street when you're out | `overheard.<family>` | 24 s on the channel, 90 s per mouth |

Ambient lines (pass, overheard) also wait eight seconds past the end of any event line. A line
holds four to nine seconds depending on its length. The street is never a wall of text.

Selection is `BarkTables::line` off `(actor id, step, talks)` with a ten second bucket of the
step. No RNG anywhere near it. The chain tries the trade's own cell above the family's, every
attitude rung above every bare one, then a floor every prefix authors. Kin borrows friend then
warm, hostile borrows cold, and a cold man never borrows a warm row. A stranger's attitude is the
ward's opinion of you, so somebody the district wants gone gets passed coldly by people who never
met him.

## The address

- Hold for the talk. The Gull actor you're talking to stands where the talk found him and keeps
  his face to you (`Tavern::holdsForTalk`, applied in `stepMovement`). Only the peaceable
  activities hold. A bouncer mid warning, a man in the fight and a Closing watchman have somewhere
  to be. It's derived from `talkingToId_`, no new field. The WardActor hold is deferred and
  declared. A street body standing still for two hundred steps changes every later tile decision,
  and that's a population baseline move for the STREET SENSES lane, not this one.
- Walk off close. Get five tiles from the person you're talking to and the talk ends with the word
  thrown after you (`farewell.away`). Checked every step while the director is open. If it was
  the body that walked (a street patrol keeps its beat, the ward hold being deferred) it ends the
  talk with its ordinary goodbye instead, said as it goes.
- The goodbye. LEAVE carries it in its own reply now. The back key says one too.
- The worded reaction. A reply that moved their disposition puts a stage direction on the panel's
  dateline row for four seconds. `(THE FACE SOFTENS A HAIR)`, `(A NOD. THAT WENT DOWN WELL)`,
  `(THAT LANDED BADLY)`, `(THE SHOULDER TURNS)`, and when a band gets crossed `(THAT WENT DOWN
  WELL. WARM NOW)`. Never a number.
- The Watch approach readback. After the halt, one line a breath later and one at reach, keyed by
  the cause the room recorded.

## The honest bound

Your swing only reaches the Gilded Gull's roster today. WardActor combat sheets are deferred by
ruling (COMBAT-ACTION-SPEC.md section 10), so a street body has no blow to witness. `react.*` is
proven on the Gull's witnesses and the street half is declared. The same tables and the same
emitter fire the day a street body can be hit, which is the STREET SENSES lane's declared
population move. Two more things aren't built here, said plainly. `watch.notice`, `watch.heat` and
`watch.paper` are a street watchman's word as you pass, and nothing follows them on the street
because the street Watch has no arrest by ruling. `rumor.<family>` stocks WHAT THE WARD SAYS in a
trade's own mouth, but no hashed event ring feeds it yet (roadmap item 17), so it's authored
breadth, not the world's news. And a third, the critic's own catch: `overheard`'s street bodies
share `paletteOf(sim::WardType)` (`native/src/render/session.cpp`), which colours a coat, trousers
and skin by TYPE and not by the actor -- three serfs standing together are three copies of the
same serf. There is no per-body knob to turn; fixing it means threading an actor id through
`paletteOf` and every render3d call site that reads it, plus authoring the colour variants
themselves, which is a wider change than a camera-and-placement pass should carry. Declared here
rather than patched around.

Two more, found by this same critic's round and tried, not fixed. Said plainly rather than
papered over: **the `watch` showcase's reveal shot.** `voice-watch-960x540.png`'s own line lands
clean (four beats, the right key, the right words), but the camera turning to Cull as he crosses
the room can end up looking into a wall-mounted shelf (`render3d/static_pieces.cpp`'s hash-placed
decor, not in the tile-solidity grid `TileQuery::lineOfSight` reads) instead of at him. A tile-LOS
wait was tried, then a measured bearing-arc wait in its place; both broke the pinned "ONE MAN, ONE
LINE" invariant (`test_street_voice.cpp`'s watch case) instead of fixing the shot -- any extra idle
time between the voice-row event and the turn gives the street a chance to speak through `say()`
again before the halt's own alert clock this test pins ever reaches zero, confirmed empirically (a
fresh bark, then a bystander's own `react.*` line, both stepping on state this branch doesn't own)
rather than assumed. Reverted to firing the turn on the voice-row event alone, which is what the
suite already holds correct; the reveal shot's own framing stays open for a pass that can afford to
touch more of the Watch's state machine than a render-side wait can reach.

**Near-camera clipping at the outdoor "rope hand" placement.** `voice-farewell`, `voice-moved`, and
(less severely) `voice-pass` all place the camera on a working stretch of the Tarwalk where dockers
walk close together -- a second body's head can pass close enough to the lens to fill the bottom of
the frame. A two-pass crowding filter at PLACEMENT time (`standTileIsCrowded`, rejects a stand tile
with another visible body within a tile of it) and a re-aim at the target's CURRENT position right
before the shot (instead of the stale aim taken at placement, which could be tens of steps stale by
the time farewell/moved's own beats land) were both tried. Neither closed it: a fresh capture after
both fixes still shows the same clipped head in the same frames. This reads as a structural
property of a genuinely busy street corner, not a placement bug this pass's tools can isolate --
`voice-pass` shows a milder version of the same thing without ever having been flagged, which
suggests the corridor itself, not any one body's position, is what wants a wider fix (a different
stand tile entirely, or a minimum-radius push against ALL bodies rather than a two-pass filter that
only checks the moment of placement). Declared here rather than patched around a second time.

## What the suite pins

`native/tests/test_street_voice.cpp`. Every row draws, fits beside a name at 960x540 and has no
dash in it. The learned word is in one priest's mouth and nobody else's (grepped). The chain falls
the right way. The pick is pure and the emitter moves neither hash. LEAVE carries a goodbye out of
a farewell table, the back key says one, and walking off throws one after you. The one you talk
to in the Gull holds still. The worded reaction is never a number. Two pass lines are never closer
than the cooldown. All six `--voice=` endings land every beat they owe. A hand built HudState
without the row draws pixel identical. `test_dialogue.cpp` pins the sheet at 521 tables (393
before this lane, plus the 128 listed below).


## Every row, for the redline

Four new files under `content/raws/barks/`. Your `barks.json` is untouched and still wins every duplicate key. Rows are listed verbatim. Redline anything.

### pass, farewell, overheard, react

`content/raws/barks/voice_barks.json`. 112 tables, 323 rows. `pass` is the one line as a body passes within reach on the street; `pass.room.<family>.<attitude>` is the same line from a stool in the Gilded Gull, so nothing about crane lines, wet planks, carts or shutters is said indoors. `farewell` is the goodbye and `farewell.away` the word thrown after you when you walk off. `overheard` is one side of an exchange between two bodies. `react` is a witness's word on a landed blow (struck), a drawn blade (steel) and a killing (slew).

**pass**
- Mind yourself.
- Wet planks. Watch your feet.
- Move along. You're standing where the ropes run.
- (a nod, no more)

**pass.serf**
- Rope coming through. Mind your feet.
- Long shift. Longer tide.
- Crane line's moving. Stand clear of it.
- Quarter-day's Kingsday. Don't remind me.
- Planks are wet. Everybody says so and somebody still goes in.
- You've the walk of a man who's never carried a crate. Lucky you.

**pass.serf.cold**
- Not you again.
- Walk on. I've nothing for you.
- (a look, and a wider berth)

**pass.serf.hostile**
- Keep walking or I call the lads.
- The ropes remember faces. Yours is one.
- Cross me on the quay and the tide can have you.

**pass.serf.warm**
- There goes a friend of the ropes.
- Still upright. Good. Ale later, if the wage lands.
- Wind's fair and so's your name today.

**pass.serf.friend**
- You owe me a drink and I mean to collect.
- Mind the crates, friend. They don't know you.

**pass.trade**
- Stock's as fresh as the tide brings it. Which isn't very.
- Buying, or blocking my light?
- Mind the shutters. They bite.
- Nothing off a Mercian bottom this quarter. Nothing off any bottom.

**pass.trade.cold**
- My counter, my business. Yours is elsewhere.
- I count the stock after you pass. Take it as counted.
- (the ledger closes as you pass)

**pass.trade.hostile**
- Out of my doorway.
- I know what walked out of here last time. Keep walking.
- The Watch has your face from me. Remember that.

**pass.trade.warm**
- Good day to a good customer.
- You paid what I asked once. I've not forgotten it.
- Mind the step, friend. I'd hate to lose you.

**pass.maritime**
- Fair wind, landsman.
- Tide waits on nobody. Walk quicker.
- Mind the lines. A mooring rope's broken better legs than yours.
- No lane open past the point. Ask any man with salt on him.

**pass.maritime.cold**
- Landsman. Keep to the land.
- Out of the way of working men.
- (a spit over the side, and a look)

**pass.maritime.hostile**
- The sea keeps what the ward won't. Remember that on a pier.
- Walk the strand alone some night. Go on.

**pass.maritime.warm**
- Fair wind, friend. If any of us ever gets one again.
- You'd make a sailor. A poor one, but a sailor.
- First cup at the Bilge is yours if the boats ever sail.

**pass.watch**
- I've seen you twice today. Three and I ask your name.
- Keep to the wall side. The carts don't stop for anybody.
- Nothing to report, and let's keep it that way.
- Carts have the Rise. You have the gutter. Walk it.

**pass.watch.cold**
- I know that face. Keep it moving.
- Every step in my ward, I count it.
- The post has your name written down. Don't add to the page.

**pass.watch.hostile**
- Give me a reason. Any reason at all.
- One shout and you sleep in the garrison. Walk.
- I'm watching you in daylight. Think about the dark.

**pass.watch.warm**
- Good day. That's not a thing I say to many.
- Keep my rounds boring and I'll keep them yours.
- A quiet street is a friend's doing. Walk on.

**pass.wastrel**
- Spare a Royal? No? Spare a look, then.
- Coin in your walk. None in mine. Spare the difference?
- Mind your purse. Not me. Somebody else. Mostly.
- Nothing to sell and nothing to say. Walk on.
- (the hand goes out, and comes back empty)

**pass.wastrel.cold**
- Not to you. Not even for a Royal.
- The Gullet's heard about you. Walk wide of it.
- (the face turns to the wall)

**pass.wastrel.hostile**
- Come down the Gullet after dark. We'll talk properly.
- The fog knows your name now.
- Keep walking. Faster than that.

**pass.wastrel.warm**
- There's the one who stood us a crust. Flame keep you.
- A friend of the strand. Say it loud so the Watch hears.
- Half a herring in my sleeve, and half of it's yours if you ask.

**pass.clergy**
- The Flame keep you, whoever you are.
- The Mission feeds at dusk. Tell whoever's hungry.
- Every soul a wick. Mind yours in this wind.
- Peace on the quay, child. What there is of it.

**pass.clergy.cold**
- The Flame sees what the Watch doesn't. Walk on.
- The door's open to you. My ear isn't, today.
- (a blessing, made quickly, and not for you)

**pass.clergy.hostile**
- The Flame keeps its own accounts. Yours is long.
- Not in the Mission's sight. Whatever you're about, not here.

**pass.clergy.warm**
- Warmth of the Flame on you. You've earned a little.
- The bunkroom speaks well of you. That's rarer than gold here.
- Peace, friend. The soup's thin tonight but the bowl's yours.

**pass.husbandry**
- Mind the beasts. They mind you.
- The dog likes you. That's not a compliment to either of you.
- Ware the goats. They butt what they don't know.
- Step wide of the pen. She kicks.

**pass.husbandry.cold**
- The dog doesn't like you. I trust the dog.
- Keep your hands off the animals and off my yard.

**pass.husbandry.warm**
- The dog remembers you. So do I. Both fondly.
- Come by the yard. There's always a pup nobody wants.

**pass.urchin**
- Got a crust? Got a Royal? Got a name?
- Mind the gulls. They take bread and fingers.
- I saw a hand in your pocket. Not mine. Mine's here.
- (a small shape, gone before you look)

**pass.thief**
- Nothing in my hands. Look.
- Fine weather for walking. Fine weather for not seeing things.
- (a glance at your belt, and away)

**pass.fisher**
- Nets up. Nothing in them a man would eat.
- Strand's wide at this tide. Don't trust it.
- The sea gave back a boot this morning. Only the boot.

**pass.sailor**
- No sailing. No lanes. No pay. Ask me anything else.
- Hammock at the Rows, if a body hasn't died in it.
- The Kestrel's the last hull with a crew. The rest are firewood.

**pass.carter**
- Cart coming through. Feet or wheels, your pick.
- Don't touch the dray. Hold her with your eyes.
- Round again. Weighhouse to the Bond and back till dark.

**pass.disciple**
- The Flame keep you. Have you eaten?
- Father Maell says to tell the hungry. You look hungry.
- I am not to stare at the sea. I am staring at the sea.

**pass.room**
- Sit or stand, but do it somewhere else.
- You're in my light.
- (a nod over the cup)

**pass.room.serf**
- Sit down or get out of the lamp.
- Room at this table if you're buying.
- Don't lean on me. I've done my leaning for today.
- You've a look of the Watch about you. Tell me I'm wrong.

**pass.room.serf.cold**
- Not this table.
- (the cup moves to the other hand, the far one)

**pass.room.serf.warm**
- Here's the one who stood us a round. Sit.
- Pull up a stool. I'll shout you the second.

**pass.room.trade**
- Cup's empty. So's your side of the bargain.
- You're at the wrong end of the bar for standing.
- Drink, or don't, but don't hover.

**pass.room.trade.cold**
- I pour for paying men. Are you one?
- (the good bottle goes under the counter)

**pass.room.trade.warm**
- There he is. The first one's on the house, and don't tell Venn.
- Sit at the bar end. I'll keep it topped.

**pass.room.maritime**
- Room at the table for a landsman, if the landsman pays.
- Don't ask about the lanes. Nobody at this table has one.
- Sit if you're sitting. Standing men make the captain nervous.

**pass.room.watch**
- Off my post and on a stool. Give me one quiet hour.
- I'm drinking, not watching. Don't make me watch.
- I know your face from the street. In here, I don't.

**pass.room.watch.cold**
- Same face, different room. I'm still the Watch.
- Sit somewhere I can see you and drink slowly.

**pass.room.clergy**
- The Flame keeps this house too, child. Badly.
- One cup, and then the Mission's door. Join me or don't.
- Sit. A priest in a tavern is a man everybody talks to.

**pass.room.wastrel**
- (a look over the cup, and nothing)
- This is my corner. The rest of the room's yours.
- Don't stand near me if the Watch knows you. It's catching.

**farewell**
- Go on, then.
- Mind how you go.
- (a nod, and back to it)

**farewell.hostile**
- Get gone.
- We're done. Don't make it twice.
- (nothing more, and a hand near the belt)

**farewell.serf**
- Back to the ropes. Mind the planks.
- Go on. The crane line won't wait for either of us.
- Kingsday, if the wage lands. Look for me at the Gull.

**farewell.serf.cold**
- That's all you get. Go.
- (a turned shoulder)

**farewell.serf.warm**
- Go carefully, friend. The quay isn't.
- Come back at the pay-out. I'll owe you a cup by then.

**farewell.trade**
- Mind the step on your way out.
- Come back with coin. Or with news. Coin first.
- Shutters at dusk. Be quick if you want anything.

**farewell.trade.cold**
- The door's behind you. Use it.
- I'll count the shelves now. Good day.

**farewell.trade.warm**
- Come back any hour. The back room's yours if you need it.
- Go well. I'll keep the good cask for you.

**farewell.maritime**
- Fair wind, if there's any.
- Off with you. The tide doesn't stand and talk.
- Look for me at the Bilge. Or under it.

**farewell.maritime.cold**
- Go. Landsmen talk too long.
- (a turn back to the water)

**farewell.maritime.warm**
- Go well. Keep off the strand at night. I mean it.
- Next hull out has a place for you. If any hull ever goes out.

**farewell.watch**
- Walk on. That's all.
- Keep the lane clear behind you.
- Go about your business, and keep it business.

**farewell.watch.cold**
- I'll remember this talk. Go.
- Off you go. The page is still open.

**farewell.watch.warm**
- Keep my rounds quiet. Go on.
- Go well. Any trouble finds you, the post answers it.

**farewell.wastrel**
- Go on. If you've a crust left, you know where I sleep.
- Say nothing to the Watch. Nothing. Go.
- (already looking past you, at the next purse)

**farewell.wastrel.cold**
- Done, then. Walk wide of the Gullet.
- (a look at your purse, and then the wall)

**farewell.wastrel.warm**
- Flame keep you, friend of the strand. Go careful.
- Come find me if you've bread. Or news. Bread first.

**farewell.clergy**
- The Flame keep you. Go in peace, and go before dark.
- Go carefully, child. The Mission's door stays open.
- Peace on you. Tell the hungry about the soup.

**farewell.clergy.cold**
- The door's open. My time isn't. Go.
- Go, and think on what you carry.

**farewell.clergy.warm**
- Warmth of the Flame on you, friend. Come back before dark.
- Go well. There's a bowl kept back for you tonight.

**farewell.husbandry**
- Go on. The beasts get restless around strangers.
- Mind the pen on your way. She kicks.
- Off with you. The dog'll see you out.

**farewell.husbandry.warm**
- Come by the yard. The dog'll be glad. So will I.
- Go well. Bring a bone next time and you're family.

**farewell.urchin**
- Off, then. Watch your pockets. Not from me.
- (gone into the crowd, small as a cat)

**farewell.disciple**
- The Flame keep you. There's soup at dusk if you want it.
- Go well. I'll tell Father you asked.

**farewell.sailor**
- Fair wind. It's all any of us has left to say.
- Off you go. Hammocks fill early at the Rows.

**farewell.fisher**
- Go on. Mind the strand at the turn of the tide.

**farewell.carter**
- Go on. The dray is patient and I am not.

**farewell.thief**
- Go on. I was never here and neither were you.
- (melting back into the doorway)

**farewell.away**
- Suit yourself.
- Walk off, then. The tide won't miss you either.
- (the words trail off behind you)
- Off, then. I'd nearly got to the point.

**farewell.away.watch**
- I wasn't finished. Remember that I wasn't finished.
- Walking away from the Watch. That's a habit to lose.

**farewell.away.trade**
- The door doesn't shut itself. Mind it.
- Walk out mid-word. See how the price sits next time.

**farewell.away.wastrel**
- Off you go, then. Nobody stays for the strand.
- (a shrug, and the hand goes back in the sleeve)

**farewell.away.serf**
- Aye, go on. Rope waits for nobody either.
- Half a word and gone. The foreman would like you.

**overheard**
- ...two quarters, and not a Mercian bottom at the quay...
- ...the penny falls Kingsday whether the wage does or not...
- ...they say it was a war a long way off that did it...
- ...ask the Netmenders. They saw the ship come in. They see everything...

**overheard.serf**
- ...the Duke says patience, and patience is what I'm paying him in...
- ...bondsworn by the spring if the lanes stay shut. Mark me...
- ...no shipments, no shifts. No shifts, no penny. That's the sum...
- ...they pulled another one off the strand at the turn of the tide...
- ...the crane line went slack at noon and nobody said why...
- ...sold the years and kept the house. Call it the better half...

**overheard.trade**
- ...the Weighhouse stamps what it likes and I sell what I'm left...
- ...the Dezdant war, a long way off, and my shelves pay for it...
- ...Fenner's lending at a Royal on four. On four. Against the penny...
- ...the Bond's full and sealed, and nobody's key fits it...

**overheard.maritime**
- ...the war has the lanes past the point. No hull goes, and none comes...
- ...the Kestrel was out the night the bodies came up. Wake says so...
- ...a war too far off to spell, and the sea road's what it costs us...
- ...a candle at the Wall for him. The sea kept the rest...
- ...lamplight under the Hold's doors again. Officially empty...

**overheard.watch**
- ...the sergeant wants the Rise walked twice. Twice, with what men...
- ...no wage three weeks and the pot cold at the post. Say that to Vess...
- ...another one off the strand and we write it down and that's all...
- ...the impound dog won't go near the Hold. I trust the dog...

**overheard.wastrel**
- ...the crimp works the Bilge on pay night. Stay off the hammocks...
- ...low tide at the grate, and what came up the grate I won't say...
- ...the Watch doesn't come down the Gullet. Never has...
- ...somebody asked after the killings. Nobody asks. Warn the rest...
- ...a Royal for a dry throat and two for a name, that's the rate...

**overheard.clergy**
- ...the docket's all one story this season, and the story's the sea...
- ...three letters to the monastery, and the fourth I didn't send...
- ...not the war that frightens me. What the war lets through...
- ...soup for forty and bread for twenty. We'll manage. We always do...

**overheard.husbandry**
- ...the dogs won't work the east end. Not for meat, not for me...
- ...rat scalps pay by the Watch's count, and the Watch can't count...
- ...the goats know the tide better than the harbourmaster does...

**react.struck**
- Hoy! Not in here!
- Somebody get between them.
- (up off the stool, cup in hand, watching)

**react.steel**
- Steel! Get back from it!
- Put it away. Put it away, for the Flame's sake.
- Fetch Cull. Fetch anybody.

**react.slew**
- Not getting up. Not getting up.
- Flame keep us. That one's gone.
- Murder. Under this roof. Murder.

**react.serf.struck**
- Hoy! Venn'll have you out for that.
- Not in here. Take it to the street.
- That was <victim> you hit. Mind who else is looking.

**react.serf.steel**
- Steel! Get behind the tables!
- A blade in the Gull. Somebody hold the door.
- Put that away or the whole quay comes down on you.

**react.serf.slew**
- Down, and not moving. Flame keep us.
- Dead on the boards. Somebody get the Watch.
- You've killed <victim>. In front of all of us.

**react.trade.struck**
- Not by my counter you don't.
- Mind the stock! Fight where there's nothing to break.
- One more swing and the bouncers earn their wage.

**react.trade.steel**
- Steel in a house of trade. Put it up, or get out.
- Not the shelves. Flame, not the shelves.
- A blade. That's the Watch's business now, not the house's.

**react.trade.slew**
- A body on my floor. Somebody get Cull.
- Flame keep <victim>. And Flame keep this house, after that.
- That'll be talked about at every counter by morning.

**react.maritime.struck**
- Hoy! Fists on land are still fists.
- Knives checked at the door, and still this.
- Steady. Steady. That's a person, not a bale.

**react.maritime.steel**
- Steel! Stand off him, all of you.
- A blade on land. Land has a Watch. Put it away.
- Get back from it. The sea's enough killing for one quay.

**react.maritime.slew**
- Dead. On land, and dead. Light a candle at the Wall.
- Gone. Somebody bar the door before the Watch does.
- The sea did not take <victim>. You did. Say it to the Wall.

**react.watch.struck**
- A brawl's the house's law. Keep it a brawl.
- Fists. I saw fists. Keep it at fists and I keep my seat.
- One blow. I've seen one blow. Don't show me two.

**react.watch.steel**
- Steel. Now it's mine.
- That blade's written down. Put it away before the rest is.
- A blade in my sight. You know how this ends.

**react.watch.slew**
- A killing. In my sight. Stand where you are.
- Dead. That's a rope, and I'm the man who fetches it.
- You put <victim> down in front of the Watch. There's no second time.

**react.wastrel.struck**
- Not my fight. Not my fight. Not my fight.
- (under the table, and staying there)
- Hit him again and the Watch hears it on the Rise.

**react.wastrel.steel**
- Steel. Out. Out the back, all of you.
- (a hand in somebody's purse while the room looks at the blade)
- Not for a stranger. Not tonight. Get back.

**react.wastrel.slew**
- Dead. Nobody saw it. Say it. Nobody saw it.
- (out of the door before the body's cold)
- That's the rope for you. And a bad night for the rest of us.

**react.clergy.struck**
- Peace. Peace, both of you. The Flame doesn't want this.
- Enough. Every soul a wick, and you're snuffing one.
- Hands down, child. Whatever was said, hands down.

**react.clergy.steel**
- Put the steel away. The Flame has seen it and so have I.
- Not steel. Not under a roof the Flame keeps. Put it up.
- Flame keep us all. A blade, in a house full of people.

**react.clergy.slew**
- Flame receive <victim>. And Flame forgive what did this.
- Somebody's dead and the lamp didn't go out. It should have.
- I will say the words over <victim>. I will say others over you.

**react.husbandry.struck**
- Hoy! The dog's up. Don't make me let her go.
- Beasts fight cleaner than that. Stop it.

**react.husbandry.steel**
- Steel! Down, girl. Down. Nobody goes near it.
- Put it away. The dog knows what a blade is and so do I.

**react.husbandry.slew**
- Dead. The dog knew before I did. She always does.
- Flame keep <victim>. Somebody fetch the Watch, and not the dog.

### the Watch on you

`content/raws/barks/watch_voice_barks.json`. 10 tables, 36 rows. `watch.notice.hands` is a watchman's eyes landing on you with your fists up and `watch.notice.sneak` with you crouched (a glance, not a cause, and never the one said to the other). `watch.heat` is a watchman passing a man the post has heard of. `watch.paper` is a warrant out, a demand and never an arrest by itself. `watch.wielder` is the deference table through gritted teeth. `watch.approach.<cause>` is the readback as a Closing watchman crosses the room after his halt, violence split by what is actually up (`.steel` with a blade in the hands, the bare key with fists).

**watch.notice.hands**
- Hands where I can see them, and we needn't talk at all.
- Lower them. Whatever you were about to do, do it elsewhere.
- You. Yes, you. Fists down.
- I see the hands. Now let me see them open.

**watch.notice.sneak**
- I see you. Stand up straight and be seen.
- Up. Nobody creeps past the Watch on his knees.
- Crouching where I can see you. That's a sore back or a bad idea.
- Walk like a man with nothing to hide, or I'll find what you're hiding.

**watch.heat**
- Word's out on you. Not paper. Not yet.
- Your name came up at the post. Twice. Keep it from a third.
- I have heard things. Give me a reason to have seen them.
- Walk on. The sergeant knows your face and I know the sergeant.

**watch.paper**
- There's paper on you. Stand still and this goes easy.
- The post has your name written down. I'm reading it now.
- Paper out, and here you are on my street as if there wasn't.
- Don't run. Every man who runs ends up on the same post.

**watch.wielder**
- Wielder. The ward's yours, as ever. Go where you like.
- No door's locked to you. That was never my choice.
- Whatever you need, Wielder. I'll write down that I gave it.

**watch.approach**
- Stay where you are. I'm coming to you.
- Don't make me run. I'm not paid to run.
- Hands out. Empty. Now.

**watch.approach.violence**
- Stand still. I saw it, and I'm the one who says what it was.
- Put your hands down. Down. I won't say it a third time.
- Nobody leaves this room. Least of all you.
- Steady now. You're done swinging. That part's over.

**watch.approach.violence.steel**
- Put the blade down. Down, and step back from it.
- Steel in my sight. It goes on the floor before I reach you.
- Nobody leaves this room with a blade out. Least of all you.
- Drop it. You're done with it, whether you know that yet or not.

**watch.approach.paper**
- The paper says your name and so do I. Stand there.
- I've been looking for that face. Don't turn it away.
- Stay put. The cell's warmer than the strand.

**watch.approach.goods**
- That sack. Set it down where I can see it.
- The sack stays. You stay with it. Don't make it two crimes.
- Set it down. The jars are the ward's now, and so's your evening.

### WHAT THE WARD SAYS, by trade

`content/raws/barks/rumor_barks.json`. 7 tables, 21 rows. The WardTalk topic resolves `rumor.<family>` first and the owner's `gossip` after it. The one row with the learned word is a priest's, by the diction rule.

**rumor.serf**
- Muster was short again this morning. The foreman took half the names and sent the rest home.
- A crate broke on the crane line Kingsday and a lad went under it. He's at the Mission now.
- The Netters' Duke posted the penny early. Early means she's afraid of something.

**rumor.trade**
- The Weighhouse turned a hull away at dawn. No papers, they said. It had papers.
- Somebody cracked Brann's cellar again and took nothing. Took nothing. Think on that.
- Fenner's had the shutters up since Kingsday. A pawnbroker who won't open is a pawnbroker who's owed.

**rumor.maritime**
- Wake's not sailing the Kestrel. Says she needs caulking. She needs a lane.
- The strand gave up a man at low tide and the post wrote him down as drowned. Drowned men don't come up dry.
- There's talk of a bottom lying off the point at night with no lights. Nobody's rowed out to ask.

**rumor.watch**
- The sergeant's pulled the night post off the Rise and put it at the Bond. Guarding boxes, not people.
- Two lads on the Tarwalk beat were paid off Kingsday. The paper's still pinned, so nobody's said why.
- There's a name on the post's board that wasn't there yesterday and nobody at the post will read it out.

**rumor.wastrel**
- The crimp lost a man off the Bilge Kingsday. Lost him. Nobody's fished him up, so he's not lost, he's sold.
- Old Cobb's dog went into the Hold's alley and came out the other side. Dogs don't do that there.
- There's a stranger at the Eel-Pots paying for the names of who works the strand at night. Paying well.

**rumor.clergy**
- Father Maell sat up with a drowned man's widow last night and wrote a letter after. He's stopped sealing them.
- Two disciples came back from the strand at dawn and one of them won't go near the water now. Won't say why.
- The catechism has a word for what I think crossed the sea. Bloodletter. I'll say it in here and nowhere else.

**rumor.husbandry**
- Cobb's terriers won't take the east end. He walked them there twice this week and they sat down.
- The impound dog howled all Kingsday night at nothing anyone could see. The keeper's lost his voice at it.
- Three goats gone off the Gallows Row pen this quarter. Not stolen. Gone. The gate was shut.

### the greet siblings

`content/raws/barks/greet_more_barks.json`. 13 tables, 26 rows. Numbered `.2` siblings that widen a canon greet cell. `BarkTables::cellLine` joins a key's rows with its siblings' and rotates across the union. No cell totals seven (the rotation steps by seven per conversation and a cell of seven would say one row forever, which the suite pins).

**greet.serf.neutral.morning.2**
- You're up early for somebody with clean hands.
- If you're here for work, the foreman's the one with the slate.

**greet.serf.neutral.day.2**
- Talk if you're walking. I've a crate that isn't going to lift itself.
- You've caught me between loads. That's the only time I'm worth talking to.

**greet.serf.neutral.evening.2**
- I've been paid, which makes me a friendlier man than I was at noon.
- Say it quick. There's a stool at the Gull with my shape in it.

**greet.serf.neutral.night.2**
- Night wage is a penny more and a finger less. Ask me why I'm on it.
- You're not a foreman and you're not the Watch, so what are you doing awake?

**greet.watch.neutral.morning.2**
- I've counted the carts and now I'm counting you. What is it?
- The post's kettle is on and I'm not. Make it worth the cold.

**greet.watch.neutral.day.2**
- If you've come to report something, the sergeant's the one who writes. I'm the one who listens.
- Middle of the day and nobody's been stabbed. Don't spoil it.

**greet.watch.neutral.evening.2**
- The lamplighter's ahead of me and the trouble's behind. You're in between.
- Pay-out's done, so half this street is drunk. Which half are you?

**greet.watch.neutral.night.2**
- I'm the only lamp on this stretch. Stand in it while you talk.
- A man awake at this hour is a man I'll remember tomorrow. Give me a reason not to.

**greet.trade.2**
- I've two prices. One for people who touch things and one for people who don't.
- Come in, come in. The door's the only thing I give away.

**greet.wastrel.2**
- I've slept in worse than this doorway, and I'll sleep in worse again. Talk.
- If you're the Watch, I've done nothing. If you're not, what've you got?

**greet.maritime.2**
- My feet have been on stone two quarters and they've forgotten what a deck is.
- You walk like a landsman. Talk like one, then, and I'll try to follow.

**greet.clergy.2**
- Sit if you're tired. The Flame doesn't ask a soul to stand.
- You've a question on your face. Ask it before it sours.

**greet.husbandry.2**
- She hasn't bitten anyone this week and I'd like to keep it that way. Stand there.
- If it's about the smell, no. If it's about the dogs, maybe.
