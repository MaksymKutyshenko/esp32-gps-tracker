#pragma once

#include <stdint.h>

// One modem interaction as a value: what to send, what final response
// means success, and how long to wait. Sequences (power-up checks, GPRS
// bearer set-up, an HTTP request) are arrays of these walked by
// Sim800Modem::executeSequence, so the modem's state changes read as a
// list instead of nested ifs with magic timeouts.
struct AtCommand
{
    const char *command;   // without the trailing CR
    const char *expect;    // substring that completes the response, e.g. "OK"
    uint32_t timeoutMs;
};
