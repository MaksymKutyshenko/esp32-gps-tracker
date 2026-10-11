#pragma once

#include <stddef.h>
#include <stdint.h>

#include "config.hpp"
#include "logic/NmeaLineAssembler.hpp"
#include "logic/UbxAck.hpp"
#include "periph/GnssProfile.hpp"
#include "periph/UartPort.hpp"

// Delivers complete NMEA sentences from the receiver's UART. Bytes are read
// in chunks (one driver call per chunk, not per byte) and fed to the
// assembler; a chunk that contains the end of one sentence and the start
// of the next is kept and resumed on the following call, so nothing is
// lost between sentences.
//
// Knows nothing about the sentence contents: parsing is NmeaParser's job.
class GnssReceiver
{
public:
    explicit GnssReceiver(UartPort &uart);

    void begin(const GnssProfile &profile);

    // Waits up to timeoutMs for one sentence. On true, `line` points at the
    // NUL-terminated sentence and stays valid until the next call.
    bool readSentence(const char *&line, uint32_t timeoutMs);

    uint32_t overflows() const { return assembler_.overflows(); }

private:
    // Waits for the receiver's answer to one setup frame.
    UbxAckScanner::Result awaitAck(uint8_t messageClass, uint8_t messageId);

    UartPort &uart_;
    NmeaLineAssembler<Config::gpsLineBytes> assembler_;
    uint8_t chunk_[64] = {};
    size_t chunkLength_ = 0;
    size_t chunkIndex_ = 0;
};
