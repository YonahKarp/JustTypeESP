#pragma once
#include "justtype/platforms/platform.h"

// UI-task owned. PWM runs independently in hardware after configuration.
namespace backlight {
bool init(int gpio);
int percent();
bool adjust(int delta);
bool handle_event(const platform::PlatformEvent& event);
void service(bool allow_save = true);
void shutdown();
}
