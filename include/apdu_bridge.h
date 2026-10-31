#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "asymcred/asymcred_types.h"

// Seeed uses uint8_t capacities; AsymCred uses size_t and may pass 256.
// Narrow only this explicitly bounded scratch capacity, never caller capacity.
template <typename Device>
asymcred_status_t reader_apdu_exchange(Device &device, const uint8_t *command,
    size_t command_length, uint8_t *response, size_t capacity, size_t *written) {
    if (!written) return ASYMCRED_ERR_INVALID_ARG;
    *written = 0;
    if (!command || !response || command_length == 0 || command_length > 251)
        return ASYMCRED_ERR_INVALID_ARG;
    uint8_t scratch[160];
    uint8_t count = sizeof(scratch);
    if (!device.inDataExchange(command, static_cast<uint8_t>(command_length), scratch, &count))
        return ASYMCRED_ERR_CARD_STATUS;
    if (count < 2 || count >= sizeof(scratch)) return ASYMCRED_ERR_BAD_LENGTH;
    if (count > capacity) return ASYMCRED_ERR_BUFFER_TOO_SMALL;
    memcpy(response, scratch, count); // Includes the card's SW1/SW2.
    *written = count;
    return ASYMCRED_OK;
}
