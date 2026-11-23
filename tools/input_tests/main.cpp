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
    // Both new file actions use physical Ctrl shortcuts, without text events.
    for (auto usage : {uint8_t(0x11), uint8_t(0x12)}) {
        report(1, usage);
        report(0, 0); // Releasing Ctrl first must still release the action key.
        const auto action = drain();
        assert(action.size() == 2);
        assert(action[0].type == Type::KeyDown && action[0].modifiers.ctrl);
        assert(action[0].key == (usage == 0x11 ? Key::N : Key::O));
        assert(action[1].type == Type::KeyUp && action[1].key == action[0].key);
        assert(!keyboard_input::pressed(action[0].key));
    }
    // A mapped Option shortcut emits its logical action and no AltGr text.
    platform::KeyModifiers opt{}; opt.alt = true;
    assert(keyboard_config::bind(0, 0x14, opt));
    report(0x40, 0x14); report(0, 0);
    auto mapped = drain();
    assert(mapped.size() == 2 && mapped[0].shortcut && mapped[0].key == Key::F);
    assert(mapped[1].type == Type::KeyUp && mapped[1].key == Key::F);
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

    // Overlapping navigation keys must each produce a press, and releasing the
    // older key must not cancel the newer held key.
    report(0, 0x50);
    assert(keyboard_input::poll(event, 0));
    assert(!keyboard_input::release_lvgl_before(event));
    assert(keyboard_input::forward_to_lvgl(event));
    keyboard_input::read_lvgl(&data);
    const uint8_t overlap[8] = {0, 0, 0x50, 0x4f, 0, 0, 0, 0};
    keyboard_input::submit_report(overlap, sizeof(overlap));
    assert(keyboard_input::poll(event, 0) && event.hid_usage == 0x4f);
    assert(keyboard_input::release_lvgl_before(event));
    keyboard_input::read_lvgl(&data);
    assert(data.key == LV_KEY_LEFT && data.state == LV_INDEV_STATE_RELEASED);
    assert(keyboard_input::forward_to_lvgl(event));
    keyboard_input::read_lvgl(&data);
    assert(data.key == LV_KEY_RIGHT && data.state == LV_INDEV_STATE_PRESSED);
    report(0, 0x4f); // release left while right stays held
    assert(keyboard_input::poll(event, 0) && event.hid_usage == 0x50);
    assert(!keyboard_input::forward_to_lvgl(event));
    keyboard_input::read_lvgl(&data);
    assert(data.key == LV_KEY_RIGHT && data.state == LV_INDEV_STATE_PRESSED);
    report(0, 0);
    assert(keyboard_input::poll(event, 0));
    assert(keyboard_input::forward_to_lvgl(event));

    // The attached keyboard repeats Right Arrow in two slots. Emit only one
    // down and one up, including if the duplicate slots move while held.
    for (uint8_t usage : {uint8_t(0x4f), uint8_t(0x50), uint8_t(4)}) {
        const uint8_t duplicated[8] = {0, 0, usage, usage, 0, 0, 0, 0};
        keyboard_input::submit_report(duplicated, sizeof(duplicated));
        const auto press = drain();
        assert(press.size()==(usage==4 ? 2 : 1));
        assert(press[0].type==Type::KeyDown && press[0].hid_usage==usage);
        const uint8_t moved[8] = {0, 0, 0, usage, 0, usage, 0, 0};
        keyboard_input::submit_report(moved, sizeof(moved));
        assert(drain().empty());
        report(0,0);
        const auto release = drain();
        assert(release.size()==1 && release[0].type==Type::KeyUp && release[0].hid_usage==usage);
    }

    // Disconnect is queued after the press; replay clears both key and modifier.
    report(1, 4); keyboard_input::disconnected();
    drain();
    assert(!keyboard_input::pressed(Key::A) && !keyboard_input::modifiers().ctrl);

    // Command must produce fresh edges even when release is consumed inside
    // Settings. Holding either/both Command keys must not repeatedly reopen it.
    report(0x08, 0); report(0x88, 0); report(0x80, 0); report(0, 0);
    assert(keyboard_input::poll(event, 0) && event.type == Type::KeyDown && event.key == Key::Meta);
    assert(event.modifiers.meta);
    assert(!keyboard_input::poll(event, 0)); // second Meta still held
    assert(!keyboard_input::poll(event, 0)); // first Meta released, second held
    assert(keyboard_input::poll(event, 0) && event.type == Type::KeyUp && event.key == Key::Meta);
    assert(!event.modifiers.meta);
    report(0x08, 0); report(0, 0);
    assert(keyboard_input::poll(event, 0) && event.type == Type::KeyDown && event.key == Key::Meta);
    assert(keyboard_input::poll(event, 0) && event.type == Type::KeyUp && event.key == Key::Meta);

    // Both printable forms of the menu key carry the same physical usage, so
    // the UI can consume their text without swallowing unrelated typing.
    report(0, 0x35); report(0, 0); report(2, 0x35); report(0, 0);
    const auto grave = drain();
    assert(grave.size() == 6);
    assert(grave[0].key == Key::Grave && grave[3].key == Key::Grave);
    assert(grave[1].type == Type::TextInput && grave[1].hid_usage == 0x35 && std::string(grave[1].text) == "`");
    assert(grave[4].type == Type::TextInput && grave[4].hid_usage == 0x35 && std::string(grave[4].text) == "~");
    report(0, 0x29); report(0, 0);
    const auto escape = drain();
    assert(escape.size() == 2 && escape[0].key == Key::Escape && escape[1].type == Type::KeyUp);
    std::cout << "Duplicate HID slots emit exactly one press and release per key\n";
    std::cout << "Buffered HID order, queue pressure, releases and disconnect passed\n";
    std::cout << "Command modal-release edges and Escape/backtick/tilde sequences passed\n";
}
