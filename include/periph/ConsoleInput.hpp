#pragma once

#include <stddef.h>
#include <stdint.h>

// Line input from whatever carries the log console: UART0 on the DevKitC,
// the USB Serial/JTAG port on the SuperMini (selected by the BOARD_*
// define). Output still goes through ESP_LOGx; this class only reads, and
// its driver install routes the VFS console through the same driver so
// reads and log writes do not fight over the port.
class ConsoleInput
{
public:
    void begin();

    // Collects characters until LF (CR dropped), echoing is left to the
    // terminal. Returns true with a NUL-terminated line, false when
    // timeoutMs passed without a complete line; partial input is kept for
    // the next call.
    bool readLine(char *line, size_t capacity, uint32_t timeoutMs);

private:
    size_t read(uint8_t *buffer, size_t capacity, uint32_t timeoutMs);

    char pending_[96] = {};
    size_t pendingLength_ = 0;
};
