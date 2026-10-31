#pragma once
// Active-HIGH PB0 buzzer control. The bench reader explicitly enables this
// for the user's 3.3 V HW-512 setup. Other builds leave PB0 as an input.
// This flag does not assert that a driver is fitted or verify GPIO load current.
// Active buzzers use switched DC; no PWM is required.
#ifndef READER_BUZZER_ENABLED
#define READER_BUZZER_ENABLED 0
#endif
