#include "pch.h"
#include "framework.h"
#include "resource.h"
#include "MainDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace {
const COLORREF kColorRx    = RGB(0, 64, 176);
const COLORREF kColorTx    = RGB(0, 112, 48);
const COLORREF kColorError = RGB(176, 0, 0);
const COLORREF kColorInfo  = RGB(96, 96, 96);
const int      kMaxLogRows = 3000;
}

CMainDlg::CMainDlg(CWnd* pParent)
    : CDialogEx(IDD_MAIN_DIALOG, pParent)
{
}

void CMainDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_COMBO_PORT, m_portCombo);
    DDX_Control(pDX, IDC_COMBO_BAUD, m_baudCombo);
    DDX_Control(pDX, IDC_COMBO_ROP, m_ropCombo);
    DDX_Control(pDX, IDC_BUTTON_OPEN, m_btnOpen);
    DDX_Control(pDX, IDC_CHECK_HEX, m_checkHex);
    DDX_Control(pDX, IDC_CHECK_PAUSE, m_checkPause);
    DDX_Control(pDX, IDC_CHECK_FAULT_CSG, m_checkCsg);
    DDX_Control(pDX, IDC_CHECK_FAULT_TIMEOUT, m_checkTimeout);
    DDX_Control(pDX, IDC_CHECK_FAULT_CRC, m_checkCrc);
    DDX_Control(pDX, IDC_CHECK_LIVE, m_checkLive);
    DDX_Control(pDX, IDC_LIST_LOG, m_log);
    DDX_Control(pDX, IDC_EDIT_DETAIL, m_detail);
    DDX_Control(pDX, IDC_EDIT_CMD, m_cmd);
    DDX_Control(pDX, IDC_STATIC_STATUS, m_status);
    DDX_Control(pDX, IDC_STATIC_COUNTS, m_counts);
}

BEGIN_MESSAGE_MAP(CMainDlg, CDialogEx)
    ON_WM_TIMER()
    ON_BN_CLICKED(IDC_BUTTON_OPEN, &CMainDlg::OnBnClickedOpen)
    ON_BN_CLICKED(IDC_BUTTON_REFRESH_PORTS, &CMainDlg::OnBnClickedRefreshPorts)
    ON_BN_CLICKED(IDC_BUTTON_CLEAR, &CMainDlg::OnBnClickedClear)
    ON_BN_CLICKED(IDC_BUTTON_SAVE, &CMainDlg::OnBnClickedSave)
    ON_BN_CLICKED(IDC_BUTTON_DEMO, &CMainDlg::OnBnClickedDemo)
    ON_BN_CLICKED(IDC_BUTTON_SEND, &CMainDlg::OnBnClickedSend)
    ON_BN_CLICKED(IDC_CHECK_FAULT_CSG, &CMainDlg::OnOptionsChanged)
    ON_BN_CLICKED(IDC_CHECK_FAULT_TIMEOUT, &CMainDlg::OnOptionsChanged)
    ON_BN_CLICKED(IDC_CHECK_FAULT_CRC, &CMainDlg::OnOptionsChanged)
    ON_BN_CLICKED(IDC_CHECK_LIVE, &CMainDlg::OnOptionsChanged)
    ON_BN_CLICKED(IDC_CHECK_PAUSE, &CMainDlg::OnOptionsChanged)
    ON_CBN_SELCHANGE(IDC_COMBO_ROP, &CMainDlg::OnRopChanged)
    ON_NOTIFY(LVN_ITEMCHANGED, IDC_LIST_LOG, &CMainDlg::OnLogSelChanged)
    ON_NOTIFY(NM_CUSTOMDRAW, IDC_LIST_LOG, &CMainDlg::OnLogCustomDraw)
    ON_MESSAGE(WM_SERIAL_RX, OnSerialRx)
END_MESSAGE_MAP()

BOOL CMainDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    InitLogList();
    InitCombos();
    InitPorts();
    SyncOptionsFromUi();

    SetDlgItemText(IDC_EDIT_CMD, _T("R G S #ROP = 0001"));
    m_status.SetWindowText(_T("状态：未连接"));
    m_detail.SetWindowText(_T("点击左侧任意日志行，这里显示该帧的逐字段结构与指令解读。\r\n\r\n无硬件时点右上角“自检演示”，可查看完整的命令 / 应答 / 错误 / 实时流帧示例。"));
    AppendEvent(_T("模拟器就绪"),
                _T("选择串口 → 打开串口，即可对 AirBridge 固件模拟 AirSense 10 设备。\r\n")
                _T("接线：USB-UART TX → ESP32 GPIO44(AB_AS10_RX_GPIO)，RX ← GPIO43，共地，3.3V，57600 8N1。\r\n")
                _T("无硬件可点“自检演示”；故障注入用于测试固件对超时、CRC 错误、注册表不可用等异常的处理。"),
                kColorInfo);
    UpdateCounts();
    SetTimer(1, 100, nullptr);
    return TRUE;
}

