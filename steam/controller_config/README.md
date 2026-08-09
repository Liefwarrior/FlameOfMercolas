# Steam Input action manifest -- Granadad: The Darkstreets

`game_actions.vdf` in this directory is a Steam Input "In-Game Action File"
declaring this game's action VOCABULARY -- the named verbs a Steam
controller config can bind to -- against the control-scheme redesign's
design brief. It does not depend on the exact keybind defaults, which a
concurrent session may still be tuning in `controls.hpp`/`controls.cpp`; see
"Where this sits relative to the keybind redesign" below.

## 1. There is no linked Steamworks SDK yet -- confirmed, not assumed

Grepped `CMakeLists.txt` (both of them: `native/CMakeLists.txt` and
`native/content/CMakeLists.txt`) and everything under `native/` for `steam`,
case-insensitive. The only hits in `native/` are false positives: three
source files (`main.cpp`, `render/controls.cpp`,
`include/granadad/render/controls.hpp`) that match on `PadStart`,
`PadRightStick`, and `PadLeftStick` -- **not** the word "steam". Everywhere
else the word actually appears in this repo is either water-vapor
terminology in the fluid simulation (`sim-core/.../FluidVaporizedEvent.java`,
`content/raws/fluids/water.json`, `MaterialPhase.java` -- this is a physical
simulation with a `Steam` phase) or Kenney-licensed Steam Deck / Steam
Controller icon assets under `content/art/kenney-input-prompts/`, which are
button-glyph images, not code.

Neither CMakeLists links, finds, or fetches `steamworks_sdk`, `steam_api`,
`ISteamInput`, or anything of the kind. **This game has no AppID and no
Steamworks integration.** That means:

- This manifest cannot be uploaded to a real depot or exercised inside the
  Steam client's controller configurator today -- both require an AppID.
- It cannot be validated against Steam's own VDF parser. What is here has
  been checked by hand and with a small standalone parser (see
  "How this was validated" below), not by Steam itself.
- If a Steamworks integration lands later, this file is the pre-built
  declaration to drop in: it is written against the game's own
  `granadad::render::Action` naming conventions specifically so it reads as
  a lookup table, not a redesign, once `ISteamInput()` calls need real
  action handles.

**If this premise ever stops being true** -- i.e. someone links
`steam_api(64).lib`/`.so` or vendors the Steamworks SDK into `native/` --
this whole section is stale and the manifest needs a second look (an AppID
means a real `steam_appid.txt` / depot build step, and a linked SDK means
the game can call `SteamInput()->GetDigitalActionData()` etc. directly
instead of relying on legacy emulation, which changes what "done" looks
like for this file).

## 2. The existing raw-gamepad input path, and why legacy Steam Input support likely already works today

Grepped `native/src` for how `Key::Pad*` values actually get read. The
answer, entirely in `native/src/client/main.cpp`:

- **SDL3's `SDL_Gamepad` API** (SDL3 renamed SDL2's `SDL_GameController` to
  `SDL_Gamepad`; it is the same abstraction under a new name). Not raw
  XInput, not raw `SDL_Joystick`, not a hand-rolled HID reader.
- Init: `SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)` (`main.cpp:1352`).
- Discovery: `SDL_GetGamepads()` + `SDL_OpenGamepad(ids[0])` picks up
  "whichever pad turned up first" (`main.cpp:1396-1404`), plus a hotplug
  path on `SDL_EVENT_GAMEPAD_ADDED` (`main.cpp:1574`) that opens a newly
  connected pad without a restart.
- Digital buttons: a `PadRow` table (`main.cpp:936-951`) maps each
  `granadad::render::Key::Pad*` enum value to an `SDL_GamepadButton`
  constant, read with `SDL_GetGamepadButton()` (`main.cpp:1044-1050`).
- Analog sticks: `SDL_GetGamepadAxis()` on `SDL_GAMEPAD_AXIS_LEFTX/LEFTY`
  drives movement direction and gait (walk/jog/sprint by magnitude
  threshold), and `SDL_GAMEPAD_AXIS_RIGHTX/RIGHTY` drives look
  (`main.cpp:1717-1743`), both run through this game's own radial deadzone
  and cubic look curve in `render/controls.hpp`.
