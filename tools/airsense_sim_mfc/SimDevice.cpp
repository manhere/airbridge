#include "pch.h"
#include "SimDevice.h"
#include "VarCatalog.h"
#include <ctime>

namespace {

bool StartsWith(const std::string& text, const char* prefix)
{
    size_t n = std::char_traits<char>::length(prefix);
    return text.size() >= n && text.compare(0, n, prefix) == 0;
}

bool IsHex(const std::string& text)
{
    if (text.empty()) return false;
    for (char c : text)
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')))
            return false;
    return true;
}

int HexNumber(const std::string& text)
{
    int value = 0;
    for (char c : text) {
        int digit = (c >= '0' && c <= '9') ? (c - '0')
                  : (c >= 'A' && c <= 'F') ? (c - 'A' + 10) : (c - 'a' + 10);
        value = value * 16 + digit;
    }
    return value;
}

std::vector<std::string> Tokens(const std::string& text)
{
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= text.size()) {
        size_t pos = text.find(' ', start);
        if (pos == std::string::npos) { out.push_back(text.substr(start)); break; }
        if (pos > start) out.push_back(text.substr(start, pos - start));
        start = pos + 1;
    }
    return out;
}

struct Field { const char* name; int width; };
const std::map<std::string, std::vector<Field>> kSchemas = {
    { "TCE", { { "MKP", 3 }, { "RFL", 3 }, { "LYK", 2 } } },
    { "APN", { { "AET", 4 }, { "DUR", 2 } } },
    { "CSN", { { "CET", 5 }, { "CSR", 4 } } },
    { "PBT", { { "MV5", 2 }, { "RRR", 2 }, { "LKF", 2 }, { "TIP", 3 }, { "TEP", 3 } } },
    { "BRH", { { "TID", 2 }, { "ATP", 3 } } },
};

const std::vector<std::string> kStoredTags = {
    "ABR", "TXC", "TXH", "TXE", "TXW", "TRR", "DLL", "ERR", "ELI", "ZRL"
};

// Custom settings registry, protocol version 02. V4 records must reference
// numeric variables (the firmware queries G C #VAR for their limits).
const std::vector<std::string> kCsgRecords = {
    "V4 04 00000FFF IPC 0032 0001 01 05:cmH2O 01 03:Set Set Pressure",
    "V4 00 00000FFF MPA 0001 0001 00 05:cmH2O 00 Max Pressure",
    "V8 02 00000FFF EBE 01 07:Comfort Easy-Breathe",
};

} // namespace

CSimDevice::CSimDevice()
{
    for (const VarDef& def : CVarCatalog::Instance().All()) {
        VarState state;
        state.isEnum = def.isEnum;
        state.options = def.options;
        if (def.isEnum) {
            state.value = 0;
            state.maximum = (std::max)(0, static_cast<int>(def.options.size()) - 1);
            state.width = 2;
        } else {
            state.value = 100;
            state.maximum = 1000;
            state.width = 4;
        }
        m_vars[def.name] = state;
    }

    std::string day = TherapyDayHex();
    m_identity = {
        { "SRN", "23123456789" }, { "PNA", "AirSense 10 AutoSet" },
        { "MID", "0028" }, { "VID", "0402" }, { "STK", "00000001" },
        // 电机运行时间（秒；固件 /3600 得小时）。0x0112A880 = 18,000,000 s = 5000 h，
        // 一台合理在用机器的数值，避免仪表盘永远显示 0h。
        { "MHR", "0112A880" }, { "BLS", "0000" }, { "SSD", day },
        { "ZEN", "0001" }, { "DSD", day }, { "SEP", "0000" },
        { "LSD", day }, { "ZRM", "0000" }, { "MST", "0000" },
        { "LAN", "0000" }, { "BID", "SX567-0402" }, { "SID", "SX567-0402" },
        { "CID", "SX567-0402" }, { "PST", "0" }, { "BLE", "0000" },
        { "BDD", "0000" },
    };
}

const std::vector<std::string>& CSimDevice::StoredTags()
{
    return kStoredTags;
}

