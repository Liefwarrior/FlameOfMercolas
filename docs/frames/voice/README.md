# The street talks

Every frame in here is the real `dist\granadad.exe` on the gated Windows build (branch
`lane/voice`, commit `1e5208c3`), playing the voice channel through the real verbs. Nothing
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
| pass | the nearest body within two tiles, once | `pass.<type>.<attitude>` | 12 s on the channel, 60 s per mouth |
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
breadth, not the world's news.

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

`content/raws/barks/voice_barks.json`. 100 tables, 289 rows. `pass` is the one line as a body passes within reach. `farewell` is the goodbye and `farewell.away` the word thrown after you when you walk off. `overheard` is one side of an exchange between two bodies. `react` is a witness's word on a landed blow (struck), a drawn blade (steel) and a killing (slew).

**pass**
- Mind yourself.
- Wet planks. Watch your feet.
- Move along, there is a cart behind you.
- (a nod, no more)

**pass.serf**
- Rope coming through. Mind your feet.
- Long shift. Longer tide.
- Crane line is moving. Stand clear of it.
- Quarter-day is Kingsday. Do not remind me.
- Planks are wet. Everybody says so and somebody still goes in.

**pass.serf.cold**
- Not you again.
- Walk on. I have nothing for you.
- (a look, and a wider berth)

**pass.serf.hostile**
- Keep walking or I call the lads.
- The ropes remember faces. Yours is one.
- Cross me on the quay and the tide can have you.

**pass.serf.warm**
- There goes a friend of the ropes.
- Still upright. Good. Ale later, if the wage lands.
- Wind is fair and so is your name today.

**pass.serf.friend**
- You owe me a drink and I intend to collect.
- Mind the crates, friend. They do not know you.

**pass.trade**
- Stock is fresh as the tide brings it. Which is not very.
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
- There is a Royal's credit for you here. Tell nobody.
- Mind the step, friend. I would hate to lose you.

**pass.maritime**
- Fair wind, landsman.
- Tide waits on nobody. Walk quicker.
- Mind the lines. A mooring rope has broken better legs than yours.
- No lane open past the point. Ask any man with salt on him.

**pass.maritime.cold**
- Landsman. Keep to the land.
- Out of the way of working men.
- (a spit over the side, and a look)

**pass.maritime.hostile**
- The sea keeps what the ward will not. Remember that on a pier.
- Walk the strand alone some night. Go on.

**pass.maritime.warm**
- Fair wind, friend. What there is of it.
- You would make a sailor. A poor one, but a sailor.
- First cup at the Bilge is yours if the boats ever sail.

**pass.watch**
- Orderly, now.
- Walk on. Nothing here is yours.
- Lane clear. Keep it that way.
- Carts have the Rise. You have the gutter. Walk it.

**pass.watch.cold**
- I know that face. Keep it moving.
- Every step in my ward, I count it.
- The post has your name written down. Do not add to the page.

**pass.watch.hostile**
- Give me a reason. Any reason at all.
- One shout and you sleep in the garrison. Walk.
- I am watching you in daylight. Think about the dark.

**pass.watch.warm**
- Good day. That is not a thing I say to many.
- Keep my rounds boring and I will keep them yours.
- A quiet street is a friend's doing. Walk on.

**pass.wastrel**
- Spare a Royal? No? Spare a look, then.
- Coin in your walk. None in mine. Spare the difference?
- Mind your purse. Not me. Somebody else. Mostly.
- Nothing to sell and nothing to say. Walk on.
- (the hand goes out, and comes back empty)

**pass.wastrel.cold**
- Not to you. Not even for a Royal.
- The Gullet has heard about you. Walk wide of it.
- (the face turns to the wall)

**pass.wastrel.hostile**
- Come down the Gullet after dark. We will talk properly.
- The fog knows your name now.
- Keep walking. Faster than that.

**pass.wastrel.warm**
- There is the one who stood us a crust. Flame keep you.
- A friend of the strand. Say it loud so the Watch hears.
- Half a herring in my sleeve, and half of it yours if you ask.

**pass.clergy**
- The Flame keep you, whoever you are.
- The Mission feeds at dusk. Tell whoever is hungry.
- Every soul a wick. Mind yours in this wind.
- Peace on the quay, child. What there is of it.

**pass.clergy.cold**
- The Flame sees what the Watch does not. Walk on.
- The door is open to you. My ear is not, today.
- (a blessing, made quickly, and not for you)