- **A gap found while looking, unrelated to this task and out of its
  scope to fix** (this task does not touch `main.cpp`, per its own
  constraints): `Key::PadLeftTrigger` and `Key::PadRightTrigger` exist in
  the `Key` enum and `PadSettings::triggerDeadzonePercent` exists and is
  loaded/saved, but neither trigger is actually read anywhere in
  `main.cpp` -- there is no `SDL_GAMEPAD_AXIS_LEFT_TRIGGER` /
  `_RIGHT_TRIGGER` call, and the `PadRow` table has no trigger row (SDL3
  reports triggers as axes, not buttons, so they would need their own
  digital-click-from-axis handling, the way the stick-to-dpad code already
  does it for movement). Worth a look by whoever owns `main.cpp` next; not
  fixed here.

**Does Steam Input's default legacy mode already work with zero extra
code?** Yes, with reasonable confidence, for the ordinary case: when a
Steam game launches under Steam (even with no Steamworks SDK linked at
all), Steam's controller support -- on by default -- intercepts supported
physical pads (Xbox, DualShock/DualSense, Switch Pro, Steam Deck, Steam
Controller, and more) and re-emits them to the OS as a virtual XInput
device using the player's own binding (Steam's generic gamepad template,
or a community config). `SDL_Gamepad` reads that virtual device exactly
like a real Xbox controller -- it has no way to tell the difference, and
does not need to. So a PS5 controller, for instance, would already work in
this build today if launched through Steam, purely because Steam Input's
legacy layer makes it look like an Xbox pad before SDL ever sees it.

The caveat: this is Steam's **legacy** emulation layer, not the real
action-set-aware Steam Input system. It means:
- The player rebinds through Steam's generic "gamepad" overlay template
  (face buttons, sticks, triggers, bumpers), not through per-game action
  names like `Sneak` or `QuickWheel`.
