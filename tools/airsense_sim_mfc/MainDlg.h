#pragma once

#include "CommandInfo.h"
#include "QFrame.h"
#include "SerialPort.h"
#include "SimDevice.h"

struct LogEntry {
    CString   time;
    CString   direction;
    CString   type;
    CString   length;
    CString   content;
    CString   brief;
    CString   detail;
    COLORREF  color = RGB(0, 0, 0);
};

class CMainDlg : public CDialogEx
{
public:
    CMainDlg(CWnd* pParent = nullptr);

#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_MAIN_DIALOG };
#endif

protected:
    virtual void DoDataExchange(CDataExchange* pDX);
    virtual BOOL OnInitDialog();
    virtual void OnCancel();

    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg void OnBnClickedOpen();
    afx_msg void OnBnClickedRefreshPorts();
    afx_msg void OnBnClickedClear();
    afx_msg void OnBnClickedSave();
    afx_msg void OnBnClickedDemo();
    afx_msg void OnBnClickedSend();
    afx_msg void OnOptionsChanged();
    afx_msg void OnRopChanged();
    afx_msg void OnLogSelChanged(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnLogCustomDraw(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg LRESULT OnSerialRx(WPARAM wParam, LPARAM lParam);

    DECLARE_MESSAGE_MAP()

private:
    void InitPorts();
    void InitLogList();
    void InitCombos();
    void SyncOptionsFromUi();
    void InsertEntry(const LogEntry& entry);
    void AppendFrame(const qf::Frame& frame, bool outgoing);
    void AppendEvent(const CString& brief, const CString& detail, COLORREF color);
    void SendFrames(const std::vector<qf::Bytes>& frames);
    void UpdateCounts();
    void RunDemo();
    CString NowStamp() const;

    CComboBox m_portCombo;
    CComboBox m_baudCombo;
    CComboBox m_ropCombo;
    CButton   m_btnOpen;
    CButton   m_checkHex;
    CButton   m_checkPause;
    CButton   m_checkCsg;
    CButton   m_checkTimeout;
    CButton   m_checkCrc;
    CButton   m_checkLive;
    CListCtrl m_log;
    CEdit     m_detail;
    CEdit     m_cmd;
    CStatic   m_status;
    CStatic   m_counts;

    CSerialPort m_serial;
    CSimDevice  m_device;
    qf::Parser  m_parser;

    std::vector<LogEntry> m_entries;
    int m_rxCount = 0;
    int m_txCount = 0;
    int m_errorCount = 0;
    bool m_paused = false;
};
