#pragma once

// The conversation surface, drawn where a conversation is allowed to be.
//
// THE HUD RULE APPLIES HERE AND IT IS THE HARD PART. "The HUD hugs all four
// edges and leaves the centre completely clear" is not a preference
// (COMBAT-FEEL-REFERENCE section 3, task #66) and it is the exact rule the
// Java build's first-person view broke: its inspector sheet and craftings bar
// ate the right half of the screen.
//
// A dialogue surface is the obvious thing to break it with, because the obvious
// design is a big box in the middle. So this one is not:
//
//     TOP BAND     who is talking, what they think of you, and what they just
//                  said. Wrapped to three lines.
//     BOTTOM BAND  the topics, in two columns, with a cursor.
//     THE MIDDLE   the person you are talking to. Untouched.
//
// That is better design as well as compliance: you look at their face while
// they talk, which is the entire reason the game is first person.
//
// dialogueCentreIsClear() exists so a test can PROVE it rather than trust it,
// and the test drives it with the longest speaker name, the longest authored
// line and a full twelve-topic list at once.

#include <cstdint>
#include <string>
#include <vector>

#include "granadad/render/framebuffer.hpp"
#include "granadad/render/hud.hpp"

namespace granadad::render {

/// Everything the surface draws. Nothing here is authoritative: the simulation
/// owns all of it and this only puts it on the screen.
struct DialogueViewState {
    bool open = false;
    /// "MASTER VENN".
    std::string speaker;
    /// "LANDLORD OF THE GILDED GULL".
    std::string epithet;
    /// "WARM", "HOSTILE", ... straight off the ledger.
    std::string attitude;
    /// What they just said. Wrapped by the view, never by the caller.
    std::string line;
    /// The topic labels, in the order the simulation built them.
    std::vector<std::string> topics;
    /// Which one the cursor is on.
    int cursor = 0;

    // --- haggling -----------------------------------------------------------
    bool haggling = false;
    /// What they are asking, and what the player is about to offer.
    int asking = 0;
    int offer = 0;
    int patience = 0;
    std::string goods;
};

/// The topic grid. Four rows is what the bottom band can hold at 640x360
/// without crossing into the exclusion rectangle -- it is a measurement, not a
/// preference -- and three columns is what it takes to show all twelve of
/// Master Venn's, who has the longest list in the game: his own business,
/// three authored micro-histories, the ward, the vanished clerk, his trade,
/// buying a bed, arguing about the price of one, standing him a drink, a hand
/// in his purse, and leaving.
inline constexpr int kTopicRows = 4;
inline constexpr int kTopicColumns = 3;
inline constexpr int kTopicSlots = kTopicRows * kTopicColumns;

/// Draws the whole surface over a rendered frame. A closed conversation draws
/// nothing at all.
void drawDialogue(Framebuffer& target, const DialogueViewState& state);

/// Breaks a line at spaces to fit `columns` characters. Never splits a word
/// unless the word alone is longer than the column.
[[nodiscard]] std::vector<std::string> wrapText(const std::string& text, std::size_t columns);

}  // namespace granadad::render
