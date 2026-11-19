#include "keyboard_input.h"
#include "keyboard_config.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
bool replace_saved_file(const std::string&, const std::string&) { return true; }
using Event = platform::PlatformEvent;
using Type = Event::Type;
using Key = platform::KeyCode;
void report(uint8_t modifiers, uint8_t key) {
    const uint8_t bytes[8] = {modifiers, 0, key, 0, 0, 0, 0, 0};
    keyboard_input::submit_report(bytes, sizeof(bytes));
}
std::vector<Event> drain() {
    std::vector<Event> events;
    Event event;
    // These test sequences contain a key edge in each queued report.
    while (keyboard_input::poll(event, 0)) events.push_back(event);
    return events;
}
int main() {
    assert(keyboard_input::init());
    keyboard_config::reset();
    // During a slide reports are queued without polling. State must not advance.
    report(2, 4); // Shift+A
    report(2, 0); // release A, Shift still held
    report(0, 0); // release Shift
    assert(!keyboard_input::pressed(Key::A));
    assert(!keyboard_input::modifiers().shift);
    Event event;
    assert(keyboard_input::poll(event, 0) && event.type == Type::KeyDown);
    assert(keyboard_input::pressed(Key::A) && event.modifiers.shift);
    assert(keyboard_input::poll(event, 0) && event.type == Type::TextInput);
    assert(std::string(event.text) == "A");
    assert(keyboard_input::poll(event, 0) && event.type == Type::KeyUp);
    assert(!keyboard_input::pressed(Key::A));
    assert(!keyboard_input::poll(event, 0));
    assert(!keyboard_input::modifiers().shift);

    // Queue pressure is visible before capacity is reached. All 32 key presses
    // and releases are subsequently delivered in order, without dropped text.
    for (unsigned i=0; i<32; ++i) { report(0, 4); report(0, 0); }
    assert(keyboard_input::backlog_high());
    const auto events = drain();
    assert(events.size() == 96);
    for (size_t i=0; i<events.size(); i+=3) {
        assert(events[i].type == Type::KeyDown && events[i].hid_usage == 4);
        assert(events[i+1].type == Type::TextInput && std::string(events[i+1].text) == "a");
        assert(events[i+2].type == Type::KeyUp && events[i+2].hid_usage == 4);
    }
    assert(!keyboard_input::backlog_high() && !keyboard_input::pressed(Key::A));

    // A navigation edge followed by release survives deferred native delivery.
    report(0, 0x4f); report(0, 0);
    assert(keyboard_input::poll(event, 0));
    assert(keyboard_input::forward_to_lvgl(event));
    lv_indev_data_t data{};
    keyboard_input::read_lvgl(&data);
    assert(data.key == LV_KEY_RIGHT && data.state == LV_INDEV_STATE_PRESSED);
    assert(keyboard_input::poll(event, 0));
    assert(keyboard_input::forward_to_lvgl(event));
    keyboard_input::read_lvgl(&data);
    assert(data.state == LV_INDEV_STATE_RELEASED);

    // Disconnect is queued after the press; replay clears both key and modifier.
    report(1, 4); keyboard_input::disconnected();
    drain();
    assert(!keyboard_input::pressed(Key::A) && !keyboard_input::modifiers().ctrl);
    std::cout << "Buffered HID order, queue pressure, releases and disconnect passed\n";
}
