#include "internal.hpp"

// KEYS, MOUSE AND THE HID VOCABULARY.

namespace granadad::render3d {

using namespace rl;

namespace {

/// raylib key (GLFW's US-layout physical key tokens) -> USB HID usage id,
/// page 7 -- SDL's scancode numbering. GLFW's tokens name the PHYSICAL key
/// as it sits on a US board, which is the same thing a scancode is.
struct KeyRow {
    int key;
    std::uint16_t hid;
};

constexpr KeyRow kKeyTable[] = {
    {KEY_A, 4},           {KEY_B, 5},           {KEY_C, 6},           {KEY_D, 7},
    {KEY_E, 8},           {KEY_F, 9},           {KEY_G, 10},          {KEY_H, 11},
    {KEY_I, 12},          {KEY_J, 13},          {KEY_K, 14},          {KEY_L, 15},
    {KEY_M, 16},          {KEY_N, 17},          {KEY_O, 18},          {KEY_P, 19},
    {KEY_Q, 20},          {KEY_R, 21},          {KEY_S, 22},          {KEY_T, 23},
    {KEY_U, 24},          {KEY_V, 25},          {KEY_W, 26},          {KEY_X, 27},
    {KEY_Y, 28},          {KEY_Z, 29},          {KEY_ONE, 30},        {KEY_TWO, 31},
    {KEY_THREE, 32},      {KEY_FOUR, 33},       {KEY_FIVE, 34},       {KEY_SIX, 35},
    {KEY_SEVEN, 36},      {KEY_EIGHT, 37},      {KEY_NINE, 38},       {KEY_ZERO, 39},
    {KEY_ENTER, 40},      {KEY_ESCAPE, 41},     {KEY_BACKSPACE, 42},  {KEY_TAB, 43},
    {KEY_SPACE, 44},      {KEY_MINUS, 45},      {KEY_EQUAL, 46},      {KEY_LEFT_BRACKET, 47},
    {KEY_RIGHT_BRACKET, 48}, {KEY_BACKSLASH, 49}, {KEY_SEMICOLON, 51}, {KEY_APOSTROPHE, 52},
    {KEY_GRAVE, 53},      {KEY_COMMA, 54},      {KEY_PERIOD, 55},     {KEY_SLASH, 56},
    {KEY_CAPS_LOCK, 57},  {KEY_F1, 58},         {KEY_F2, 59},         {KEY_F3, 60},
    {KEY_F4, 61},         {KEY_F5, 62},         {KEY_F6, 63},         {KEY_F7, 64},
    {KEY_F8, 65},         {KEY_F9, 66},         {KEY_F10, 67},        {KEY_F11, 68},
    {KEY_F12, 69},        {KEY_PRINT_SCREEN, 70}, {KEY_SCROLL_LOCK, 71}, {KEY_PAUSE, 72},
    {KEY_INSERT, 73},     {KEY_HOME, 74},       {KEY_PAGE_UP, 75},    {KEY_DELETE, 76},
    {KEY_END, 77},        {KEY_PAGE_DOWN, 78},  {KEY_RIGHT, 79},      {KEY_LEFT, 80},
    {KEY_DOWN, 81},       {KEY_UP, 82},         {KEY_NUM_LOCK, 83},   {KEY_KP_DIVIDE, 84},
    {KEY_KP_MULTIPLY, 85}, {KEY_KP_SUBTRACT, 86}, {KEY_KP_ADD, 87},   {KEY_KP_ENTER, 88},
    {KEY_KP_1, 89},       {KEY_KP_2, 90},       {KEY_KP_3, 91},       {KEY_KP_4, 92},
    {KEY_KP_5, 93},       {KEY_KP_6, 94},       {KEY_KP_7, 95},       {KEY_KP_8, 96},
    {KEY_KP_9, 97},       {KEY_KP_0, 98},       {KEY_KP_DECIMAL, 99}, {KEY_KP_EQUAL, 103},
    {KEY_KB_MENU, 118},   {KEY_LEFT_CONTROL, 224}, {KEY_LEFT_SHIFT, 225}, {KEY_LEFT_ALT, 226},
    {KEY_LEFT_SUPER, 227}, {KEY_RIGHT_CONTROL, 228}, {KEY_RIGHT_SHIFT, 229}, {KEY_RIGHT_ALT, 230},
    {KEY_RIGHT_SUPER, 231},
};

/// raylib mouse button -> SDL's numbering (1 left, 2 middle, 3 right, 4/5 the
/// side buttons).
struct MouseRow {
    int button;
    std::uint8_t sdl;
};

constexpr MouseRow kMouseTable[] = {
    {MOUSE_BUTTON_LEFT, 1}, {MOUSE_BUTTON_MIDDLE, 2}, {MOUSE_BUTTON_RIGHT, 3},
    {MOUSE_BUTTON_SIDE, 4}, {MOUSE_BUTTON_EXTRA, 5},
};

}  // namespace

InputFrame Backend::poll() {
    InputFrame in;
    in.closeRequested = WindowShouldClose();
    in.windowWidth = GetScreenWidth();
    in.windowHeight = GetScreenHeight();
    if (impl_->headless) {
        // A headless context has no keyboard, no mouse and no focus to lose.
        in.focused = true;
        return in;
    }
    in.focused = IsWindowFocused();
    for (const KeyRow& row : kKeyTable) {
        if (IsKeyPressed(row.key)) {
            in.keys.push_back(KeyEdge{row.hid, true, false});
        } else if (IsKeyPressedRepeat(row.key)) {
            in.keys.push_back(KeyEdge{row.hid, true, true});
        }
        if (IsKeyReleased(row.key)) {
            in.keys.push_back(KeyEdge{row.hid, false, false});
        }
    }
    const Vector2 position = GetMousePosition();
    for (const MouseRow& row : kMouseTable) {
        if (IsMouseButtonPressed(row.button)) {
            in.buttons.push_back(MouseButtonEdge{row.sdl, true, position.x, position.y});
        }
        if (IsMouseButtonReleased(row.button)) {
            in.buttons.push_back(MouseButtonEdge{row.sdl, false, position.x, position.y});
        }
    }
    for (int codepoint = GetCharPressed(); codepoint != 0; codepoint = GetCharPressed()) {
        in.chars.push_back(static_cast<std::uint32_t>(codepoint));
    }
    const Vector2 delta = GetMouseDelta();
    in.mouseX = position.x;
    in.mouseY = position.y;
    in.mouseDeltaX = delta.x;
    in.mouseDeltaY = delta.y;
    in.wheelY = GetMouseWheelMove();
    return in;
}

void Backend::setRelativeMouse(bool relative) {
    Impl& impl = *impl_;
    if (impl.headless || impl.relativeMouse == relative) {
        return;
    }
    impl.relativeMouse = relative;
    if (relative) {
        DisableCursor();
    } else {
        EnableCursor();
    }
}

}  // namespace granadad::render3d
