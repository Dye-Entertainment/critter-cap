#include "input/controls.h"

#include "input/gamepad.h"
#include "input/keyboard.h"
#include "input/manager.h"

#include "compiled/warning_off.h"
#include <initializer_list>
#include "compiled/warning_on.h"

namespace smallcap
{
namespace
{
using Key = input::KeyboardKey;
using Pad = input::Gamepad::Buttons;

void Update(Button &button, const Bool is_down)
{
    // Edges accumulate until a fixed tick consumes them, so short taps between ticks count.
    button.pressed |= is_down && !button.down;
    button.released |= !is_down && button.down;
    button.down = is_down;
}

Bool AnyKey(const input::Keyboard *keyboard, const std::initializer_list<Key> keys)
{
    if (!keyboard)
    {
        return false;
    }
    for (const Key key : keys)
    {
        if (input::IsKeyDown(*keyboard, key))
        {
            return true;
        }
    }
    return false;
}

Bool AnyPad(const input::Gamepad *pad, const std::initializer_list<Pad> buttons)
{
    if (!pad)
    {
        return false;
    }
    for (const Pad button : buttons)
    {
        if (input::IsButtonDown(*pad, button))
        {
            return true;
        }
    }
    return false;
}
} // namespace

void PollControls(Controls &controls)
{
    auto *manager = input::Manager::GetInstance();
    const input::Keyboard *keyboard = nullptr;
    if (auto keyboard_opt = manager->GetKeyboard(); keyboard_opt.has_value())
    {
        keyboard = keyboard_opt.value();
    }
    const input::Gamepad *pad = nullptr;
    if (auto pad_opt = manager->GetGamepad(0); pad_opt.has_value())
    {
        pad = pad_opt.value();
    }
    Update(controls.left, AnyKey(keyboard, {Key::Left, Key::A}) || AnyPad(pad, {Pad::DpadLeft}));
    Update(controls.right, AnyKey(keyboard, {Key::Right, Key::D}) || AnyPad(pad, {Pad::DpadRight}));
    Update(controls.up, AnyKey(keyboard, {Key::Up, Key::W}) || AnyPad(pad, {Pad::DpadTop}));
    Update(controls.down, AnyKey(keyboard, {Key::Down, Key::S}) || AnyPad(pad, {Pad::DpadBottom}));
    Update(controls.sword, AnyKey(keyboard, {Key::Z, Key::J, Key::Space}) || AnyPad(pad, {Pad::SouthButton}));
    Update(controls.action, AnyKey(keyboard, {Key::X, Key::K}) || AnyPad(pad, {Pad::EastButton}));
    Update(
        controls.roll,
        AnyKey(keyboard, {Key::C, Key::L, Key::LShift, Key::RShift}) || AnyPad(pad, {Pad::WestButton})
    );
    Update(controls.start, AnyKey(keyboard, {Key::Enter, Key::P}) || AnyPad(pad, {Pad::Start}));
}

void ConsumePresses(Controls &controls)
{
    for (Button *b : {&controls.left, &controls.right, &controls.up, &controls.down, &controls.sword,
                      &controls.action, &controls.roll, &controls.start})
    {
        b->pressed = false;
        b->released = false;
    }
}
} // namespace smallcap