DeviceStats CSimDevice::Stats() const
{
    DeviceStats s;
    s.commands = m_commands;
    s.responses = m_responses;
    s.oxhFrames = m_oxhFrames;
    s.flashFrames = m_flashFrames;
    s.parserErrors = m_parserErrors;
    s.autoAcks = m_autoAcks;
    for (const auto& item : m_subscriptions)
        if (item.second) { if (!s.subscriptions.empty()) s.subscriptions += ","; s.subscriptions += item.first; }
    return s;
}

std::string CSimDevice::TherapyDayHex()
{
    long long days = static_cast<long long>(std::time(nullptr)) / 86400LL;
    return qf::ToUpperHex(static_cast<unsigned>(days & 0xFFFF), 4);
}

std::string CSimDevice::ClockText(const char* name)
{
    SYSTEMTIME now = {};
    GetLocalTime(&now);
    char buffer[32] = {};
    if (std::string(name) == "DAC")
        sprintf_s(buffer, "%02u%02u%04u", now.wDay, now.wMonth, now.wYear);
    else
        sprintf_s(buffer, "%02u%02u%02u", now.wHour, now.wMinute, now.wSecond);
    return std::string(buffer);
}

std::string CSimDevice::WireValue(const VarState& var) const
{
    if (!var.text.empty()) return var.text;
    return qf::ToUpperHex(static_cast<unsigned>(var.value), var.width);
}

qf::Bytes CSimDevice::Post(qf::Bytes wire)
{
    if (m_options.faultBadCrc && wire.size() >= 4) {
        uint8_t last = wire[wire.size() - 1];
        wire[wire.size() - 1] = (last == 'A') ? static_cast<uint8_t>('B') : static_cast<uint8_t>('A');
    }
    return wire;
}

qf::Bytes CSimDevice::Reply(const std::string& command, const std::string& value, bool echoOnly)
{
    std::string payload = echoOnly ? command : command + " = " + value;
    ++m_responses;
    return Post(qf::Build('R', payload));
}

qf::Bytes CSimDevice::Error(const std::string& command, const std::string& code)
{
    std::string payload = command.empty() ? code : command + " = " + code;
    ++m_responses;
    return Post(qf::Build('E', payload));
}

std::vector<qf::Bytes> CSimDevice::OnFrame(const qf::Frame& frame)
{
    if (!frame.crcOk) {
        ++m_parserErrors;
        return {};
    }
    if (frame.immediate) {
        if (frame.type == 'P') {
            m_gfQueue.clear();
            m_gfWaiting = false;
            return {};
        }
        if (frame.type == 'O') return GfAdvance(GetTickCount());
        return {};
    }

    switch (frame.type) {
    case 'Q':
        ++m_commands;
        return OnCommand(frame.payload);
    case 'L':
        ++m_oxhFrames;      // oximetry injection or host-side live frame
        return {};
    case 'f':
        ++m_flashFrames;
        return {};
    case 'O':
        return BootloaderDump(frame.payload);
    default:
        return {};
    }
}

std::vector<qf::Bytes> CSimDevice::OnCommand(const std::string& rawCommand)
{
    std::string command = rawCommand;
    for (size_t i = 0; i < command.size() && i < 8; ++i)
        if (command[i] >= 'a' && command[i] <= 'z')
            command[i] = static_cast<char>(command[i] - 'a' + 'A');

    if (StartsWith(command, "G S #"))      return CmdGet(command);
    if (StartsWith(command, "P S #"))      return CmdSet(command);
    if (StartsWith(command, "G C #"))      return CmdCaps(command);
    if (StartsWith(command, "G C &CSG"))   return CmdCsg(command);
    if (StartsWith(command, "G C &"))      return CmdSchema(command);
    if (StartsWith(command, "P S &"))      return CmdSubscribe(command);
    if (StartsWith(command, "G F &"))      return CmdStoredStream(command);
    if (StartsWith(command, "G V #"))      return CmdStoredValue(command);
    if (StartsWith(command, "P F *"))      return CmdFlashSelect(command);
    return { Error(command, "600E") };
}