**pass.clergy.hostile**
- The Flame keeps its own accounts. Yours is long.
- Not in the Mission's sight. Whatever you are about, not here.

**pass.clergy.warm**
- Warmth of the Flame on you. You have earned a little.
- The bunkroom speaks well of you. That is rarer than gold here.
- Peace, friend. The soup is thin tonight but the bowl is yours.

**pass.husbandry**
- Mind the beasts. They mind you.
- The dog likes you. That is not a compliment to either of you.
- Ware the goats. They have opinions about strangers.
- Step wide of the pen. She kicks.

**pass.husbandry.cold**
- The dog does not like you. I trust the dog.
- Keep your hands off the animals and off my yard.

**pass.husbandry.warm**
- The dog remembers you. So do I. Both fondly.
- Come by the yard. There is always a pup nobody wants.

**pass.urchin**
- Got a crust? Got a Royal? Got a name?
- Mind the gulls. They take bread and fingers.
- I saw a hand in your pocket. Not mine. Mine is here.
- (a small shape, gone before you look)

**pass.thief**
- Nothing in my hands. Look.
- Fine weather for walking. Fine weather for not seeing things.
- (a glance at your belt, and away)

**pass.fisher**
- Nets up. Nothing in them a man would eat.
- Strand is wide at this tide. Do not trust it.
- The sea gave back a boot this morning. Only the boot.

**pass.sailor**
- No sailing. No lanes. No pay. Ask me anything else.
- Hammock at the Rows, if a body has not died in it.
- The Kestrel is the last hull with a crew. The rest are firewood.

**pass.carter**
- Cart coming through. Feet or wheels, your pick.
- Do not touch the dray. Hold her with your eyes.
- Round again. Weighhouse to the Bond and back till dark.

**pass.disciple**
- The Flame keep you. Have you eaten?
- Father Maell says to tell the hungry. You look hungry.
- I am not to stare at the sea. I am staring at the sea.

**farewell**
- Go on, then.
- Mind how you go.
- (a nod, and back to it)

**farewell.hostile**
- Get gone.
- We are done. Do not make it twice.
- (nothing more, and a hand near the belt)

**farewell.serf**
- Back to the ropes. Mind the planks.
- Go on. The crane line will not wait for either of us.
- Kingsday, if the wage lands. Look for me at the Gull.

**farewell.serf.cold**
- That is all you get. Go.
- (a turned shoulder)

**farewell.serf.warm**
- Go carefully, friend. The quay is not.
- Come back at the pay-out. I will owe you a cup by then.

**farewell.trade**
- Mind the step on your way out.
- Come back with coin. Or with news. Coin first.
- Shutters at dusk. Be quick if you want anything.

**farewell.trade.cold**
- The door is behind you. Use it.
- I will count the shelves now. Good day.

**farewell.trade.warm**
- Come back any hour. The back room is yours if you need it.
- Go well. I will keep the good cask for you.

**farewell.maritime**
- Fair wind, if there is any.
- Off with you. The tide does not stand and talk.
- Look for me at the Bilge. Or under it.

**farewell.maritime.cold**
- Go. Landsmen talk too long.
- (a turn back to the water)

**farewell.maritime.warm**
- Go well. Keep off the strand at night. I mean it.
- Next hull out has a place for you. If any hull ever goes out.

**farewell.watch**
- Walk on, citizen. That is all.
- Keep the lane clear behind you.
- Go about your business, and keep it business.

**farewell.watch.cold**
- I will remember this talk. Go.
- Off you go. The page is still open.

**farewell.watch.warm**
- Keep my rounds quiet. Go on.
- Go well. Any trouble finds you, the post answers it.

**farewell.wastrel**
- Go on. The strand will still be here.
- Say nothing to the Watch. Nothing. Go.
- (already looking past you)

**farewell.wastrel.cold**
- Done, then. Walk wide of the Gullet.
- (a look at your purse, and then the wall)

**farewell.wastrel.warm**
- Flame keep you, friend of the strand. Go careful.
- Come find me if you have bread. Or news. Bread first.

**farewell.clergy**
- The Flame keep you. Go in peace, what there is of it.
- Go carefully, child. The Mission's door stays open.
- Peace on you. Tell the hungry about the soup.

**farewell.clergy.cold**
- The door is open. My time is not. Go.
- Go, and think on what you carry.

**farewell.clergy.warm**
- Warmth of the Flame on you, friend. Come back before dark.
- Go well. There is a bowl with your name on it tonight.

