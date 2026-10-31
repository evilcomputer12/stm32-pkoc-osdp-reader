#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#define HEX 16
inline uint32_t fake_time;
inline uint32_t millis() { return fake_time; }
inline void delay(unsigned ms) { fake_time += ms; }
struct FakeSerial {
    template<class... T> void print(T...) {}
    template<class... T> void println(T...) {}
};
inline FakeSerial Serial;
