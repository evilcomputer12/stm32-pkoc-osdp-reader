#pragma once

// ATECC always uses I2C1 on PB6/PB7. The split build gives PN532 I2C2.
#if defined(READER_SPLIT_I2C) && defined(READER_SOFT_I2C)
#error "The split build uses two independent hardware I2C controllers"
#endif
#ifdef READER_PN532_HSU
#define READER_PN532_SCL PB10
#define READER_PN532_SDA PB11
#define READER_PN532_BUS "HSU B10-TX/B11-RX"
#elif defined(READER_SPLIT_I2C)
#define READER_PN532_SCL PB10
#define READER_PN532_SDA PB11
#ifdef READER_PN532_SOFT_I2C
#ifdef READER_I2C_WEAK_PULLUPS
#define READER_PN532_BUS "GPIO B10/B11 pull-up"
#else
#define READER_PN532_BUS "GPIO B10/B11 stream"
#endif
#else
#define READER_PN532_BUS "I2C2 B10/B11"
#endif
#else
#define READER_PN532_SCL PB6
#define READER_PN532_SDA PB7
#define READER_PN532_BUS "I2C1 B6/B7"
#endif
