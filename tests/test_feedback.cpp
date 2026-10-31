#include <cassert>
#include <cstring>
#include <cstdio>
#include "feedback_state.h"

static void encode(const osdp_led_record_t &record, uint8_t *out) {
    size_t written = 0;
    assert(osdp_led_build(&record, 1, out, 14, &written) == OSDP_OK && written == 14);
}
int main() {
    FeedbackState state;
    uint8_t payload[28] = {};
    osdp_led_record_t on = {};
    on.temp_control_code = 2; on.temp_on_color = OSDP_LED_GREEN;
    on.temp_on_time = 1; on.temp_timer_100ms = 20;
    encode(on, payload);
    assert(state.apply_led(payload,14,100,false) != OSDP_OK && !state.led_on(100));
    assert(state.apply_led(payload,14,100,true) == OSDP_OK);
    assert(state.led_on(100) && state.led_on(2099) && !state.led_on(2100));
    // Invalid later record must not partially change the physical output.
    state.disconnected(); encode(on,payload); auto invalid=on; invalid.led_no=1;
    encode(invalid,payload+14);
    assert(state.apply_led(payload,28,0,true) != OSDP_OK && !state.led_on(0));
    invalid=on; invalid.temp_on_color=OSDP_LED_RED; encode(invalid,payload);
    assert(state.apply_led(payload,14,0,true) != OSDP_OK);
    invalid=on; invalid.temp_control_code=3; encode(invalid,payload);
    assert(state.apply_led(payload,14,0,true) != OSDP_OK);
    assert(state.apply_led(payload,13,0,true) != OSDP_OK);
    assert(state.apply_led(nullptr,14,0,true) != OSDP_OK);
    // NOP fields are ignored even if their unused bytes are unsupported.
    auto permanent=on; permanent.temp_control_code=0;
    permanent.temp_on_color=255; permanent.perm_control_code=1;
    permanent.perm_on_color=OSDP_LED_GREEN; permanent.perm_on_time=1;
    encode(permanent,payload); assert(state.apply_led(payload,14,0,true)==OSDP_OK && state.led_on(5000));
    on.temp_on_color=OSDP_LED_BLACK; on.temp_timer_100ms=1;
    encode(on,payload); assert(state.apply_led(payload,14,10,true)==OSDP_OK);
    assert(!state.led_on(109) && state.led_on(110));
    state.disconnected(); assert(!state.led_on(111));
    on.temp_on_color=OSDP_LED_GREEN; on.temp_on_time=1; on.temp_off_time=1;
    encode(on,payload); assert(state.apply_led(payload,14,0xfffffff0U,true)==OSDP_OK);
    assert(state.led_on(0xfffffff0U) && !state.led_on(0x54U) && !state.led_on(0xb8U)); // Timer expired.
    state.disconnected(); state.card_read(0xfffffff0U);
    assert(state.sounding(0xfffffff0U) && state.sounding(0x53U) && !state.sounding(0x54U));
    state.advance(0x54U); assert(!state.sounding(0xfffffff0U)); // No retrigger next clock epoch.
    assert(!state.led_on(0x20U)); // A verified card never grants access locally.
    const uint8_t buz[]={0,2,1,1,2};
    assert(state.apply_buzzer(buz,5,0,true,false)!=OSDP_OK); // Disabled output: never ACK.
    assert(state.apply_buzzer(buz,5,0,false,true)!=OSDP_OK);
    assert(state.apply_buzzer(buz,5,0,true,true)==OSDP_OK);
    assert(state.sounding(0) && !state.sounding(100) && state.sounding(200) && !state.sounding(400));
    const uint8_t off[]={0,1,0,0,0};
    state.card_read(10); assert(state.apply_buzzer(off,5,11,true,true)==OSDP_OK && !state.sounding(11));
    uint8_t bad[]={1,2,1,0,0}; assert(state.apply_buzzer(bad,5,0,true,true)!=OSDP_OK);
    bad[0]=0; bad[1]=9; assert(state.apply_buzzer(bad,5,0,true,true)!=OSDP_OK);
    assert(state.apply_buzzer(buz,4,0,true,true)!=OSDP_OK);
    state.card_read(100); state.disconnected();
    assert(!state.sounding(100) && !state.led_on(100));
    puts("PASS: authenticated LED commands, whole-batch validation, timings, wraparound, no local access grant, disconnect clearing and disabled-output buzzer rejection");
}
