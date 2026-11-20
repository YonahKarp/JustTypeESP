#pragma once
#include "justtype/platforms/platform.h"
namespace spelling {
enum class TooltipAction { Pass, Consume, Open, Accept, Dismiss, Up, Down };
class TooltipInput {
    uint8_t swallowed_ = 0;

  public:
    TooltipAction route(const platform::PlatformEvent &event, bool active,
                        bool can_open) {
        using T = platform::PlatformEvent::Type;
        if (swallowed_ && event.hid_usage == swallowed_) {
            if (event.type == T::KeyUp) {
                swallowed_ = 0;
                return TooltipAction::Consume;
            }
            if (event.type == T::TextInput)
                return TooltipAction::Consume;
        }
        if (active) {
            if (event.type == T::KeyDown) {
                swallowed_ = event.hid_usage;
                if (event.key == platform::KeyCode::Enter) return TooltipAction::Accept;
                if (event.key == platform::KeyCode::Up) return TooltipAction::Up;
                if (event.key == platform::KeyCode::Down) return TooltipAction::Down;
                return TooltipAction::Dismiss;
            }
            return event.type == T::TextInput ? TooltipAction::Dismiss
                                              : TooltipAction::Consume;
        }
        if (can_open && event.type == T::KeyDown && event.hid_usage == 0x2c &&
            event.modifiers.ctrl && !event.modifiers.alt &&
            !event.modifiers.meta) {
            swallowed_ = event.hid_usage;
            return TooltipAction::Open;
        }
        return TooltipAction::Pass;
    }
};
} // namespace spelling
