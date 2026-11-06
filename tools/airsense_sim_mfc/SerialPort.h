#pragma once

#include "QFrame.h"

// Posted from the reader thread; LPARAM owns a new std::vector<uint8_t>.
#define WM_SERIAL_RX   (WM_APP + 1)

class CSerialPort
{
public:
    CSerialPort();
    ~CSerialPort();

    static std::vector<CString> EnumPorts();

    bool Open(const CString& port, int baud, HWND hwnd, CString& error);
    void Close();
    bool IsOpen() const { return m_hCom != INVALID_HANDLE_VALUE; }
    bool Write(const qf::Bytes& data, CString& error);

private:
    static DWORD WINAPI ThreadProc(LPVOID parameter);
    void ReaderLoop();

    HANDLE m_hCom = INVALID_HANDLE_VALUE;
    HANDLE m_hStop = nullptr;
    HANDLE m_hThread = nullptr;
    HWND  m_hwnd = nullptr;
};