**farewell.husbandry**
- Go on. The beasts get restless around strangers.
- Mind the pen on your way. She kicks.
- Off with you. The dog will see you out.

**farewell.husbandry.warm**
- Come by the yard. The dog will be glad. So will I.
- Go well. Bring a bone next time and you are family.

**farewell.urchin**
- Off, then. Watch your pockets. Not from me.
- (gone into the crowd, small as a cat)

**farewell.disciple**
- The Flame keep you. You have not eaten. Go and eat.
- Go well. I will tell Father you asked.

**farewell.sailor**
- Fair wind. It is all any of us has left to say.
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
- Walk off, then. The tide will not miss you either.
- (the words trail off behind you)
- That is one way to end a talk.

**farewell.away.watch**
- I was not finished. Remember that I was not finished.
- Walking away from the Watch. Bold.

**farewell.away.trade**
- The door does not shut itself. Mind it.
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
- ...the Duke says patience, and patience is what I am paying him in...
- ...bondsworn by the spring if the lanes stay shut. Mark me...
- ...no shipments, no shifts. No shifts, no penny. That is the sum...
- ...they pulled another one off the strand at the turn of the tide...
- ...the crane line went slack at noon and nobody said why...

**overheard.trade**
- ...the Weighhouse stamps what it likes and I sell what I am left...
- ...the Dezdant war, a long way off, and my shelves pay for it...
- ...Fenner is lending at a Royal on four. On four. Against the penny...
- ...the Bond is full and sealed, and nobody's key fits it...

**overheard.maritime**
- ...the war has the lanes past the point. No hull goes, and none comes...
- ...the Kestrel was out the night the bodies came up. Wake says so...
- ...a war too far off to spell, and the sea road is what it costs us...
- ...a candle at the Wall for him. The sea kept the rest...
- ...lamplight under the Hold's doors again. Officially empty...

**overheard.watch**
- ...the sergeant wants the Rise walked twice. Twice, with what men...
- ...no wage three weeks and the pot cold at the post. Say that to Vess...
- ...another one off the strand and we write it down and that is all...
- ...the impound dog will not go near the Hold. I trust the dog...

**overheard.wastrel**
- ...the crimp works the Bilge on pay night. Stay off the hammocks...
- ...low tide at the grate, and what came up the grate I will not say...
- ...the Watch does not come down the Gullet. Never has...
- ...somebody asked after the killings. Nobody asks. Warn the rest...
- ...a Royal for a dry throat and two for a name, that is the rate...

**overheard.clergy**
- ...the docket is all one story this season, and the story is the sea...
- ...three letters to the monastery, and the fourth I did not send...
- ...not the war that frightens me. What the war lets through...
- ...soup for forty and bread for twenty. We will manage. We always do...

**overheard.husbandry**
- ...the dogs will not work the east end. Not for meat, not for me...
- ...rat scalps pay by the Watch's count, and the Watch cannot count...
- ...the goats know the tide better than the harbourmaster does...

**react.struck**
- Hoy! Not in here!
- Somebody get between them.
- (a stool goes over, and the room goes quiet)

**react.steel**
- Steel! Get back from it!
- Put it away. Put it away, for the Flame's sake.
- Fetch Cull. Fetch anybody.

**react.slew**
- He is not getting up. He is not getting up.
- Flame keep us. That one is gone.
- Murder. Under this roof. Murder.

**react.serf.struck**
- Hoy! Venn will have you out for that.
- Not on pay night. Not in here.
- That was a working man you hit. Mind who else is one.

**react.serf.steel**
- Steel! Get behind the tables!
- A blade in the Gull. Somebody hold the door.
- Put that away or the whole quay comes down on you.

**react.serf.slew**
- He is down and he is not moving. Flame keep us.
- A rope hand. Dead on the boards. Somebody get the Watch.
- You killed him. In front of all of us. You killed him.

**react.trade.struck**
- Not by my counter you do not.
- Mind the stock! Fight where there is nothing to break.
- One more swing and the door has your name on it.

**react.trade.steel**
- Steel in a house of trade. Put it up, or get out.
- Not the shelves. Flame, not the shelves.
- A blade. That is the Watch's business now, not the house's.

**react.trade.slew**
- A dead man on the floor. Somebody get Cull.
- Flame keep him. And Flame keep this house, after that.
- That will be talked about at every counter by morning.

