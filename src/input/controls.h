#pragma once
#include "platform/types.h"

namespace smallcap
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
    Button sword;   // Z / J / Space, pad South
    Button action;  // X / K / Enter, pad East
    Button roll;    // C / L / Shift, pad West
    Button start;   // Enter / P, pad Start
};

void PollControls(Controls &controls);
// Clears the edge flags so one press is handled by one fixed tick only.
void ConsumePresses(Controls &controls);
} // namespace smallcap