- There is no action-set switching (no "the Menu screen automatically gets
  its own bindings while it's open") -- that only exists once the game
  calls `ISteamInput()` itself and activates action sets by name, which
  needs the SDK link described in section 1.
- `game_actions.vdf` in this directory is *for that second, real
  integration* -- it is not required for the legacy path to keep working,
  and shipping it changes nothing about today's behavior.

## 3. Key::Pad* -> Steam Input glyph lookup table

For when a real `ISteamInput()` integration replaces the `SDL_GamepadButton`
reads in `main.cpp` with `SteamInput()->GetDigitalActionData()` calls, or
just needs to draw the right glyph next to a binding: this is what each
`granadad::render::Key::Pad*` enum value (`include/granadad/render/controls.hpp`)
already means physically, via the `PadRow` table in `main.cpp:936-951`, and
what Steam Input calls the same physical control.

| `Key::Pad*` | SDL3 constant | Xbox glyph | PlayStation glyph | Steam Input generic name |
|---|---|---|---|---|
| `PadSouth` | `SDL_GAMEPAD_BUTTON_SOUTH` | A | Cross | Button A / South Face Button |
| `PadEast` | `SDL_GAMEPAD_BUTTON_EAST` | B | Circle | Button B / East Face Button |
| `PadWest` | `SDL_GAMEPAD_BUTTON_WEST` | X | Square | Button X / West Face Button |
| `PadNorth` | `SDL_GAMEPAD_BUTTON_NORTH` | Y | Triangle | Button Y / North Face Button |
| `PadLeftBumper` | `SDL_GAMEPAD_BUTTON_LEFT_SHOULDER` | LB | L1 | Left Bumper |
| `PadRightBumper` | `SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER` | RB | R1 | Right Bumper |
| `PadLeftTrigger` | *(not wired -- see section 2)* | LT | L2 | Left Trigger |
| `PadRightTrigger` | *(not wired -- see section 2)* | RT | R2 | Right Trigger |
| `PadLeftStick` | `SDL_GAMEPAD_BUTTON_LEFT_STICK` | L3 (stick click) | L3 | Left Stick Click |
| `PadRightStick` | `SDL_GAMEPAD_BUTTON_RIGHT_STICK` | R3 (stick click) | R3 | Right Stick Click |
| `PadStart` | `SDL_GAMEPAD_BUTTON_START` | Menu (☰) | Options | Start / Menu Button |
| `PadBack` | `SDL_GAMEPAD_BUTTON_BACK` | View | Share / Create | Back / Select Button |
| `PadUp` | `SDL_GAMEPAD_BUTTON_DPAD_UP` | D-Pad Up | D-Pad Up | D-Pad Up |
| `PadDown` | `SDL_GAMEPAD_BUTTON_DPAD_DOWN` | D-Pad Down | D-Pad Down | D-Pad Down |
| `PadLeft` | `SDL_GAMEPAD_BUTTON_DPAD_LEFT` | D-Pad Left | D-Pad Left | D-Pad Left |
| `PadRight` | `SDL_GAMEPAD_BUTTON_DPAD_RIGHT` | D-Pad Right | D-Pad Right | D-Pad Right |

Two rows outside this table entirely, because they are analog axes read
directly in `main.cpp:1717-1743` rather than `Key::Pad*` digital bindings:
left stick (`SDL_GAMEPAD_AXIS_LEFTX/LEFTY`) drives `MoveForward` /
`MoveBack` / `StrafeLeft` / `StrafeRight` in this manifest, and right stick
(`SDL_GAMEPAD_AXIS_RIGHTX/RIGHTY`) drives `CameraLook`. Steam Input calls
these the Left Stick and Right Stick respectively, and there is no
per-brand glyph variance for a stick the way there is for face buttons.

Steam Input's glyph system draws each of the above per the player's actual
connected controller automatically (Xbox face letters, PlayStation shapes,
Nintendo shapes, Steam Deck's own art) -- this table exists so that
whichever future code path needs to reason about "which physical control is
this" has a lookup instead of having to reverse-engineer it again.

## Where this sits relative to the keybind redesign

This deliverable declares the action **vocabulary** only: `MoveForward`,
`Attack`, `QuickWheel`, `OpenMenu`, `Pause`, `Confirm`, and so on, matching
the design brief. It does not encode which physical key or pad button is
"the" default for any of them -- that is `ControlSettings::defaults()`'s
job in `render/controls.cpp`, owned by the concurrent control-scheme
redesign, not this file. Steam Input configs bind physical controls to
*named actions*, not to `granadad::render::Key` values, so nothing about a
keybind redesign invalidates an action name declared here. If the redesign
renames or restructures a verb (for instance if `VerticalMove`'s
jump/vault/climb/drop consolidation, or `QuickWheel`'s radial behavior,
ends up called something else once it lands), this manifest's action names
should be updated to match -- but the shape of the file (two sets, this
grouping of stick vs. button actions) will not need to change.

## How this was validated

Steam's VDF ("KeyValues") format has no formal public grammar file to run a
real parser against, and there is no Steam client here to load the manifest
into (see section 1). Validated two ways instead:

1. **By hand**, reading every open/close brace pair against its match.
2. **With a small standalone tokenizer/parser**, written for this task,
   that is quote-aware and `//`-comment-aware (unlike a naive brace count,
   it will not get confused by a `{` or `}` inside a quoted title string --
   none happen to appear here, but the script does not rely on that) and
   that walks the whole file confirming every key is followed by either a
   nested block or exactly one quoted value, with no stray tokens left over
   at the end. It parsed the file into a tree of 2 action sets and 22
   actions total (`Default`: 5 `StickPadGyro` + 9 `Button` = 14 actions;
   `Menu`: 4 `StickPadGyro` + 4 `Button` = 8 actions) and one
   `localization > english` block with all 24 referenced tokens (2 set
   titles + 22 action titles) present and none orphaned. Raw brace count on
   the file is 33 opens / 33 closes, consistent with the parse.