void CMainDlg::OnCancel()
{
    KillTimer(1);
    m_serial.Close();
    CDialogEx::OnCancel();
}

void CMainDlg::InitLogList()
{
    m_log.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES);
    // 时间列需容纳 "20:55:50.301" 全串，避免省略号截断
    m_log.InsertColumn(0, _T("时间"), LVCFMT_LEFT, 100);
    m_log.InsertColumn(1, _T("方向"), LVCFMT_LEFT, 46);
    m_log.InsertColumn(2, _T("类型"), LVCFMT_LEFT, 62);
    m_log.InsertColumn(3, _T("长度"), LVCFMT_LEFT, 46);
    m_log.InsertColumn(4, _T("内容 / HEX"), LVCFMT_LEFT, 200);
    m_log.InsertColumn(5, _T("指令解读"), LVCFMT_LEFT, 220);
}

void CMainDlg::InitCombos()
{
    const int bauds[] = { 9600, 19200, 38400, 57600, 115200, 230400, 460800 };
    m_baudCombo.ResetContent();
    for (int baud : bauds) {
        CString text;
        text.Format(_T("%d"), baud);
        m_baudCombo.AddString(text);
    }
    m_baudCombo.SetCurSel(3);   // 57600 8N1

    m_ropCombo.ResetContent();
    m_ropCombo.AddString(_T("0000 待机"));
    m_ropCombo.AddString(_T("0001 治疗中"));
    m_ropCombo.AddString(_T("0004 过渡状态"));
    m_ropCombo.SetCurSel(0);

    m_checkLive.SetCheck(BST_CHECKED);
    m_checkCsg.SetCheck(BST_UNCHECKED);
    m_checkTimeout.SetCheck(BST_UNCHECKED);
    m_checkCrc.SetCheck(BST_UNCHECKED);
    m_checkHex.SetCheck(BST_UNCHECKED);
    m_checkPause.SetCheck(BST_UNCHECKED);
}

void CMainDlg::InitPorts()
{
    CString previous;
    int selected = m_portCombo.GetCurSel();
    if (selected >= 0) m_portCombo.GetLBText(selected, previous);

    m_portCombo.ResetContent();
    std::vector<CString> ports = CSerialPort::EnumPorts();
    for (const CString& port : ports) m_portCombo.AddString(port);

    if (ports.empty()) {
        m_portCombo.AddString(_T("(未发现串口)"));
        m_portCombo.SetCurSel(0);
        return;
    }
    int index = m_portCombo.FindStringExact(0, previous);
    m_portCombo.SetCurSel(index >= 0 ? index : 0);
}

void CMainDlg::SyncOptionsFromUi()
{
    DeviceOptions options;
    CString rop;
    int selected = m_ropCombo.GetCurSel();
    if (selected >= 0) m_ropCombo.GetLBText(selected, rop);
    options.rop = ci::Narrow(rop.Left(4));
    if (options.rop.size() != 4) options.rop = "0000";
    options.faultCsg = (m_checkCsg.GetCheck() == BST_CHECKED);
    options.faultRopTimeout = (m_checkTimeout.GetCheck() == BST_CHECKED);
    options.faultBadCrc = (m_checkCrc.GetCheck() == BST_CHECKED);
    options.liveStream = (m_checkLive.GetCheck() == BST_CHECKED);
    m_paused = (m_checkPause.GetCheck() == BST_CHECKED);
    m_device.SetOptions(options);
}