**react.maritime.struck**
- Hoy! Fists on land are still fists.
- Knives checked at the door, and still this.
- Steady. Steady. That is a man, not a bale.

**react.maritime.steel**
- Steel! Stand off him, all of you.
- A blade on land. Land has a Watch. Put it away.
- Get back from it. The sea is enough killing for one quay.

**react.maritime.slew**
- Dead. On land, and dead. Light a candle at the Wall.
- He is gone. Somebody bar the door before the Watch does.
- The sea did not take him. You did. Say it to the Wall.

**react.watch.struck**
- A brawl is the house's law. Keep it a brawl.
- Fists. I saw fists. Keep it at fists and I keep my seat.
- One blow. I have seen one blow. Do not show me two.

**react.watch.steel**
- Steel. Now it is mine.
- That blade is written down. Put it away before the rest is.
- A blade in my sight. You know how this ends.

**react.watch.slew**
- A killing. In my sight. Stand where you are.
- Dead. That is a rope, and I am the man who fetches it.
- You put him down in front of the Watch. Nobody is that stupid twice.

**react.wastrel.struck**
- Not my fight. Not my fight. Not my fight.
- (under the table, and staying there)
- Hit him again and the Watch hears it on the Rise.

**react.wastrel.steel**
- Steel. Out. Out the back, all of you.
- (a purse changes hands in the panic and nobody sees whose)
- Not for a stranger. Not tonight. Get back.

**react.wastrel.slew**
- Dead. Nobody saw it. Say it. Nobody saw it.
- (out of the door before the body is cold)
- That is the rope for you. And a bad night for the rest of us.

**react.clergy.struck**
- Peace. Peace, both of you. The Flame does not want this.
- Enough. Every soul a wick, and you are snuffing one.
- Hands down, child. Whatever he said, hands down.

**react.clergy.steel**
- Put the steel away. The Flame has seen it and so have I.
- Not steel. Not under a roof the Flame keeps. Put it up.
- Flame keep us all. A blade, and the night not half done.

**react.clergy.slew**
- Flame receive him. And Flame forgive what did this.
- A man is dead and the lamp did not go out. It should have.
- I will say the words over him. I will say others over you.

**react.husbandry.struck**
- Hoy! The dog is up. Do not make me let her go.
- Beasts fight cleaner than that. Stop it.

**react.husbandry.steel**
- Steel! Down, girl. Down. Nobody goes near it.
- Put it away. The dog knows what a blade is and so do I.

**react.husbandry.slew**
- Dead. The dog knew before I did. She always does.
- Flame keep him. Somebody fetch the Watch, and not the dog.

### the Watch on you

`content/raws/barks/watch_voice_barks.json`. 8 tables, 28 rows. `watch.notice` is a watchman's eyes landing on you crouched or with your hands up. `watch.heat` is a watchman passing a man the post has heard of. `watch.paper` is a warrant out, a demand and never an arrest by itself. `watch.wielder` is the deference table through gritted teeth. `watch.approach.<cause>` is the readback as a Closing watchman crosses the room after his halt.

**watch.notice**
- I see you. Stand up straight and be seen.
- Hands where I can see them, and we need not talk at all.
- Lower them. Whatever you were about to do, do it elsewhere.
- You. Yes, you. Walk like a man with nothing to hide.

**watch.heat**
- Word is out on you. Not paper. Not yet.
- Your name came up at the post. Twice. Keep it from a third.
- I have heard things. Give me a reason to have seen them.
- Walk on. The sergeant knows your face and I know the sergeant.

**watch.paper**
- There is paper on you. Stand still and this goes easy.
- The post has your name written down. I am reading it now.
- Paper out, and here you are on my street. Bold.
- Do not run. Every man who runs ends up on the same post.

**watch.wielder**
- Wielder. The ward is yours, as ever. Go where you like.
- No door is locked to you. That was never my choice.
- Whatever you need, Wielder. I will write down that I gave it.

**watch.approach**
- Stay where you are. I am coming to you.
- Do not make me run. I am not paid to run.
- Hands out. Empty. Now.

**watch.approach.violence**
- Stand still. I saw it, and I am the one who says what it was.
- Put your hands down. Down. I will not say it a third time.
- Nobody leaves this room. Least of all you.
- Steady now. You are done swinging. That part is over.

**watch.approach.paper**
- The paper says your name and so do I. Stand there.
- I have been looking for that face. Do not turn it away.
- Stay put. The cell is warmer than the strand.

