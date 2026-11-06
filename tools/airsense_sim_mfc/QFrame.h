#pragma once

// AirSense 10 / AirBridge qframe codec.
//
//   [0x55] [type] [len:3 hex-ASCII] [payload, 0x55-escaped] [crc:4 hex-ASCII]
//
// len counts every wire byte (sync..CRC) after escaping. CRC-16/CCITT-FALSE
// (poly 0x1021, init 0xFFFF, MSB first, no final XOR) covers sync..payload
// on the escaped wire bytes. Mirrors src/qframe.cpp / src/crc.cpp.

#include <string>
#include <vector>
#include <cstdint>

namespace qf {

using Bytes = std::vector<uint8_t>;

const uint8_t SYNC = 0x55;
const size_t  MIN_RAW = 9;      // 1 sync + 1 type + 3 length + 4 CRC
const size_t  MAX_RAW = 600;
const size_t  MAX_PAYLOAD = 502;

struct Frame {
    char        type = 0;
    std::string payload;        // unescaped
    Bytes       raw;            // complete wire bytes
    bool        immediate = false;
    bool        crcOk = true;
};

uint16_t    Crc16(const uint8_t* data, size_t len, uint16_t crc = 0xFFFF);
Bytes       Escape(const Bytes& payload);
Bytes       Unescape(const uint8_t* data, size_t len);
Bytes       Build(char type, const std::string& payload);
Bytes       BuildBytes(char type, const Bytes& payload);
Bytes       Immediate(char type);           // 0x55 'O' / 0x55 'P'

// Decode one complete raw frame (used for frames we generated ourselves).
bool        ParseOne(const Bytes& raw, Frame& out);

class Parser {
public:
    std::vector<Frame> Feed(const uint8_t* data, size_t len);
    std::vector<std::string> TakeErrors();
    int  ErrorCount() const { return m_errorCount; }
    void Reset() { m_buf.clear(); m_errors.clear(); m_errorCount = 0; }

private:
    enum class Step { Wait, Frame, Skip };
    Step Next(Frame& out, size_t& consumed);

    Bytes m_buf;
    std::vector<std::string> m_errors;
    int m_errorCount = 0;
};

std::string HexDump(const Bytes& data, size_t maxBytes = 32);
std::string Printable(const std::string& data);   // non-printables -> '.'
bool        IsHexChar(uint8_t c);
std::string ToUpperHex(unsigned value, int width);

} // namespace qf