CString CMainDlg::NowStamp() const
{
    SYSTEMTIME now = {};
    GetLocalTime(&now);
    CString stamp;
    stamp.Format(_T("%02u:%02u:%02u.%03u"),
                 now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
    return stamp;
}

void CMainDlg::InsertEntry(const LogEntry& entry)
{
    int row = m_log.GetItemCount();
    m_log.InsertItem(row, entry.time);
    m_log.SetItemText(row, 1, entry.direction);
    m_log.SetItemText(row, 2, entry.type);
    m_log.SetItemText(row, 3, entry.length);
    m_log.SetItemText(row, 4, entry.content);
    m_log.SetItemText(row, 5, entry.brief);
    m_entries.push_back(entry);

    if (static_cast<int>(m_entries.size()) > kMaxLogRows) {
        m_log.DeleteItem(0);
        m_entries.erase(m_entries.begin());
    }
    if (!m_paused) m_log.EnsureVisible(m_log.GetItemCount() - 1, FALSE);
}

void CMainDlg::AppendFrame(const qf::Frame& frame, bool outgoing)
{
    LogEntry entry;
    entry.time = NowStamp();
    entry.direction = outgoing ? _T("TX") : _T("RX");
    entry.type = ci::FrameTypeName(frame.type);
    if (frame.immediate) entry.type += _T(" 立即");
    entry.length.Format(_T("%u"), static_cast<unsigned>(frame.raw.size()));
    if (m_checkHex.GetCheck() == BST_CHECKED)
        entry.content = ci::Widen(qf::HexDump(frame.raw, 24));
    else if (frame.immediate)
        entry.content = _T("(无载荷)");
    else
        entry.content = ci::Widen(qf::Printable(frame.payload));
    entry.brief = ci::Brief(frame, outgoing);
    entry.detail = ci::Detail(frame, outgoing, entry.time);

    if (!frame.crcOk || frame.type == 'E') entry.color = kColorError;
    else if (outgoing) entry.color = kColorTx;
    else entry.color = kColorRx;

    if (outgoing) ++m_txCount; else ++m_rxCount;
    InsertEntry(entry);
    UpdateCounts();
}

void CMainDlg::AppendEvent(const CString& brief, const CString& detail, COLORREF color)
{
    LogEntry entry;
    entry.time = NowStamp();
    entry.direction = _T("--");
    entry.type = _T("事件");
    entry.length = _T("-");
    entry.content = brief;
    entry.brief = brief;
    entry.detail = detail;
    entry.color = color;
    InsertEntry(entry);
}

void CMainDlg::SendFrames(const std::vector<qf::Bytes>& frames)
{
    for (const qf::Bytes& wire : frames) {
        qf::Frame frame;
        if (!qf::ParseOne(wire, frame)) continue;
        AppendFrame(frame, true);
        if (m_serial.IsOpen()) {
            CString error;
            if (!m_serial.Write(wire, error))
                AppendEvent(error, CString(_T("串口写入失败\r\n")) + error, kColorError);
        }
    }
}

void CMainDlg::UpdateCounts()
{
    DeviceStats stats = m_device.Stats();
    CString text;
    CString subs = stats.subscriptions.empty() ? CString(_T("无")) : ci::Widen(stats.subscriptions);
    text.Format(_T("计数：RX %d / TX %d / 错误 %d    命令 %d / 应答 %d    订阅 [%s]"),
                m_rxCount, m_txCount, m_errorCount, stats.commands, stats.responses,
                static_cast<LPCTSTR>(subs));
    m_counts.SetWindowText(text);
}

LRESULT CMainDlg::OnSerialRx(WPARAM, LPARAM lParam)
{
    auto* chunk = reinterpret_cast<std::vector<uint8_t>*>(lParam);
    if (!chunk) return 0;
    std::vector<qf::Frame> frames = m_parser.Feed(chunk->data(), chunk->size());
    delete chunk;

    for (const qf::Frame& frame : frames) {
        AppendFrame(frame, false);
        if (frame.crcOk)
            SendFrames(m_device.OnFrame(frame));
        else
            ++m_errorCount;
    }
    for (const std::string& error : m_parser.TakeErrors()) {
        ++m_errorCount;
        CString brief = ci::Widen(error);
        AppendEvent(brief, CString(_T("帧解析事件\r\n")) + brief, kColorError);
    }
    UpdateCounts();
    return 0;
}

void CMainDlg::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == 1) {
        SendFrames(m_device.Tick(GetTickCount()));
        UpdateCounts();
    }
    CDialogEx::OnTimer(nIDEvent);
}

