#pragma once
#include "platform/types.h"

namespace crittercap
{
struct Button
{
    Bool down = false;
    Bool pressed = false;
    Bool released = false;
};

struct Controls
{
    Button left;
    Button right;
    Button up;
    Button down;
    Button a;      // Z / J / Space, pad South: talk, confirm
    Button b;      // X / K / C / Shift / Backspace, pad East or West: cancel, hold to run
    Button start;  // Enter / P, pad Start: menu
};

void PollControls(Controls &controls);
// Clears the edge flags so one press is handled by one fixed tick only.
void ConsumePresses(Controls &controls);
} // namespace crittercap