std::vector<qf::Bytes> CSimDevice::CmdGet(const std::string& command)
{
    if (command.size() != 8) return { Error(command, "6008") };
    std::string name = command.substr(5, 3);

    if (name == "ROP")
        return m_options.faultRopTimeout ? std::vector<qf::Bytes>{} : std::vector<qf::Bytes>{ Reply(command, m_options.rop) };
    if (name == "DAC" || name == "TIC") {
        auto it = m_clockText.find(name);
        return { Reply(command, (it != m_clockText.end()) ? it->second : ClockText(name.c_str())) };
    }
    if (name == "AOA" || name == "TMU") return { Reply(command, "0000") };
    auto ident = m_identity.find(name);
    if (ident != m_identity.end()) return { Reply(command, ident->second) };
    auto var = m_vars.find(name);
    if (var != m_vars.end()) return { Reply(command, WireValue(var->second)) };
    return { Error(command, "6006") };
}

std::vector<qf::Bytes> CSimDevice::CmdSet(const std::string& command)
{
    if (command.size() < 10 || command[8] != ' ') return { Error(command, "600E") };
    std::string name = command.substr(5, 3);
    std::string value = command.substr(9);

    static const std::vector<std::string> kVirtual = {
        "RES", "BLL", "BDD", "PIP", "LCA", "LCT", "SEP", "SET", "ROP"
    };
    if (std::find(kVirtual.begin(), kVirtual.end(), name) != kVirtual.end()) {
        if (name == "ROP") m_options.rop = value;
        return { Reply(command, value) };
    }
    if (name == "DAC" || name == "TIC") {
        size_t want = (name == "DAC") ? 8 : 6;
        if (value.size() != want || value.find_first_not_of("0123456789") != std::string::npos)
            return { Error(command, "6031") };
        m_clockText[name] = value;
        return { Reply(command, value) };
    }
    auto ident = m_identity.find(name);
    if (ident != m_identity.end()) {
        ident->second = value;
        return { Reply(command, value) };
    }
    auto var = m_vars.find(name);
    if (var != m_vars.end()) {
        if (!IsHex(value)) return { Error(command, "6031") };
        int number = HexNumber(value);
        if (var->second.isEnum && number > var->second.maximum)
            return { Error(command, "6033") };
        var->second.value = number;
        return { Reply(command, WireValue(var->second)) };
    }
    return { Error(command, "6006") };
}

std::vector<qf::Bytes> CSimDevice::CmdCaps(const std::string& command)
{
    if (command.size() != 8 && command.size() != 11) return { Error(command, "600E") };
    std::string name = command.substr(5, 3);
    bool indexed = (command.size() == 11);
    std::string indexText = indexed ? command.substr(9, 2) : std::string();
    if (indexed && !IsHex(indexText)) return { Error(command, "6031") };

    if (name == "ALR") return { Reply(command, "7") };
    auto var = m_vars.find(name);
    if (var == m_vars.end()) return { Error(command, "6006") };

    if (!indexed) {
        if (var->second.isEnum) return { Reply(command, "7") };
        return { Reply(command, "7 " + qf::ToUpperHex(0, var->second.width)
                                 + " " + qf::ToUpperHex(static_cast<unsigned>(var->second.maximum), var->second.width)) };
    }

    int index = HexNumber(indexText);
    if (!var->second.isEnum || index < 0 || index > var->second.maximum ||
        index >= static_cast<int>(var->second.options.size()))
        return { Error(command, "6033") };
    return { Reply(command, qf::ToUpperHex(static_cast<unsigned>(var->second.options.size()), 2)
                             + " 1 " + var->second.options[static_cast<size_t>(index)]) };
}