void CMainDlg::OnBnClickedOpen()
{
    if (m_serial.IsOpen()) {
        m_serial.Close();
        m_parser.Reset();
        m_btnOpen.SetWindowText(_T("打开串口"));
        m_status.SetWindowText(_T("状态：未连接"));
        AppendEvent(_T("串口已关闭"), _T("读线程已停止，解析器缓冲已清空。"), kColorInfo);
        return;
    }

    CString port;
    m_portCombo.GetWindowText(port);
    port.Trim();
    if (port.IsEmpty() || port.Left(1) == _T("(")) {
        MessageBox(_T("未发现可用串口，请插入 USB-UART 后点“刷新”。"), _T("串口"), MB_ICONINFORMATION);
        return;
    }
    CString baudText;
    int selected = m_baudCombo.GetCurSel();
    if (selected >= 0) m_baudCombo.GetLBText(selected, baudText);
    int baud = _ttoi(baudText);
    if (baud <= 0) baud = 57600;

    CString error;
    if (!m_serial.Open(port, baud, GetSafeHwnd(), error)) {
        MessageBox(error, _T("串口"), MB_ICONWARNING);
        return;
    }
    m_parser.Reset();
    m_btnOpen.SetWindowText(_T("关闭串口"));
    CString status;
    status.Format(_T("状态：%s @ %d 8N1，等待主机命令"), static_cast<LPCTSTR>(port), baud);
    m_status.SetWindowText(status);
    AppendEvent(CString(_T("串口已打开：")) + port,
                CString(_T("设备模拟已启动。\r\n参数：")) + baudText + _T(" baud, 8N1, 无流控。\r\n等待 AirBridge 发送 Q 帧命令。"),
                kColorInfo);
}

void CMainDlg::OnBnClickedRefreshPorts()
{
    InitPorts();
    CString text;
    text.Format(_T("刷新串口：找到 %d 个端口"), m_portCombo.GetCount());
    AppendEvent(text, CString(_T("枚举 HKEY_LOCAL_MACHINE\\HARDWARE\\DEVICEMAP\\SERIALCOMM。\r\n")) + text, kColorInfo);
}

void CMainDlg::OnBnClickedClear()
{
    m_log.DeleteAllItems();
    m_entries.clear();
    m_rxCount = m_txCount = m_errorCount = 0;
    m_detail.SetWindowText(_T("日志已清空。"));
    UpdateCounts();
}

void CMainDlg::OnBnClickedSave()
{
    CFileDialog dialog(FALSE, _T("txt"), _T("airsense_uart_log"),
                       OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT,
                       _T("文本文件 (*.txt)|*.txt|所有文件 (*.*)|*.*||"), this);
    if (dialog.DoModal() != IDOK) return;

    CString text = _T("AirSense 10 串口模拟设备 - 通讯日志\r\n")
                   _T("时间\t方向\t类型\t长度\t内容\t解读\r\n");
    for (const LogEntry& entry : m_entries) {
        CString line;
        line.Format(_T("%s\t%s\t%s\t%s\t%s\t%s\r\n"),
                    static_cast<LPCTSTR>(entry.time),
                    static_cast<LPCTSTR>(entry.direction),
                    static_cast<LPCTSTR>(entry.type),
                    static_cast<LPCTSTR>(entry.length),
                    static_cast<LPCTSTR>(entry.content),
                    static_cast<LPCTSTR>(entry.brief));
        text += line;
    }

    CFile file;
    if (!file.Open(dialog.GetPathName(), CFile::modeCreate | CFile::modeWrite)) {
        MessageBox(_T("无法创建日志文件。"), _T("保存"), MB_ICONWARNING);
        return;
    }
    std::string utf8 = ci::Narrow(text);
    const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
    file.Write(bom, 3);
    file.Write(utf8.data(), static_cast<UINT>(utf8.size()));
    file.Close();
    AppendEvent(_T("日志已保存"), CString(_T("文件：")) + CString(dialog.GetPathName()), kColorInfo);
}

void CMainDlg::OnBnClickedSend()
{
    CString text;
    m_cmd.GetWindowText(text);
    text.Trim();
    if (text.IsEmpty()) {
        MessageBox(_T("请输入要注入的帧，例如：R G S #ROP = 0001"), _T("注入"), MB_ICONINFORMATION);
        return;
    }
    std::string type = ci::Narrow(text.Left(1));
    CString body = text.Mid(1);
    body.TrimLeft();
    if (type.size() != 1) return;

    qf::Bytes wire;
    if ((type[0] == 'O' || type[0] == 'P') && body.IsEmpty())
        wire = qf::Immediate(type[0]);
    else
        wire = qf::Build(type[0], ci::Narrow(body));
    if (wire.empty()) {
        MessageBox(_T("帧构造失败：内容过长或类型非法。"), _T("注入"), MB_ICONWARNING);
        return;
    }
    qf::Frame frame;
    if (!qf::ParseOne(wire, frame)) return;
    AppendFrame(frame, true);
    if (m_serial.IsOpen()) {
        CString error;
        if (!m_serial.Write(wire, error))
            AppendEvent(error, CString(_T("串口写入失败\r\n")) + error, kColorError);
    } else {
        AppendEvent(_T("已记录注入帧（串口未打开，未实际发送）"),
                    _T("打开串口后注入的帧会真正写到线上面。"), kColorInfo);
    }
}

