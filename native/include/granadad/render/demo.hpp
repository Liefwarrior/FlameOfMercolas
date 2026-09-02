#pragma once

// THE SCRIPTED DEMO -- a curated route through the ward that plays itself.
//
// WHY THIS AND NOT ANOTHER --smoke FLAG. Every scripted line this build has
// (--trail, --roofs, --flame, --skyrun, --threshold, --burgle) runs INSIDE
// runSmoke(): headless, as fast as the CPU will go, one PNG at the end. That
// shape is exactly right for evidence and exactly wrong for a demo, which has
// to be WATCHED. The two things a watcher needs -- a human pace, and a caption
// saying what is being looked at -- are things a capture harness deliberately
// does not have.
//
// So this is the same idea aimed the other way: the route is a table of BEATS,
// the playhead advances one beat-frame per rendered frame, and every beat
// drives the session through the SAME public calls a keyboard makes --
// Session::examine(), toggleCasebook(), commitCasebookLead(), toggleDistrictMap(),
// skipToHour(), and a sim::MoveInput handed back to the frame loop to be
// stepped. Nothing here reaches into the simulation sideways, which is the
// only reason a frame it produces is worth anything.
//
// DETERMINISM. The project gates on twin-run determinism and the demo must not
// be the one thing that drifts, so the playhead is counted in FRAMES and never
// in seconds: the client runs a fixed number of simulation steps per rendered
// frame while the demo is up (see main.cpp), so demo frame N is the same
// picture on a 60 Hz laptop and a 144 Hz desktop and inside a capture. Wall
// clock is used for exactly one thing -- sleeping so the window does not run
// the route at four times speed -- and it cannot change what is drawn.
//
// NOT A SECOND UI VOCABULARY. The card and the caption are PanelFrame,
// drawBreadcrumb, drawInvertedFill and drawCellText out of panel.hpp, in the
// register docs/design/UI-REFERENCE-TERMINAL.md sets down, with the caption
// STANDING DOWN whenever a page or a conversation owns the screen -- the same
// rule every other overlay in this build keeps.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "granadad/render/anim.hpp"
#include "granadad/render/framebuffer.hpp"
#include "granadad/sim/player.hpp"

namespace granadad::render {

class Session;

/// What one beat of the route does.
enum class DemoAct : std::uint8_t {
    /// A title card, centred, over a dimmed frame. `line`/`sub` are its two
    /// rows. Nothing moves while one is up.
    Card,
    /// Put the camera on (a, b) of band `c`, facing yaw `d` degrees. INSTANT,
    /// and the demo's only cut -- a route that walked every link between the
    /// quay, the Rise, the Mission and the Weighhouse would be twenty minutes
    /// of pavement. Lands on the nearest standable cell, searched in a fixed
    /// spiral, so a coordinate that drifted by a tile does not strand the run.
    Cut,
    /// Stand still. The beat every shot ends on, so the eye has time.
    Hold,
    /// Turn toward tile (a, b) over the beat, easing, and hold once arrived.
    Face,
    /// Turn to `d` degrees over the beat.
    Pan,
    /// Route to (a, b) on the current band and walk it, looking where the feet
    /// are going. Ends early on arrival; ends anyway when the beat runs out,
    /// so a blocked route costs a beat and never the run.
    Walk,
    /// Press Q on whatever is underfoot.
    Examine,
    /// Open/close the casebook.
    Casebook,
    /// Put the casebook cursor on the lead whose id is `text`.
    CasebookLead,
    /// Run the selected lead's own commit verb.
    Commit,
    /// Open/close the ward map.
    Map,
    /// Step the ward map's zoom by `a`.
    MapZoom,
    /// Close whatever page is open.
    Close,
    /// Set the clock to hour `a`.
    Clock,
};

/// One beat. Deliberately flat and deliberately POD: the whole route is a
/// static table a reader can take in at once, which is the only way the order
/// of a demo stays arguable.
struct DemoBeat {
    /// Which section this beat belongs to. `--demo=SECTION` starts here.
    const char* section = "";
    DemoAct act = DemoAct::Hold;
    /// How many frames the beat lasts. At the demo's fixed cadence this is
    /// sixtieths of a second.
    int frames = 60;
    std::int32_t a = 0;
    std::int32_t b = 0;
    std::int32_t c = 0;
    /// Degrees, for Cut and Pan. 0 north, 90 east, clockwise.
    std::int32_t degrees = 0;
    /// A lead id (CasebookLead), or a card's first row.
    const char* line = nullptr;
    /// A card's second row.
    const char* sub = nullptr;
    /// The lower-left caption to raise while this beat runs. Null LEAVES THE
    /// CAPTION ALONE rather than clearing it, so one line can hold across a
    /// walk and the hold after it; empty string ("") takes it down.
    const char* caption = nullptr;
    /// When capturing, write this frame as `<dir>/<shot>.png`. Null takes no
    /// picture.
    const char* shot = nullptr;
    /// WHICH frame of the beat the shutter goes on. -1 is the beat's last
    /// frame, which is what a still wants: fades up, plates landed, the eye
    /// settled. IT IS NOT WHAT A TRANSIENT WANTS -- the lead-opened plate is up
    /// for three seconds and the hold after a clue is longer than that, so a
    /// shot on the last frame of that beat is a photograph of the plate having
    /// already gone. That defect was in the first run of this route and this
    /// field is the fix.
    int shotAt = -1;
};

/// The demo's lower-left commentary pane and its centred title card, exposed
/// so the case-watch overlay (case_watch.hpp) draws the exact panes the demo
/// draws instead of growing a second vocabulary for the same two ideas. Both
/// are pure draws: state (what the text is, how far the fade has got) stays
/// with whichever director owns the run.
void drawRouteCaption(Framebuffer& target, const std::string& caption, float alpha);
void drawRouteCard(Framebuffer& target, const std::string& line, const std::string& sub,
                   const std::string& foot, float alpha);
/// True while a page, a conversation or the case plate owns the pixels a
/// caption would take -- the demo's own stand-down rule, shared for the same
/// reason the draws are.
[[nodiscard]] bool routeOverlayStandsDown(const Session& session);

/// The route, in order. Exposed so a test can walk it without a window.
[[nodiscard]] const std::vector<DemoBeat>& demoRoute();

/// The sections, in the order they appear, for `--demo=SECTION` and --help.
[[nodiscard]] std::vector<std::string> demoSections();

/// Total frames the route runs for, from `section` onward (empty = all).
[[nodiscard]] int demoFrameCount(const std::string& section);

/// The playhead. One instance per run; `advance` once per rendered frame,
/// BEFORE the session is stepped, and `drawOverlay` after it is drawn.
class DemoDirector {
  public:
    /// `section` empty starts at the top. `shotDir` empty takes no pictures.
    DemoDirector(std::string section, std::filesystem::path shotDir);