std::vector<qf::Bytes> CSimDevice::CmdCsg(const std::string& command)
{
    if (m_options.faultCsg) return { Error(command, "6009") };
    if (command == "G C &CSG")
        return { Reply(command, "02 " + qf::ToUpperHex(static_cast<unsigned>(kCsgRecords.size()), 2)) };
    if (command.size() == 11 && IsHex(command.substr(9, 2))) {
        int index = HexNumber(command.substr(9, 2));
        if (index < 0 || index >= static_cast<int>(kCsgRecords.size()))
            return { Error(command, "6033") };
        return { Reply(command, kCsgRecords[static_cast<size_t>(index)]) };
    }
    return { Error(command, "600E") };
}

std::vector<qf::Bytes> CSimDevice::CmdSchema(const std::string& command)
{
    if (command.size() != 8) return { Error(command, "6009") };
    std::string tag = command.substr(5, 3);
    auto it = kSchemas.find(tag);
    if (it == kSchemas.end()) return { Error(command, "6009") };
    std::string body = qf::ToUpperHex(static_cast<unsigned>(it->second.size()), 2);
    for (const Field& field : it->second)
        body += " " + std::string(field.name) + ":" + qf::ToUpperHex(static_cast<unsigned>(field.width), 2);
    return { Reply(command, body) };
}

std::vector<qf::Bytes> CSimDevice::CmdSubscribe(const std::string& command)
{
    if (command.size() != 10 || command[8] != ' ' || (command[9] != '0' && command[9] != '1'))
        return { Error(command, "600E") };
    std::string tag = command.substr(5, 3);
    if (kSchemas.find(tag) == kSchemas.end()) return { Error(command, "6009") };
    bool on = (command[9] == '1');
    if (on) {
        m_subscriptions[tag] = true;
        m_streamSeq[tag] = 0;
        m_nextStreamAt[tag] = GetTickCount();
    } else {
        m_subscriptions.erase(tag);
        m_nextStreamAt.erase(tag);
    }
    return { Reply(command, on ? "1" : "0") };
}

std::vector<qf::Bytes> CSimDevice::CmdStoredValue(const std::string& command)
{
    std::vector<std::string> parts = Tokens(command);
    if (parts.size() != 5 || parts[2].size() != 4 || parts[2][0] != '#'
        || !IsHex(parts[3]) || parts[3].size() != 4 || !IsHex(parts[4]))
        return { Error(command, "6034") };
    std::string name = parts[2].substr(1);
    // 日报表字段每天恰好 1 个样本（sleep_report.cpp 拒绝 sample_count != 1）；
    // ONT/OFT 是每天多次治疗时段，合法返回多个。LSD（最近治疗日）必须把被查询的
    // 日原样回送为唯一样本，否则 sleep_report.cpp:123 会以 report_day_mismatch
    // 判整个报告失败，治疗数据因此无法测试。
    if (name == "LSD")
        return { Reply(command, "FFFF01" + parts[3] + "FFFF") };
    std::vector<int> samples;
    if (name == "ONT" || name == "OFT")      samples = { 8, 32 };
    else if (name == "AHI")                  samples = { 23 };
    else                                     samples = { 10 };
    std::string value = "FFFF" + qf::ToUpperHex(static_cast<unsigned>(samples.size()), 2);
    for (int sample : samples) value += qf::ToUpperHex(static_cast<unsigned>(sample), 4);
    value += "FFFF";
    return { Reply(command, value) };
}

std::vector<qf::Bytes> CSimDevice::CmdStoredStream(const std::string& command)
{
    if (command.size() < 8) return { Error(command, "600E") };
    std::string tag = command.substr(5, 3);
    std::string arg;
    if (command.size() > 8) {
        if (command[8] != ' ') return { Error(command, "600E") };
        arg = command.substr(9);
        if (arg.empty() || arg.size() > 4 || !IsHex(arg)) return { Error(command, "600E") };
    }
    if (std::find(kStoredTags.begin(), kStoredTags.end(), tag) == kStoredTags.end())
        return { Error(command, "6009") };

    if (arg == "0000") {
        m_gfQueue.clear();
        m_gfWaiting = false;
        return { Reply(command, "", true) };
    }

    std::string day = TherapyDayHex();
    const int count = 2;
    m_gfQueue.clear();
    for (int i = 0; i < count; ++i)
        m_gfQueue.push_back(tag + qf::ToUpperHex(static_cast<unsigned>(i + 1), 5) + day + "00000A1B2C3D");
    m_gfQueue.push_back(tag + "99999FFFF0" + qf::ToUpperHex(static_cast<unsigned>(count), 4));
    m_gfWaiting = false;

    std::vector<qf::Bytes> out;
    out.push_back(Reply(command, "", true));
    std::vector<qf::Bytes> more = GfAdvance(GetTickCount());
    out.insert(out.end(), more.begin(), more.end());
    return out;
}

