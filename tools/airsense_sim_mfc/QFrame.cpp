#include "pch.h"
#include "QFrame.h"

namespace qf {

uint16_t Crc16(const uint8_t* data, size_t len, uint16_t crc)
{
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x8000)
                crc = static_cast<uint16_t>((crc << 1) ^ 0x1021);
            else
                crc = static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

bool IsHexChar(uint8_t c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

static int HexValue(uint8_t c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static char HexDigit(unsigned value)
{
    static const char* digits = "0123456789ABCDEF";
    return digits[value & 0xF];
}

std::string ToUpperHex(unsigned value, int width)
{
    std::string out(static_cast<size_t>(width), '0');
    for (int i = width - 1; i >= 0; --i) {
        out[static_cast<size_t>(i)] = HexDigit(value);
        value >>= 4;
    }
    return out;
}

Bytes Escape(const Bytes& payload)
{
    Bytes out;
    out.reserve(payload.size() * 2);
    for (uint8_t c : payload) {
        out.push_back(c);
        if (c == SYNC) out.push_back(SYNC);
    }
    return out;
}

Bytes Unescape(const uint8_t* data, size_t len)
{
    Bytes out;
    out.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        if (data[i] == SYNC && i + 1 < len && data[i + 1] == SYNC) {
            out.push_back(SYNC);
            ++i;
        } else {
            out.push_back(data[i]);
        }
    }
    return out;
}

Bytes BuildBytes(char type, const Bytes& payload)
{
    Bytes wire = Escape(payload);
    size_t total = MIN_RAW + wire.size();
    if (total > MAX_RAW) return Bytes();

    Bytes head;
    head.reserve(5 + wire.size());
    head.push_back(SYNC);
    head.push_back(static_cast<uint8_t>(type));
    head.push_back(static_cast<uint8_t>(HexDigit(static_cast<unsigned>(total >> 8))));
    head.push_back(static_cast<uint8_t>(HexDigit(static_cast<unsigned>(total >> 4))));
    head.push_back(static_cast<uint8_t>(HexDigit(static_cast<unsigned>(total))));
    head.insert(head.end(), wire.begin(), wire.end());

    uint16_t crc = Crc16(head.data(), head.size());
    head.push_back(static_cast<uint8_t>(HexDigit(static_cast<unsigned>(crc >> 12))));
    head.push_back(static_cast<uint8_t>(HexDigit(static_cast<unsigned>(crc >> 8))));
    head.push_back(static_cast<uint8_t>(HexDigit(static_cast<unsigned>(crc >> 4))));
    head.push_back(static_cast<uint8_t>(HexDigit(static_cast<unsigned>(crc))));
    return head;
}

Bytes Build(char type, const std::string& payload)
{
    return BuildBytes(type, Bytes(payload.begin(), payload.end()));
}

Bytes Immediate(char type)
{
    return Bytes{ SYNC, static_cast<uint8_t>(type) };
}

bool ParseOne(const Bytes& raw, Frame& out)
{
    out = Frame();
    if (raw.size() == 2 && raw[0] == SYNC && (raw[1] == 'O' || raw[1] == 'P')) {
        out.type = static_cast<char>(raw[1]);
        out.immediate = true;
        out.raw = raw;
        return true;
    }
    if (raw.size() < MIN_RAW || raw[0] != SYNC) return false;
    for (size_t i = 2; i < 5; ++i)
        if (!IsHexChar(raw[i])) return false;

    size_t total = 0;
    for (size_t i = 2; i < 5; ++i) total = total * 16 + static_cast<size_t>(HexValue(raw[i]));
    if (total < MIN_RAW || total > MAX_RAW || raw.size() < total) return false;

    Bytes frame(raw.begin(), raw.begin() + total);
    for (size_t i = frame.size() - 4; i < frame.size(); ++i)
        if (!IsHexChar(frame[i])) return false;

    unsigned want = 0;
    for (size_t i = frame.size() - 4; i < frame.size(); ++i)
        want = want * 16 + static_cast<unsigned>(HexValue(frame[i]));

    out.type = static_cast<char>(frame[1]);
    out.crcOk = (Crc16(frame.data(), frame.size() - 4) == want);
    if (frame.size() > 9) {
        Bytes payload = Unescape(frame.data() + 5, frame.size() - 9);
        out.payload.assign(payload.begin(), payload.end());
    }
    out.raw = frame;
    return true;
}

Parser::Step Parser::Next(Frame& out, size_t& consumed)
{
    consumed = 0;
    if (m_buf.empty()) return Step::Wait;

    if (m_buf[0] != SYNC) {
        size_t pos = 0;
        while (pos < m_buf.size() && m_buf[pos] != SYNC) ++pos;
        if (pos == m_buf.size()) {
            m_errors.push_back("丢弃 " + std::to_string(pos) + " 字节非帧数据（未找到 0x55 同步字节）");
            ++m_errorCount;
            m_buf.clear();
            return Step::Wait;
        }
        m_buf.erase(m_buf.begin(), m_buf.begin() + pos);
        consumed = pos;
        return Step::Skip;
    }

    if (m_buf.size() < 2) return Step::Wait;
    uint8_t type = m_buf[1];

    if ((type == 'O' || type == 'P') && m_buf.size() >= 3 && !IsHexChar(m_buf[2])) {
        out = Frame();
        out.type = static_cast<char>(type);
        out.immediate = true;
        out.raw.assign(m_buf.begin(), m_buf.begin() + 2);
        m_buf.erase(m_buf.begin(), m_buf.begin() + 2);
        consumed = 2;
        return Step::Frame;
    }

    if (m_buf.size() < 5) return Step::Wait;
    if (!IsHexChar(m_buf[2]) || !IsHexChar(m_buf[3]) || !IsHexChar(m_buf[4])) {
        m_errors.push_back("长度字段非十六进制，丢弃同步字节重新同步");
        ++m_errorCount;
        m_buf.erase(m_buf.begin());
        consumed = 1;
        return Step::Skip;
    }

    size_t total = 0;
    for (size_t i = 2; i < 5; ++i) total = total * 16 + static_cast<size_t>(HexValue(m_buf[i]));
    if (total < MIN_RAW || total > MAX_RAW) {
        m_errors.push_back("长度 " + std::to_string(total) + " 超出 9..600，丢弃同步字节重新同步");
        ++m_errorCount;
        m_buf.erase(m_buf.begin());
        consumed = 1;
        return Step::Skip;
    }
    if (m_buf.size() < total) return Step::Wait;

    Bytes raw(m_buf.begin(), m_buf.begin() + total);
    bool parsed = ParseOne(raw, out);
    m_buf.erase(m_buf.begin(), m_buf.begin() + total);
    consumed = total;
    if (!parsed) {
        m_errors.push_back("CRC 字段非法，丢弃该帧");
        ++m_errorCount;
        return Step::Skip;
    }
    if (!out.crcOk) {
        m_errors.push_back(std::string("CRC 校验失败（类型 ") + out.type + "），按协议应为 0x6014");
        ++m_errorCount;
    }
    return Step::Frame;
}

std::vector<Frame> Parser::Feed(const uint8_t* data, size_t len)
{
    m_buf.insert(m_buf.end(), data, data + len);
    std::vector<Frame> frames;
    for (size_t guard = 0; guard < 100000; ++guard) {
        Frame f;
        size_t consumed = 0;
        Step step = Next(f, consumed);
        if (step == Step::Frame) frames.push_back(f);
        if (step == Step::Wait && consumed == 0) break;
    }
    return frames;
}

std::vector<std::string> Parser::TakeErrors()
{
    std::vector<std::string> tmp;
    tmp.swap(m_errors);
    return tmp;
}

std::string HexDump(const Bytes& data, size_t maxBytes)
{
    std::string out;
    size_t n = (maxBytes && data.size() > maxBytes) ? maxBytes : data.size();
    for (size_t i = 0; i < n; ++i) {
        if (i) out += ' ';
        out += HexDigit(data[i] >> 4);
        out += HexDigit(data[i] & 0xF);
    }
    if (n < data.size())
        out += " ... (+" + std::to_string(data.size() - n) + " 字节)";
    return out;
}

std::string Printable(const std::string& data)
{
    std::string out = data;
    for (char& c : out) {
        unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u > 0x7E) c = '.';
    }
    return out;
}

} // namespace qf