    /// What the frame loop should feed the simulation this frame, and whether
    /// the route is still running. A false `running` means the route finished:
    /// the client closes its window, which is how the demo ends cleanly
    /// instead of dumping the player mid-ward.
    struct Tick {
        sim::MoveInput move;
        bool running = true;
    };
    [[nodiscard]] Tick advance(Session& session);

    /// THE CARD OWNS THE FRAME while this is true.
    ///
    /// Every composed page in this build already stands the HUD down when it
    /// takes the screen -- the ward map, the controls page and the casebook all
    /// do it, and for the same reason: a full-screen surface with its own
    /// header and its own readout does not want a compass ribbon and a clock
    /// landing in them. The card is the one full-screen surface the SESSION
    /// does not own -- the director draws it after drawFrame has already
    /// returned -- so it cannot stand the HUD down from the inside and has to
    /// say so from the outside. The client asks this before it draws.
    ///
    /// The threshold is the same one drawOverlay uses to decide the card is on
    /// screen at all, so the furniture is gone for exactly the frames the card
    /// is up and not one either side.
    [[nodiscard]] bool cardOwnsFrame() const noexcept;

    /// The card and the caption, drawn over the finished frame.
    void drawOverlay(Framebuffer& target, const Session& session) const;

    /// Writes the PNG the last `advance` armed, if it armed one. Called after
    /// drawOverlay so a captured frame is exactly the frame presented.
    void shutter(const Framebuffer& target);

    [[nodiscard]] int beatIndex() const noexcept { return at_; }
    [[nodiscard]] std::int64_t frameIndex() const noexcept { return frame_; }

  private:
    void enter(Session& session);

    const std::vector<DemoBeat>* route_;
    std::filesystem::path shotDir_;
    int at_ = 0;
    int held_ = 0;
    std::int64_t frame_ = 0;
    bool entered_ = false;
    bool finished_ = false;
    /// The shot `advance` armed for this frame, or null. ARMED IN advance AND
    /// NOT READ OFF THE PLAYHEAD IN shutter, because advance is also what ends
    /// a beat: a shutter that looked the beat up itself would be looking at the
    /// NEXT one on exactly the frame a beat closes.
    const char* pending_ = nullptr;
    /// The walk in progress: the router's own waypoints, and how far along.
    std::vector<std::int32_t> pathX_;
    std::vector<std::int32_t> pathY_;
    std::size_t leg_ = 0;
    int stuck_ = 0;
    /// The yaw a Pan/Face/Walk beat started from, so the ease is a pure
    /// function of how far into the beat the playhead is.
    std::int32_t yawFrom_ = 0;
    std::string caption_;
    std::string cardLine_;
    std::string cardSub_;
    EasedToggle card_{18, 18};
    EasedToggle captionFade_{14, 14};
};

}  // namespace granadad::render
