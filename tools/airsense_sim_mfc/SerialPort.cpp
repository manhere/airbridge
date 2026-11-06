#include "pch.h"
#include "SerialPort.h"

CSerialPort::CSerialPort()
{
}

CSerialPort::~CSerialPort()
{
    Close();
}

std::vector<CString> CSerialPort::EnumPorts()
{
    std::vector<CString> ports;
    HKEY key = nullptr;
    LONG status = RegOpenKeyEx(HKEY_LOCAL_MACHINE,
                               _T("HARDWARE\\DEVICEMAP\\SERIALCOMM"),
                               0, KEY_READ, &key);
    if (status == ERROR_SUCCESS) {
        DWORD index = 0;
        for (;;) {
            TCHAR valueName[256] = {};
            BYTE data[512] = {};
            DWORD nameLen = 256;
            DWORD dataLen = sizeof(data);
            DWORD type = 0;
            LONG result = RegEnumValue(key, index++, valueName, &nameLen,
                                       nullptr, &type, data, &dataLen);
            if (result != ERROR_SUCCESS) break;
            if (type != REG_SZ || dataLen == 0) continue;
            data[sizeof(data) - 2] = 0;
            data[sizeof(data) - 1] = 0;
            CString port(reinterpret_cast<TCHAR*>(data));
            port.Trim();
            if (!port.IsEmpty())
                ports.push_back(port);
        }
        RegCloseKey(key);
    }

    // Fallback for drivers that do not publish SERIALCOMM (e.g. some virtual ports).
    if (ports.empty()) {
        for (int i = 1; i <= 32; ++i) {
            CString port;
            port.Format(_T("COM%d"), i);
            CString path = _T("\\\\.\\") + port;
            HANDLE handle = CreateFile(path, GENERIC_READ | GENERIC_WRITE, 0,
                                       nullptr, OPEN_EXISTING, 0, nullptr);
            if (handle != INVALID_HANDLE_VALUE) {
                CloseHandle(handle);
                ports.push_back(port);
            }
        }
    }

    std::sort(ports.begin(), ports.end(), [](const CString& a, const CString& b) {
        int na = _ttoi(static_cast<LPCTSTR>(a) + 3);
        int nb = _ttoi(static_cast<LPCTSTR>(b) + 3);
        if (na != nb) return na < nb;
        return a.Compare(b) < 0;
    });
    ports.erase(std::unique(ports.begin(), ports.end()), ports.end());
    return ports;
}

bool CSerialPort::Open(const CString& port, int baud, HWND hwnd, CString& error)
{
    Close();
    if (port.IsEmpty()) { error = _T("请先选择串口"); return false; }

    CString path = _T("\\\\.\\") + port;
    m_hCom = CreateFile(path, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (m_hCom == INVALID_HANDLE_VALUE) {
        error.Format(_T("打开 %s 失败（错误 %lu）；请确认端口未被其他程序占用"),
                     static_cast<LPCTSTR>(port), GetLastError());
        return false;
    }

    SetupComm(m_hCom, 8192, 8192);
    DCB dcb = {};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(m_hCom, &dcb)) {
        error = _T("读取串口参数失败");
        Close();
        return false;
    }
    dcb.BaudRate = static_cast<DWORD>(baud);
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fAbortOnError = FALSE;
    if (!SetCommState(m_hCom, &dcb)) {
        error = _T("设置 57600 8N1 失败");
        Close();
        return false;
    }

    COMMTIMEOUTS timeouts = {};
    timeouts.ReadIntervalTimeout = 10;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 20;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 200;
    SetCommTimeouts(m_hCom, &timeouts);
    PurgeComm(m_hCom, PURGE_RXCLEAR | PURGE_TXCLEAR | PURGE_RXABORT | PURGE_TXABORT);

    m_hwnd = hwnd;
    m_hStop = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    m_hThread = CreateThread(nullptr, 0, ThreadProc, this, 0, nullptr);
    if (!m_hThread) {
        error = _T("创建读线程失败");
        Close();
        return false;
    }
    return true;
}

void CSerialPort::Close()
{
    if (m_hStop) SetEvent(m_hStop);
    if (m_hThread) {
        WaitForSingleObject(m_hThread, 2000);
        CloseHandle(m_hThread);
        m_hThread = nullptr;
    }
    if (m_hStop) {
        CloseHandle(m_hStop);
        m_hStop = nullptr;
    }
    if (m_hCom != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hCom);
        m_hCom = INVALID_HANDLE_VALUE;
    }
    m_hwnd = nullptr;
}

bool CSerialPort::Write(const qf::Bytes& data, CString& error)
{
    if (!IsOpen()) { error = _T("串口未打开"); return false; }
    if (data.empty()) return true;
    DWORD written = 0;
    if (!WriteFile(m_hCom, data.data(), static_cast<DWORD>(data.size()), &written, nullptr)) {
        error.Format(_T("写串口失败（错误 %lu）"), GetLastError());
        return false;
    }
    if (written != data.size()) {
        error = _T("写串口未完整写出");
        return false;
    }
    return true;
}

DWORD WINAPI CSerialPort::ThreadProc(LPVOID parameter)
{
    static_cast<CSerialPort*>(parameter)->ReaderLoop();
    return 0;
}

void CSerialPort::ReaderLoop()
{
    uint8_t buffer[4096];
    while (WaitForSingleObject(m_hStop, 0) == WAIT_TIMEOUT) {
        DWORD read = 0;
        if (!ReadFile(m_hCom, buffer, sizeof(buffer), &read, nullptr)) {
            DWORD err = GetLastError();
            if (err == ERROR_OPERATION_ABORTED) break;
            Sleep(5);
            continue;
        }
        if (read == 0) continue;
        auto* chunk = new std::vector<uint8_t>(buffer, buffer + read);
        if (!(m_hwnd && IsWindow(m_hwnd)
              && PostMessage(m_hwnd, WM_SERIAL_RX, 0, reinterpret_cast<LPARAM>(chunk))))
            delete chunk;
    }
}
