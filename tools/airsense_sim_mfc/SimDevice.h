#pragma once

#include "QFrame.h"

#include <map>
#include <string>
#include <vector>
#include <cstdint>

struct DeviceOptions {
    std::string rop = "0000";       // 0000 standby / 0001 therapy / 0004 transition
    bool faultCsg = false;          // G C &CSG -> 0x6009
    bool faultRopTimeout = false;   // G S #ROP -> no reply
    bool faultBadCrc = false;       // corrupt the CRC of every reply
    bool liveStream = true;         // emit L frames for subscribed channels
};

struct DeviceStats {
    int commands = 0;
    int responses = 0;
    int oxhFrames = 0;
    int flashFrames = 0;
    int parserErrors = 0;
    int autoAcks = 0;
    std::string subscriptions;
};

struct VarState {
    bool isEnum = false;
    int value = 100;
    int minimum = 0;
    int maximum = 1000;
    int width = 4;
    std::vector<std::string> options;
    std::string text;
};

class CSimDevice {
public:
    CSimDevice();

    void SetOptions(const DeviceOptions& options) { m_options = options; }
    const DeviceOptions& Options() const { return m_options; }
    DeviceStats Stats() const;

    // Handle one frame received from AirBridge; returns frames to send back.
    std::vector<qf::Bytes> OnFrame(const qf::Frame& frame);
    // Periodic work: live streams and the 350 ms stored-stream auto-continue.
    std::vector<qf::Bytes> Tick(uint32_t nowMs);

    static const std::vector<std::string>& StoredTags();

private:
    qf::Bytes Reply(const std::string& command, const std::string& value, bool echoOnly = false);
    qf::Bytes Error(const std::string& command, const std::string& code);
    qf::Bytes Post(qf::Bytes wire);

    std::vector<qf::Bytes> OnCommand(const std::string& command);
    std::vector<qf::Bytes> CmdGet(const std::string& command);
    std::vector<qf::Bytes> CmdSet(const std::string& command);
    std::vector<qf::Bytes> CmdCaps(const std::string& command);
    std::vector<qf::Bytes> CmdCsg(const std::string& command);
    std::vector<qf::Bytes> CmdSchema(const std::string& command);
    std::vector<qf::Bytes> CmdSubscribe(const std::string& command);
    std::vector<qf::Bytes> CmdStoredValue(const std::string& command);
    std::vector<qf::Bytes> CmdStoredStream(const std::string& command);
    std::vector<qf::Bytes> CmdFlashSelect(const std::string& command);
    std::vector<qf::Bytes> BootloaderDump(const std::string& payload);
    std::vector<qf::Bytes> GfAdvance(uint32_t nowMs);

    std::string WireValue(const VarState& var) const;
    static std::string TherapyDayHex();
    static std::string ClockText(const char* name);

    DeviceOptions m_options;
    std::map<std::string, VarState> m_vars;
    std::map<std::string, std::string> m_identity;
    std::map<std::string, std::string> m_clockText;

    std::map<std::string, bool> m_subscriptions;
    std::map<std::string, int> m_streamSeq;
    std::map<std::string, uint32_t> m_nextStreamAt;
    std::vector<std::string> m_gfQueue;
    bool m_gfWaiting = false;
    uint32_t m_gfSentAt = 0;

    int m_commands = 0;
    int m_responses = 0;
    int m_oxhFrames = 0;
    int m_flashFrames = 0;
    int m_parserErrors = 0;
    int m_autoAcks = 0;
};
