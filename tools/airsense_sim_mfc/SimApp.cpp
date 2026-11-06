#include "pch.h"
#include "framework.h"
#include "SimApp.h"
#include "MainDlg.h"
#include "QFrame.h"
#include "CommandInfo.h"
#include "SimDevice.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

CSimApp theApp;

CSimApp::CSimApp()
{
}

BEGIN_MESSAGE_MAP(CSimApp, CWinApp)
    ON_COMMAND(ID_HELP, &CWinApp::OnHelp)
END_MESSAGE_MAP()

namespace {

qf::Frame MakeFrame(char type, const std::string& payload)
{
    qf::Frame frame;
    qf::ParseOne(qf::Build(type, payload), frame);
    return frame;
}

qf::Frame AnswerOf(CSimDevice& device, const std::string& command)
{
    qf::Frame request = MakeFrame('Q', command);
    std::vector<qf::Bytes> replies = device.OnFrame(request);
    if (replies.empty()) return qf::Frame();
    qf::Frame answer;
    qf::ParseOne(replies.front(), answer);
    return answer;
}

int RunSelfTest()
{
    std::vector<std::string> lines;
    int passed = 0, failed = 0;
    auto check = [&](const char* name, bool ok, const std::string& detail = std::string()) {
        lines.push_back(std::string(ok ? "[PASS] " : "[FAIL] ") + name
                        + (detail.empty() ? "" : "  ->  " + detail));
        if (ok) ++passed; else ++failed;
    };

    // --- frame codec ---
    const char* vector = "123456789";
    uint16_t crc = qf::Crc16(reinterpret_cast<const uint8_t*>(vector), 9);
    check("CRC-16/CCITT-FALSE(123456789) = 29B1", crc == 0x29B1, qf::ToUpperHex(crc, 4));

    qf::Bytes wire = qf::Build('Q', "G S #ROP");
    qf::Frame frame;
    bool parsed = qf::ParseOne(wire, frame);
    check("Q frame roundtrip / CRC", parsed && frame.type == 'Q'
          && frame.payload == "G S #ROP" && frame.crcOk,
          "len=" + std::to_string(wire.size()));
    check("length = 9 + escaped payload", wire.size() == 9 + 8, std::to_string(wire.size()));

    std::string escaped("A");
    escaped.push_back(static_cast<char>(0x55));
    escaped.push_back('B');
    qf::Bytes escapedWire = qf::Build('Q', escaped);
    qf::Frame escapedFrame;
    qf::ParseOne(escapedWire, escapedFrame);
    check("0x55 payload escaping", escapedFrame.payload == escaped && escapedWire.size() == 9 + 4);

    qf::Frame immediate;
    qf::ParseOne(qf::Immediate('O'), immediate);
    check("immediate O frame", immediate.immediate && immediate.type == 'O'
          && immediate.raw.size() == 2);

    qf::Parser parser;
    const uint8_t junk[] = { 0x00, 0x12, 0x33 };
    parser.Feed(junk, sizeof(junk));
    std::vector<qf::Frame> recovered = parser.Feed(wire.data(), wire.size());
    check("parser resynchronises on 0x55", recovered.size() == 1
          && recovered[0].payload == "G S #ROP");

    qf::Bytes broken = wire;
    broken[broken.size() - 1] = (broken[broken.size() - 1] == 'A') ? 'B' : 'A';
    qf::Frame brokenFrame;
    qf::ParseOne(broken, brokenFrame);
    check("bad CRC is detected", !brokenFrame.crcOk);

    // --- device state machine ---
    CSimDevice device;
    qf::Frame rop = AnswerOf(device, "G S #ROP");
    check("G S #ROP -> R 0000", rop.type == 'R' && rop.payload == "G S #ROP = 0000", rop.payload);

    qf::Frame set = AnswerOf(device, "P S #IPC 00C8");
    check("P S #IPC 00C8 -> readback", set.type == 'R'
          && set.payload == "P S #IPC 00C8 = 00C8", set.payload);

    qf::Frame header = AnswerOf(device, "G C &CSG");
    check("G C &CSG -> 02 03", header.type == 'R'
          && header.payload == "G C &CSG = 02 03", header.payload);

    qf::Frame record = AnswerOf(device, "G C &CSG 00");
    check("G C &CSG 00 -> V4 record", record.type == 'R'
          && record.payload.find("= V4 ") != std::string::npos,
          record.payload.substr(0, 48));

    qf::Frame caps = AnswerOf(device, "G C #MOP 00");
    check("G C #MOP 00 -> enum option", caps.type == 'R'
          && caps.payload == "G C #MOP 00 = 0C 1 CPAP", caps.payload);

    qf::Frame stored = AnswerOf(device, "G V #AHI 4E20 0");
    check("G V #AHI -> FFFF010017FFFF", stored.type == 'R'
          && stored.payload.find("FFFF010017FFFF") != std::string::npos, stored.payload);

    qf::Frame unknown = AnswerOf(device, "G S #XXX");
    check("unknown var -> E 6006", unknown.type == 'E'
          && unknown.payload.find("6006") != std::string::npos, unknown.payload);

    qf::Frame subscribe = AnswerOf(device, "P S &TCE 1");
    check("P S &TCE 1 -> R 1", subscribe.type == 'R'
          && subscribe.payload == "P S &TCE 1 = 1", subscribe.payload);
    std::vector<qf::Bytes> live = device.Tick(GetTickCount());
    qf::Frame liveFrame;
    if (!live.empty()) qf::ParseOne(live.front(), liveFrame);
    check("subscribed channel emits L frame", liveFrame.type == 'L', liveFrame.payload);

    // --- fault injection ---
    DeviceOptions options;
    options.faultBadCrc = true;
    device.SetOptions(options);
    qf::Frame badReply = AnswerOf(device, "G S #ROP");
    check("fault: bad CRC in every reply", badReply.type == 'R' && !badReply.crcOk);

    options.faultBadCrc = false;
    options.faultRopTimeout = true;
    device.SetOptions(options);
    check("fault: ROP timeout produces no reply", AnswerOf(device, "G S #ROP").type == 0);
    options.faultRopTimeout = false;

    options.faultCsg = true;
    device.SetOptions(options);
    qf::Frame csgError = AnswerOf(device, "G C &CSG");
    check("fault: CSG 6009", csgError.type == 'E'
          && csgError.payload.find("6009") != std::string::npos, csgError.payload);

    // --- write report next to the exe ---
    lines.push_back("");
    lines.push_back("TOTAL: " + std::to_string(passed) + " passed, "
                    + std::to_string(failed) + " failed");

    TCHAR module[MAX_PATH] = {};
    GetModuleFileName(nullptr, module, MAX_PATH);
    CString path(module);
    int slash = path.ReverseFind(_T('\\'));
    if (slash >= 0) path = path.Left(slash + 1);
    path += _T("selftest_result.txt");

    std::string report;
    for (const std::string& line : lines) report += line + "\r\n";
    CFile file;
    if (file.Open(path, CFile::modeCreate | CFile::modeWrite)) {
        const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
        file.Write(bom, 3);
        file.Write(report.data(), static_cast<UINT>(report.size()));
        file.Close();
    }
    return failed;
}

} // namespace

BOOL CSimApp::InitInstance()
{
    CWinApp::InitInstance();

    INITCOMMONCONTROLSEX icex = {};
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    CString commandLine = m_lpCmdLine;
    if (commandLine.Find(_T("--selftest")) >= 0) {
        RunSelfTest();
        return FALSE;
    }

    CMainDlg dlg;
    m_pMainWnd = &dlg;
    dlg.DoModal();

    return FALSE;
}