void CMainDlg::OnOptionsChanged()
{
    SyncOptionsFromUi();
}

void CMainDlg::OnRopChanged()
{
    SyncOptionsFromUi();
    CString rop;
    int selected = m_ropCombo.GetCurSel();
    if (selected >= 0) m_ropCombo.GetLBText(selected, rop);
    AppendEvent(CString(_T("设备状态切换为 ")) + rop,
                _T("ROP 由 AirBridge 轮询读取。\r\n0000 待机 / 0001 治疗中 / 0004 过渡状态。\r\n切换后下一次 G S #ROP 返回新值。"),
                kColorInfo);
}

void CMainDlg::OnLogSelChanged(NMHDR* pNMHDR, LRESULT* pResult)
{
    LPNMLISTVIEW notify = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
    if (notify->iItem >= 0 && (notify->uNewState & LVIS_SELECTED) &&
        notify->iItem < static_cast<int>(m_entries.size())) {
        m_detail.SetWindowText(m_entries[static_cast<size_t>(notify->iItem)].detail);
    }
    *pResult = 0;
}

void CMainDlg::OnLogCustomDraw(NMHDR* pNMHDR, LRESULT* pResult)
{
    NMLVCUSTOMDRAW* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(pNMHDR);
    *pResult = CDRF_DODEFAULT;
    if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) {
        *pResult = CDRF_NOTIFYITEMDRAW;
    } else if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
        int row = static_cast<int>(draw->nmcd.dwItemSpec);
        if (row >= 0 && row < static_cast<int>(m_entries.size()))
            draw->clrText = m_entries[static_cast<size_t>(row)].color;
    }
}

void CMainDlg::RunDemo()
{
    SyncOptionsFromUi();
    AppendEvent(_T("自检演示开始"),
                _T("本次演示不依赖硬件：直接把一组典型的主机命令喂给设备状态机，\r\n")
                _T("再把它产生的应答帧记录到日志中，用于检查编解码与解读是否正确。"),
                kColorInfo);

    auto host = [this](const std::string& command) {
        qf::Frame frame;
        if (!qf::ParseOne(qf::Build('Q', command), frame)) return;
        AppendFrame(frame, false);
        if (frame.crcOk) SendFrames(m_device.OnFrame(frame));
    };

    host("G S #ROP");
    host("G S #SRN");
    host("G S #DAC");
    host("P S #IPC 00C8");
    host("G C #IPC");
    host("G C #MOP 00");
    host("G C &CSG");
    host("G C &CSG 00");
    host("G C &TCE");
    host("G V #AHI 4E20 0");
    host("P S &TCE 1");
    SendFrames(m_device.Tick(GetTickCount()));
    host("G F &ERR 0001");

    qf::Frame errorFrame;
    if (qf::ParseOne(qf::Build('E', "G S #XXX = 6006"), errorFrame))
        AppendFrame(errorFrame, true);

    qf::Bytes bad = qf::Build('Q', "G S #ROP");
    if (bad.size() >= 4) bad[bad.size() - 1] = (bad[bad.size() - 1] == 'A') ? 'B' : 'A';
    qf::Frame badFrame;
    if (qf::ParseOne(bad, badFrame)) {
        AppendFrame(badFrame, false);
        ++m_errorCount;
        AppendEvent(_T("CRC 校验失败：设备按协议应回 0x6014，模拟器默认丢弃该帧"),
                    _T("故障注入演示：把帧尾 CRC 的最后一个十六进制字符改掉。\r\n")
                    _T("帧解析器仍会报告该帧，但 crcOk = false，设备状态机不产生应答。"),
                    kColorError);
    }

    qf::Frame immediate;
    if (qf::ParseOne(qf::Immediate('O'), immediate))
        AppendFrame(immediate, false);

    AppendEvent(_T("自检演示结束"),
                _T("点击任意日志行可查看：帧长度 / CRC / 0x55 转义 / 线字节 HEX，\r\n")
                _T("以及命令语义、变量中文名、期望应答格式与错误码含义。"),
                kColorInfo);
    UpdateCounts();
}

void CMainDlg::OnBnClickedDemo()
{
    RunDemo();
}
