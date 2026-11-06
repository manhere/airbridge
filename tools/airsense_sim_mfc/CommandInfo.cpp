#include "pch.h"
#include "CommandInfo.h"

namespace ci {

CString Widen(const std::string& text)
{
    if (text.empty()) return CString();
    int n = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    if (n <= 0) return CString();
    CString out;
    wchar_t* buffer = out.GetBuffer(n);
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), buffer, n);
    out.ReleaseBuffer(n);
    return out;
}

std::string Narrow(const CString& text)
{
    if (text.IsEmpty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, text, text.GetLength(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, text.GetLength(), &out[0], n, nullptr, nullptr);
    return out;
}

static std::vector<std::string> Split(const std::string& text, char sep)
{
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= text.size()) {
        size_t pos = text.find(sep, start);
        if (pos == std::string::npos) {
            parts.push_back(text.substr(start));
            break;
        }
        parts.push_back(text.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

static bool IsHexDigits(const std::string& s)
{
    if (s.empty()) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')))
            return false;
    return true;
}

CString FrameTypeName(char type)
{
    CString out;
    out.Format(_T("%c"), static_cast<TCHAR>(type));
    return out;
}

CString FrameTypeLabel(char type)
{
    switch (type) {
    case 'Q': return _T("Q 主机命令帧（AirBridge → 设备）");
    case 'R': return _T("R 成功响应帧（设备 → AirBridge）");
    case 'E': return _T("E 错误响应帧（设备 → AirBridge）");
    case 'K': return _T("K 存储流数据帧");
    case 'L': return _T("L 实时流 / 血氧输入帧");
    case 'f': return _T("f 固件传输帧（bootloader）");
    case 'O': return _T("O 存储流确认 / bootloader dump");
    case 'P': return _T("P 解析器复位 / 擦除进度");
    case 'T': return _T("T 保留类型（设备以 0x6011 拒绝）");
    default:  return CString(_T("未知帧类型 ")) + CString(static_cast<TCHAR>(type));
    }
}

CString ErrorCodeText(const std::string& code)
{
    static const struct { const char* code; const TCHAR* text; } kErrors[] = {
        { "6001", _T("异步 G V 工作线程忙") },
        { "6004", _T("无效或不支持的 bootloader 命令 / 块选择") },
        { "6006", _T("未知变量或通道（Air 10 数据帧块名不匹配）") },
        { "6008", _T("G S 命令长度错误") },
        { "6009", _T("命令在该变量 / 通道上下文不可用（原厂固件常见）") },
        { "600C", _T("新同步字节打断了未完成的帧") },
        { "600E", _T("命令语法错误") },
        { "6011", _T("帧类型被拒绝") },
        { "6012", _T("帧长度非法") },
        { "6013", _T("未完成帧中出现非法类型字节") },
        { "6014", _T("帧 CRC 校验失败") },
        { "6031", _T("十六进制参数非法") },
        { "6033", _T("能力 / CSG 索引不可用") },
        { "6034", _T("G V 选择器或参数非法") },
        { "6052", _T("响应缓冲区溢出（不截断，直接报错）") },
        { "7003", _T("Flash 字节编程失败") },
        { "7006", _T("帧序号异常") },
        { "7009", _T("记录地址越出所选块") },
        { "700D", _T("块已选中，擦除或传输进行中") },
        { "7016", _T("固件完整性 / CCX-CDX 兼容性校验失败") },
    };
    for (const auto& item : kErrors)
        if (code == item.code) return CString(item.text);
    return CString();
}

static CString ValueNote(const std::string& command, const std::string& value)
{
    if (command.find("ROP") != std::string::npos) {
        if (value == "0000") return _T("待机（设备在场、未在治疗）");
        if (value == "0001") return _T("治疗中");
        if (value == "0004") return _T("治疗启动 / 过渡状态");
        return _T("其他 ROP 状态值");
    }
    if (command.find("#MOP") != std::string::npos)
        return _T("MOP 治疗模式索引，需查 G C #MOP II 获取选项名");
    if (command.find("&CSG") != std::string::npos)
        return _T("VV = 协议版本，NN = 记录条数（16 进制）");
    if (command.find("#DAC") != std::string::npos) return _T("日期 DDMMYYYY");
    if (command.find("#TIC") != std::string::npos) return _T("时间 HHMMSS");
    return CString();
}

CString CommandExplain(const std::string& command)
{
    std::string cmd = command;
    // CDX upper-cases the first eight characters; the value keeps its bytes.
    for (size_t i = 0; i < cmd.size() && i < 8; ++i)
        if (cmd[i] >= 'a' && cmd[i] <= 'z') cmd[i] = static_cast<char>(cmd[i] - 'a' + 'A');

    CString out;
    auto add = [&out](const TCHAR* text) { out += text; out += _T("\r\n"); };

    if (cmd.rfind("G S #", 0) == 0) {
        std::string var = cmd.substr(5, 3);
        out += CString(_T("读取标量变量 ")) + Widen(var) + _T("（") + Widen(VarDisplayName(var)) + _T("）\r\n");
        add(_T("命令格式：G S #VAR，固定 8 字符；VAR 为三字符 UART 变量名"));
        add(_T("期望应答：R: G S #VAR = VALUE（数值为大写十六进制原始值）"));
        if (var == "ROP") {
            add(_T("ROP = 治疗运行状态：0000 待机 / 0001 治疗中 / 0004 过渡"));
            add(_T("固件用途：AirSenseState 约每 5 s 轮询一次，用于判断设备是否在场"));
        } else if (var == "DAC" || var == "TIC") {
            add(_T("时钟变量：DAC = DDMMYYYY，TIC = HHMMSS（十进制数字）"));
            add(_T("固件用途：air10_clock.cpp 读写呼吸机时钟"));
        } else if (var == "LAN" || var == "MOP" || var == "AOA" || var == "TMU") {
            add(_T("固件用途：自定义设置注册表发现前的上下文读取"));
        } else {
            add(_T("固件用途：临床设置页读取当前设定值"));
        }
        add(_T("异常：未知变量 0x6006；命令长度错误 0x6008"));
        return out;
    }

    if (cmd.rfind("P S #", 0) == 0) {
        std::string var = cmd.substr(5, 3);
        std::string value = cmd.size() > 9 ? cmd.substr(9) : std::string();
        out += CString(_T("写入标量变量 ")) + Widen(var) + _T("（") + Widen(VarDisplayName(var)) + _T("） = ") + Widen(value) + _T("\r\n");
        add(_T("命令格式：P S #VAR VALUE，变量名后必须有一个空格"));
        add(_T("写值走校验 → 回调 → 依赖 → 持久化流程，应答中的值是处理后的回读值"));
        add(_T("期望应答：R: P S #VAR VALUE = 回读值（可能与请求值不同）"));
        add(_T("异常：语法 0x600E；非十六进制字符 0x6031；枚举索引越界 0x6033"));
        return out;
    }

    if (cmd.rfind("G C #", 0) == 0) {
        std::string rest = cmd.substr(5);
        std::vector<std::string> parts = Split(rest, ' ');
        std::string var = parts.empty() ? std::string() : parts[0];
        out += CString(_T("查询变量能力 ")) + Widen(var) + _T("（") + Widen(VarDisplayName(var)) + _T("）\r\n");
        if (parts.size() > 1) {
            out += CString(_T("枚举选项索引 II = ")) + Widen(parts[1]) + _T("\r\n");
            add(_T("期望应答：NN P LABEL，NN 为选项总数，P=1 表示该选项当前可用"));
            add(_T("注意：客户端需拿到 NN 后逐个查询 0..NN-1，不能把首个 0x6033 当作列表结束"));
        } else {
            add(_T("数值型应答：F MIN MAX（F 为标志位，MIN/MAX 与变量等宽）"));
            add(_T("枚举 / 文本型应答：F（单个十六进制标志位）"));
            add(_T("标志位：bit0 ACT、bit1 VIS、bit2 EDT"));
        }
        add(_T("异常：未知变量 0x6006；索引不可用 0x6033"));
        return out;
    }

    if (cmd.rfind("G C &CSG", 0) == 0) {
        add(_T("自定义设置注册表（Airbreak 补丁扩展），命令族 G C &CSG"));
        if (cmd.size() <= 8) {
            add(_T("无参数：读注册表头 → VV NN"));
            add(_T("VV = 协议版本（02 支持分组记录；01 无分组字段）"));
            add(_T("NN = 记录条数；原厂固件返回 0x6009，打了 UART 元数据补丁但无菜单返回 02 00"));
        } else {
            add(_T("带索引 II：读取第 II 条记录，返回 V4 / V8 开头的一条"));
            add(_T("V4 数值变量：V4 CC MMMMMMMM VAR SSSS TTTT DD UU:UNITS GG [LL:GROUP ...] LABEL"));
            add(_T("V8 枚举变量：V8 CC MMMMMMMM VAR GG [LL:GROUP ...] LABEL"));
            add(_T("CC 分类 00..04 = 治疗 / 舒适 / 附件 / 选项 / 配置；UU、LL 按字节数读取"));
            add(_T("异常：索引语法 0x600E、非十六进制 0x6031、不可用 0x6033、缓冲区溢出 0x6052"));
        }
        add(_T("固件用途：custom_settings.cpp 做临床设置页的注册表发现"));
        return out;
    }

    if (cmd.rfind("G C &", 0) == 0) {
        std::string tag = cmd.substr(5, 3);
        out += CString(_T("查询实时流通道 ")) + Widen(tag) + _T(" 的字段布局\r\n");
        add(_T("期望应答：NN VAR:WW VAR:WW ...（NN = 字段数，WW = 十六进制字符宽度）"));
        add(_T("字段顺序与 L 帧载荷一致；原厂固件对该命令返回 0x6009"));
        return out;
    }

    if (cmd.rfind("P S &", 0) == 0) {
        std::string tag = cmd.substr(5, 3);
        out += CString(_T("订阅 / 取消实时流通道 ")) + Widen(tag) + _T("\r\n");
        add(_T("P S &TAG 1 = 开启，P S &TAG 0 = 关闭；没有速率参数，订阅在重启后清空"));
        add(_T("开启后设备主动推送 L 帧；AirBridge 实际使用的 TAG：TCE / APN / CSN / PBT / BRH"));
        return out;
    }

    if (cmd.rfind("G F &", 0) == 0) {
        std::string tag = cmd.substr(5, 3);
        out += CString(_T("查询存储流 ")) + Widen(tag) + _T("（EEPROM 记录）\r\n");
        add(_T("参数：省略 = 全部当前记录；0000 = 关闭读取器；0001..0FFF = 倒序第 N 条；1000..FFFF = 治疗日"));
        add(_T("应答：先 R 原样回显请求，再发若干 K 帧，最后以 99999FFFF0NNNN 结束"));
        add(_T("主机每收一帧 K 需回立即 O（0x55 0x4F）确认；结束标记无需确认"));
        return out;
    }

    if (cmd.rfind("G V #", 0) == 0) {
        out += _T("读取存储信号样本（STR.ssn / NPD / NPA）\r\n");
        add(_T("格式：G V #VAR DDDD C；DDDD 为治疗日索引（≥1000），C 为游标"));
        add(_T("异步命令，无立即成功响应；结果在 R 帧返回"));
        add(_T("应答：HHHH NN [样本…] CCCC；CCCC=FFFF 表示读取完成"));
        add(_T("异常：不支持的信号 0x6009；worker 忙 0x6001；参数非法 0x6034"));
        return out;
    }

    if (cmd.rfind("P F *", 0) == 0) {
        add(_T("bootloader：选择并擦除 Flash 块（BLX / CCX / CDX / CMX）"));
        add(_T("擦除过程中设备重复发 P 进度帧，最后以 R 帧表示完成"));
        return out;
    }

    out += CString(_T("未识别的命令：")) + Widen(command) + _T("\r\n");
    add(_T("CDX 命令族：G S / P S / G C / G F / G V / P F；固件会对未知命令返回 0x600E"));
    return out;
}

static std::string RequestOf(const std::string& payload)
{
    size_t pos = payload.find(" = ");
    return (pos == std::string::npos) ? payload : payload.substr(0, pos);
}

static std::string ValueOf(const std::string& payload)
{
    size_t pos = payload.find(" = ");
    return (pos == std::string::npos) ? std::string() : payload.substr(pos + 3);
}

CString Brief(const qf::Frame& frame, bool outgoing)
{
    CString arrow = outgoing ? _T("模拟设备 → 主机") : _T("主机 → 模拟设备");
    if (frame.immediate) {
        if (frame.type == 'O') return _T("立即 O：存储流确认 + 解析器复位");
        return _T("立即 P：解析器复位");
    }

    std::string text = frame.payload;
    switch (frame.type) {
    case 'Q': {
        CString explain = CommandExplain(text);
        int nl = explain.Find(_T('\n'));
        return (nl > 0 ? explain.Left(nl).TrimRight(_T('\r')) : explain) + _T(" [") + arrow + _T("]");
    }
    case 'R': {
        std::string req = RequestOf(text);
        std::string value = ValueOf(text);
        CString out = CString(_T("应答 ")) + Widen(req) + _T(" = ") + Widen(value);
        CString note = ValueNote(req, value);
        if (!note.IsEmpty()) out += _T("（") + note + _T("）");
        return out;
    }
    case 'E': {
        std::string code;
        std::vector<std::string> parts = Split(text, ' ');
        for (auto it = parts.rbegin(); it != parts.rend(); ++it)
            if (IsHexDigits(*it) && it->size() <= 4) { code = *it; break; }
        CString out = CString(_T("错误 ")) + Widen(code);
        CString meaning = ErrorCodeText(code);
        if (!meaning.IsEmpty()) out += _T("：") + meaning;
        return out;
    }
    case 'K': {
        if (text.size() >= 3) {
            std::string tag = text.substr(0, 3);
            if (text.find("FFFF", 3) != std::string::npos)
                return CString(_T("存储流 ")) + Widen(tag) + _T("：传输结束 / 控制标记");
            return CString(_T("存储流 ")) + Widen(tag) + _T("：记录数据帧，等待立即 O 确认");
        }
        return _T("存储流数据帧");
    }
    case 'L': {
        std::string tag = text.size() >= 3 ? text.substr(0, 3) : std::string();
        if (tag == "OXH") return _T("血氧 OXH 输入帧：适配器注入 SpO2 / 脉率");
        std::string seq = text.size() >= 5 ? text.substr(3, 2) : std::string("??");
        return CString(_T("实时流 ")) + Widen(tag) + _T("，序号 0x") + Widen(seq);
    }
    case 'f': return _T("bootloader 固件数据帧");
    case 'O': return _T("bootloader dump 请求 / 响应");
    case 'P': return _T("bootloader 擦除进度帧");
    default:  return CString(_T("帧类型 ")) + FrameTypeName(frame.type) + _T(" [") + arrow + _T("]");
    }
}

CString Detail(const qf::Frame& frame, bool outgoing, const CString& stamp)
{
    CString out;
    out += _T("时间        : ") + stamp + _T("\r\n");
    out += _T("方向        : ") + CString(outgoing ? _T("发送（模拟设备 → AirBridge）") : _T("接收（AirBridge → 模拟设备）")) + _T("\r\n");
    out += _T("帧类型      : ") + FrameTypeLabel(frame.type) + _T("\r\n");

    if (frame.immediate) {
        out += _T("帧形式      : 立即帧（2 字节，无长度 / 载荷 / CRC）\r\n");
        out += CString(_T("线字节      : ")) + Widen(qf::HexDump(frame.raw)) + _T("\r\n");
        out += _T("\r\n【指令解读】\r\n");
        if (frame.type == 'O')
            out += _T("立即 O（0x55 0x4F）：确认收到的存储流 K 帧并复位解析器；设备随后发送下一帧 K。\r\n固件说明：AirBridge 不发送该形式，靠 350 ms 帧间空闲判定传输结束。\r\n");
        else
            out += _T("立即 P（0x55 0x50）：复位帧解析器，不产生应答。\r\n");
        return out;
    }

    size_t total = frame.raw.size();
    size_t payloadWire = (total >= qf::MIN_RAW) ? total - 9 : 0;
    size_t syncBytes = 0;
    for (size_t i = 5; i + 4 < frame.raw.size(); ++i)
        if (frame.raw[i] == qf::SYNC) ++syncBytes;

    CString line;
    line.Format(_T("线字节长度  : %u 字节（长度字段 0x%03X，含 sync..CRC 全部字节）"),
                static_cast<unsigned>(total), static_cast<unsigned>(total));
    out += line + _T("\r\n");
    line.Format(_T("载荷        : %u 字节（转义后线上字节），去转义后 %u 字节"),
                static_cast<unsigned>(payloadWire), static_cast<unsigned>(frame.payload.size()));
    out += line + _T("\r\n");
    line.Format(_T("0x55 转义   : 载荷区检测到 %u 个 0x55 线字节（成对写入代表 1 个载荷 0x55）"),
                static_cast<unsigned>(syncBytes));
    out += line + _T("\r\n");
    if (total >= 4) {
        std::string crc(frame.raw.begin() + static_cast<long>(total) - 4, frame.raw.end());
        out += CString(_T("CRC-16      : ")) + Widen(crc) + (frame.crcOk ? _T("，校验通过") : _T("，校验失败 → 0x6014")) + _T("\r\n");
    }
    out += _T("CRC 算法    : CCITT-FALSE，poly 0x1021，init 0xFFFF，高位在前，无输出异或；覆盖 sync..载荷（转义后）\r\n");
    out += _T("线字节 HEX  : ") + Widen(qf::HexDump(frame.raw, 48)) + _T("\r\n");
    out += _T("载荷文本    : ") + Widen(qf::Printable(frame.payload)) + _T("\r\n");

    out += _T("\r\n【指令解读】\r\n");
    std::string text = frame.payload;
    switch (frame.type) {
    case 'Q':
        out += CommandExplain(text);
        break;
    case 'R': {
        std::string req = RequestOf(text);
        std::string value = ValueOf(text);
        out += CString(_T("成功应答，对应请求：")) + Widen(req) + _T("\r\n");
        out += CString(_T("返回值 VALUE = ")) + Widen(value) + _T("\r\n");
        CString note = ValueNote(req, value);
        if (!note.IsEmpty()) out += CString(_T("值含义      : ")) + note + _T("\r\n");
        out += _T("格式约定    : R 帧载荷为 \"<命令> = <值>\"；capture_variable() 校验 payload[8:11] == \" = \"，从偏移 11 取值\r\n");
        if (req.find("G V #") != std::string::npos)
            out += _T("G V 应答结构：HHHH(4) + NN(2, 样本数) + 样本(每样本 4 位 hex) + CCCC(下一游标，FFFF 完成)\r\n");
        if (req.find("G C &CSG") != std::string::npos)
            out += _T("CSG 记录解析：首字段 V4 / V8 区分数值 / 枚举变量，UU: 与 LL: 后的字符串按给定字节数读取\r\n");
        break;
    }
    case 'E': {
        std::string code;
        std::vector<std::string> parts = Split(text, ' ');
        for (auto it = parts.rbegin(); it != parts.rend(); ++it)
            if (IsHexDigits(*it) && it->size() <= 4) { code = *it; break; }
        out += CString(_T("错误码      : 0x")) + Widen(code) + _T("\r\n");
        CString meaning = ErrorCodeText(code);
        out += CString(_T("含义        : ")) + (meaning.IsEmpty() ? CString(_T("未收录的错误码")) : meaning) + _T("\r\n");
        out += CString(_T("对应请求    : ")) + Widen(RequestOf(text)) + _T("\r\n");
        out += _T("载荷形态    : \"REQUEST = CCCC\"，解析器层错误可能只有 \" = CCCC\"\r\n");
        if (code == "6009")
            out += _T("提示        : 原厂固件不支持 Airbreak 扩展命令（G C &CSG / G C &TAG）时返回此码\r\n");
        break;
    }
    case 'K': {
        if (text.size() >= 3) {
            out += CString(_T("流标识 NAME : ")) + Widen(text.substr(0, 3)) + _T("\r\n");
            if (text.size() >= 17) {
                out += CString(_T("帧序号      : ")) + Widen(text.substr(3, 5)) + _T("\r\n");
                out += CString(_T("治疗日 DDDD : ")) + Widen(text.substr(8, 4)) + _T("\r\n");
                out += CString(_T("记录偏移    : ")) + Widen(text.substr(12, 5)) + _T("\r\n");
                out += CString(_T("数据 DATA   : ")) + Widen(text.substr(17)) + _T("\r\n");
            }
            if (text.find("FFFF", 3) != std::string::npos)
                out += _T("控制标记    : 99999 / FFFF 表示记录间游标或传输结束，NNNN 为选中记录数\r\n");
        }
        out += _T("确认流程    : 每帧 K 后主机需回立即 O（0x55 0x4F）才会继续下一帧；结束标记无需确认\r\n");
        break;
    }
    case 'L': {
        std::string tag = text.size() >= 3 ? text.substr(0, 3) : std::string();
        if (tag == "OXH") {
            out += _T("血氧适配器输入帧（AirBridge → 设备），设备不直接应答。\r\n");
            out += _T("OXH 载荷    : O X H + seq(2) + OXS(2) + HRR(3) + SAS(2) + SAR(2) + NVS(2)\r\n");
            out += _T("字段含义    : OXS = SpO2，HRR = 脉率，SAS/SAR 为信号质量与状态位\r\n");
        } else {
            out += CString(_T("通道 TAG    : ")) + Widen(tag) + _T("\r\n");
            out += CString(_T("滚动序号 SS : 0x")) + Widen(text.size() >= 5 ? text.substr(3, 2) : std::string("??")) + _T("\r\n");
            out += CString(_T("字段数据    : ")) + Widen(text.size() > 5 ? text.substr(5) : std::string()) + _T("\r\n");
            out += _T("字段宽度    : 由 G C &TAG 的 VAR:WW 定义，字段间无分隔符\r\n");
            out += _T("订阅来源    : P S &TAG 1 开启，重启后订阅清空\r\n");
        }
        break;
    }
    case 'f':
        out += _T("bootloader 固件帧：BLOCK(3) + marker(1, 0x00 数据 / 'F' 完成) + sequence(1) + 二进制 Flash 记录\r\n");
        out += _T("无逐帧应答；完成后设备通过 #BLE 暴露状态\r\n");
        break;
    case 'O':
        out += _T("patched bootloader dump：'D' + offset_le32 + length_le16，应答 'd' + 同样的偏移/长度 + 数据\r\n");
        break;
    case 'P':
        out += _T("bootloader 擦除进度帧；最终 R 帧表示擦除完成\r\n");
        break;
    default:
        out += _T("该帧类型无附加解读。\r\n");
        break;
    }

    if (!frame.crcOk)
        out += _T("\r\n注意：CRC 校验失败，按协议应回 0x6014，模拟设备默认丢弃该帧。\r\n");
    return out;
}

} // namespace ci