**watch.approach.goods**
- That sack. Set it down where I can see it.
- The sack stays. You stay with it. Do not make it two crimes.
- Set it down. The jars are the ward's now, and so is your evening.

### WHAT THE WARD SAYS, by trade

`content/raws/barks/rumor_barks.json`. 7 tables, 21 rows. The WardTalk topic resolves `rumor.<family>` first and the owner's `gossip` after it. The one row with the learned word is a priest's, by the diction rule.

**rumor.serf**
- No Mercian bottom in two quarters. No wage, no penny, and the Duke counts both.
- They pulled another off the strand at the turn of the tide. Third this season.
- The crane line stopped at noon Kingsday and nobody at the Weighhouse said why.

**rumor.trade**
- The Bond is full and sealed. Whose goods, the harbourmaster will not say.
- Fenner lends at a Royal on four against the penny. Half this row owes him.
- Lamplight under the Hold's doors again. Officially empty for nine years.

**rumor.maritime**
- The Dezdant war has the lanes past the point. No hull goes out and comes back.
- The Kestrel was at sea the night the bodies came up. Wake tells everyone.
- There is a new name at the Wall every week and no storm to blame for it.

**rumor.watch**
- The sergeant doubled the Rise patrol. With what men, nobody at the post can say.
- Whatever came up off the strand, the post wrote it down and did nothing else.
- The Watch does not go down the Gullet. That is not a rumor. That is the rule.

**rumor.wastrel**
- Somebody is asking after the killings. Nobody asks. The Gullet is warned.
- The grate at the Outfall opens at low tide. What comes up it I will not say.
- The crimp works the Bilge on pay night. A hammock there is a berth elsewhere.

**rumor.clergy**
- The docket is one story this season: a war took the sea road, the widows came.
- I wrote three letters to the monastery about the water. None was answered.
- The catechism has a word for what crossed the sea. Bloodletter. Do not say it on the quay.

**rumor.husbandry**
- The dogs will not work the east end. Not for meat, not for the Watch's coin.
- Cobb's terrier refused the Hold's door. A dog does not refuse a door for nothing.
- The goats are off their feed since the tide turned strange. Beasts know.

### the greet siblings

`content/raws/barks/greet_more_barks.json`. 13 tables, 26 rows. Numbered `.2` siblings that widen a canon greet cell. `BarkTables::cellLine` joins a key's rows with its siblings' and rotates across the union. No cell totals seven (the rotation steps by seven per conversation and a cell of seven would say one row forever, which the suite pins).

**greet.serf.neutral.morning.2**
- Muster is called. If you are not on the list you are in the way.
- Cold enough to stiffen rope. Say your piece and let me pull.

**greet.serf.neutral.day.2**
- Berths are full and the Weighhouse is slow. Talk while I wait.
- Half the hold shifted and the other half is arguing. What?

**greet.serf.neutral.evening.2**
- Wage table first, then the Gull, then whatever you want.
- Shoulders done. If it is work, ask tomorrow. If it is ale, ask now.

**greet.serf.neutral.night.2**
- Keep your voice down. The bunkhouse sleeps light.
- Late for the planks. Late for honest business, my mother would say.

**greet.watch.neutral.morning.2**
- Dawn cart is through and the Rise is mine. Business?
- Early. State it quick, the sergeant walks at this hour.

**greet.watch.neutral.day.2**
- Midday. The lane is clear and I would like it kept so.
- Papers, cargo or trouble. Pick one and be quick.

**greet.watch.neutral.evening.2**
- Lamps lit, honest folk home. Which are you?
- Evening rounds. Say it walking if you can.

**greet.watch.neutral.night.2**
- Late walker. Name and business.
- The post is cold and so am I. What?

**greet.trade.2**
- Stock is what the tide left, and the tide left little. Buying?
- Counter is open. Purse open?

**greet.wastrel.2**
- A Royal for a dry throat, or a word for nothing. Your pick.
- You have the look of coin. I have the look of nothing. Talk.

**greet.maritime.2**
- No lanes, no sailing, no pay. What else is there to talk about?
- Salt in the air and a hull on blocks. Speak, landsman.

**greet.clergy.2**
- The Flame keep you. Have you eaten? The pot is on.
- Peace, child. What brings you to the Flame's door?

**greet.husbandry.2**
- Mind the dog. She minds you. Speak.
- Beasts fed, pen shut. What do you want with a keeper?
