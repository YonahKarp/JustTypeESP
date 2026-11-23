#include "backlight.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "nvs.h"
#include <cassert>
#include <iostream>
using E=platform::PlatformEvent;
using K=platform::KeyCode;
int main() {
    // An old 20 kHz preference of 30% must not restore a black screen.
    assert(backlight::init(2));
    assert(backlight::percent()==100 && fake_duty==1024);
    assert(fake_timer.freq_hz==300 && fake_timer.duty_resolution==10 && fake_timer.clk_cfg==LEDC_USE_XTAL_CLK);
    E event{}; event.type=E::Type::KeyDown; event.key=K::Minus; event.modifiers.alt=true;
    assert(backlight::handle_event(event) && backlight::percent()==90);
    event.key=K::Equals;
    assert(backlight::handle_event(event) && backlight::percent()==100);
    event.modifiers.ctrl=true;
    assert(!backlight::handle_event(event) && backlight::percent()==100);
    event.modifiers.ctrl=false; event.modifiers.meta=true;
    assert(!backlight::handle_event(event));
    event.modifiers.meta=false; event.type=E::Type::TextInput; event.hid_usage=0x2e;
    assert(backlight::handle_event(event) && backlight::percent()==100);
    event.hid_usage=4;
    assert(!backlight::handle_event(event));

    // Each direction changes exactly ten percentage points over the full range.
    for (int expected=90; expected>=10; expected-=10) {
        assert(backlight::adjust(-10) && backlight::percent()==expected);
        assert(fake_duty==unsigned((expected*1024+50)/100));
    }
    for (int expected=20; expected<=100; expected+=10)
        assert(backlight::adjust(10) && backlight::percent()==expected);

    // A remapped Opt shortcut may have the logical Minus action but a different physical key.
    event.type=E::Type::KeyDown; event.key=K::Minus; event.hid_usage=0x14;
    assert(!backlight::handle_event(event) && backlight::percent()==100);
    event.hid_usage=0x2d;
    assert(backlight::handle_event(event) && backlight::percent()==90);
    assert(backlight::adjust(10));

    assert(backlight::adjust(-1000) && backlight::percent()==10 && fake_duty==102);
    const int updates=fake_updates;
    assert(backlight::adjust(-10) && fake_updates==updates);
    assert(backlight::adjust(1000) && fake_duty==1024);
    assert(backlight::adjust(-60)); // 40%
    fake_time_us=500000; backlight::service(); assert(fake_commits==0);
    assert(backlight::adjust(-10)); // 30%; restart debounce
    fake_time_us=800000; backlight::service(); assert(fake_commits==0);
    fake_time_us=1500000; backlight::service(false); assert(fake_commits==0);
    backlight::service(); assert(fake_commits==1 && fake_saved_value==30);
    assert(backlight::init(2) && backlight::percent()==30 && fake_duty==307); // reboot

    assert(backlight::adjust(10)); fake_time_us+=800000;
    fake_nvs_error=ESP_FAIL; backlight::service();
    assert(fake_commits==2 && fake_saved_value==30);
    fake_nvs_error=0; fake_time_us+=4000000; backlight::service();
    assert(fake_commits==2);
    fake_time_us+=1000000; backlight::service();
    assert(fake_commits==3 && fake_saved_value==40);
    fake_update_error=ESP_FAIL;
    assert(!backlight::adjust(-10) && backlight::percent()==40 && fake_duty==410);
    fake_update_error=0;
    assert(backlight::adjust(10)); backlight::shutdown();
    assert(fake_saved_value==50 && fake_stops==1);

    fake_saved_value=0;
    assert(backlight::init(2) && backlight::percent()==100 && fake_duty==1024);
    fake_saved_value=255;
    assert(backlight::init(2) && backlight::percent()==100);
    fake_timer_error=ESP_FAIL;
    assert(!backlight::init(2) && fake_gpio_level==1 && backlight::percent()==100);
    assert(!backlight::adjust(-10));
    event.type=E::Type::KeyDown; event.key=K::Minus; event.hid_usage=0x2d;
    assert(backlight::handle_event(event)); // consume shortcut even on hardware failure
    std::cout << "Backlight PWM, bounds, shortcuts, persistence, retries and fallback passed\n";
}
