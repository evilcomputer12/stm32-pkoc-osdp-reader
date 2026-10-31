#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// A lost selected-target session is not proof that a physical card left.
// UID is used only for duplicate suppression, never for authentication.
struct PresentationGuard {
    bool held=false, refind=false;
    uint8_t uid[7]={}, uid_length=0, misses=0, absent_samples=0;
    uint32_t absent_since=0, reselections=0;
    uint8_t reported_credential[32]={};
    bool credential_reported=false;
    void reset() { *this={}; }
    void clear_absence() { misses=0; absent_samples=0; }
    void selected_present() { clear_absence(); }
    void error() { clear_absence(); }
    bool selected_timeout() {
        if (++misses < 3) return false;
        misses=0; absent_samples=0; refind=true;
        return true; // Restart discovery, but keep the presentation held.
    }
    void discovery_absent(uint32_t now) {
        if (!held || !refind) return;
        if (!absent_samples) absent_since=now;
        if (absent_samples<255) ++absent_samples;
        if (absent_samples>=3 && uint32_t(now-absent_since)>=1000) {
            held=false; refind=false; uid_length=0; clear_absence();
            credential_reported=false; memset(reported_credential,0,sizeof(reported_credential));
        }
    }
    bool discovery_found(const uint8_t *found,size_t length) {
        if (!found || !length || length>sizeof(uid)) { error(); return false; }
        const bool same=held && length==uid_length && !memcmp(uid,found,length);
        if (same) ++reselections;
        memcpy(uid,found,length); uid_length=length;
        held=true; refind=false; clear_absence();
        return !same;
    }
    bool was_reported(const uint8_t *credential,size_t length) const {
        return credential && length==sizeof(reported_credential) && credential_reported &&
            !memcmp(reported_credential,credential,length);
    }
    void note_reported(const uint8_t *credential,size_t length) {
        if (!credential || length!=sizeof(reported_credential)) return;
        memcpy(reported_credential,credential,length); credential_reported=true;
    }
};
