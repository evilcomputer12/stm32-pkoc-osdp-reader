#include <cassert>
#include <cstdio>
#include "presentation_guard.h"
int main() {
    PresentationGuard state;
    const uint8_t card[]={1,2,3,4}, other[]={4,3,2,1};
    uint8_t credential[32]={1}, different[32]={2};
    assert(state.discovery_found(card,4) && state.held);
    assert(!state.was_reported(credential,32)); state.note_reported(credential,32);
    assert(state.was_reported(credential,32) && !state.was_reported(different,32));
    assert(!state.selected_timeout() && !state.selected_timeout());
    assert(state.selected_timeout() && state.held && state.refind);
    assert(!state.discovery_found(card,4) && state.held && !state.refind && state.reselections==1);
    assert(state.was_reported(credential,32));
    // A randomized UID for the same verified credential cannot create an event.
    assert(state.discovery_found(other,4) && state.was_reported(credential,32));
    state.discovery_found(card,4);
    // Repeated session loss while the same card remains must never re-report.
    for(unsigned i=0;i<5;++i) {
        state.selected_timeout(); state.selected_timeout(); state.selected_timeout();
        assert(!state.discovery_found(card,4));
    }
    state.selected_timeout(); state.selected_timeout(); state.selected_timeout();
    state.discovery_absent(100); state.discovery_absent(350); state.discovery_absent(600);
    assert(state.held); // Three fast samples alone are insufficient.
    state.discovery_absent(1100); assert(!state.held);
    assert(!state.was_reported(credential,32));
    assert(state.discovery_found(card,4)); // True absence permits same card again.
    state.selected_timeout(); state.error();
    assert(!state.selected_timeout() && !state.selected_timeout());
    assert(state.selected_timeout());
    state.discovery_absent(2000); state.discovery_absent(2500); state.error();
    state.discovery_absent(3000); state.discovery_absent(3500); assert(state.held);
    state.discovery_absent(4000); assert(!state.held);
    state.discovery_found(card,4);
    state.selected_timeout(); state.selected_timeout(); state.selected_timeout();
    assert(state.discovery_found(other,4)); // A different card is a new presentation.
    assert(!state.discovery_found(nullptr,4) && state.held);
    state.selected_timeout(); state.selected_timeout(); state.selected_timeout();
    state.discovery_absent(0xfffffff0U); state.discovery_absent(0x1e4U); state.discovery_absent(0x3d8U);
    assert(!state.held); // Clock wrap preserves the one-second absence interval.
    state.reset(); assert(!state.held && !state.refind && state.reselections==0);
    puts("PASS: session loss does not re-report held card, confirmed discovery absence rearms, errors do not prove absence, changed card and clock wrap");
}
