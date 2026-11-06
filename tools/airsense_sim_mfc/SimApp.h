#pragma once

class CSimApp : public CWinApp
{
public:
    CSimApp();
    virtual BOOL InitInstance();

    DECLARE_MESSAGE_MAP()
};

extern CSimApp theApp;