std::vector<qf::Bytes> CSimDevice::CmdFlashSelect(const std::string& command)
{
    std::vector<std::string> parts = Tokens(command);
    std::string block;
    for (const std::string& part : parts)
        if (!part.empty() && part[0] == '*') { block = part; break; }
    return { Reply(block.empty() ? command : "P F " + block, "", true) };
}

std::vector<qf::Bytes> CSimDevice::BootloaderDump(const std::string& payload)
{
    if (payload.size() < 7 || payload[0] != 'D') return {};
    unsigned char p[7] = {};
    for (size_t i = 0; i < 7; ++i) p[i] = static_cast<unsigned char>(payload[i]);
    unsigned offset = static_cast<unsigned>(p[1]) | (static_cast<unsigned>(p[2]) << 8)
                    | (static_cast<unsigned>(p[3]) << 16) | (static_cast<unsigned>(p[4]) << 24);
    unsigned length = static_cast<unsigned>(p[5]) | (static_cast<unsigned>(p[6]) << 8);
    if (length > 240) length = 240;
    qf::Bytes body;
    body.push_back('d');
    for (size_t i = 1; i < 7; ++i) body.push_back(p[i]);
    for (unsigned i = 0; i < length; ++i)
        body.push_back(static_cast<uint8_t>((offset + i) & 0xFF));
    ++m_responses;
    return { Post(qf::BuildBytes('O', body)) };
}

std::vector<qf::Bytes> CSimDevice::GfAdvance(uint32_t nowMs)
{
    m_gfWaiting = false;
    if (m_gfQueue.empty()) return {};
    std::string payload = m_gfQueue.front();
    m_gfQueue.erase(m_gfQueue.begin());
    ++m_responses;
    if (!m_gfQueue.empty()) {
        m_gfWaiting = true;
        m_gfSentAt = nowMs;
    }
    return { Post(qf::Build('K', payload)) };
}

std::vector<qf::Bytes> CSimDevice::Tick(uint32_t nowMs)
{
    std::vector<qf::Bytes> out;
    if (m_gfWaiting && (nowMs - m_gfSentAt) >= 350) {
        ++m_autoAcks;
        std::vector<qf::Bytes> more = GfAdvance(nowMs);
        out.insert(out.end(), more.begin(), more.end());
    }

    if (m_options.liveStream) {
        for (const auto& item : m_subscriptions) {
            if (!item.second) continue;
            const std::string& tag = item.first;
            auto next = m_nextStreamAt.find(tag);
            if (next != m_nextStreamAt.end() && nowMs < next->second) continue;
            auto schema = kSchemas.find(tag);
            if (schema == kSchemas.end()) continue;

            int seq = m_streamSeq[tag];
            std::string payload = tag + qf::ToUpperHex(static_cast<unsigned>(seq & 0xFF), 2);
            for (size_t i = 0; i < schema->second.size(); ++i) {
                int width = schema->second[i].width;
                int modulus = 1;
                for (int k = 0; k < (std::min)(width, 6); ++k) modulus *= 16;
                unsigned value = static_cast<unsigned>((seq * 7 + static_cast<int>(i) * 13 + 1) % modulus);
                payload += qf::ToUpperHex(value, width);
            }
            out.push_back(Post(qf::Build('L', payload)));
            ++m_responses;
            m_streamSeq[tag] = (seq + 1) & 0xFF;
            m_nextStreamAt[tag] = nowMs + 200;
        }
    }
    return out;
}
