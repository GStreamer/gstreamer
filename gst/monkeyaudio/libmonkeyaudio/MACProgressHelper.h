// MACProgressHelper.h: interface for the CMACProgressHelper class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_MACPROGRESSHELPER_H__5ACFF299_B004_4A19_85D9_CC49296D5611__INCLUDED_)
#define AFX_MACPROGRESSHELPER_H__5ACFF299_B004_4A19_85D9_CC49296D5611__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#define KILL_FLAG_CONTINUE       0
#define KILL_FLAG_PAUSE         -1

typedef void (__stdcall * APE_PROGRESS_CALLBACK) (int);

class CMACProgressHelper
{
public:
    CMACProgressHelper(int nTotalSteps, int *pPercentageDone, APE_PROGRESS_CALLBACK ProgressCallback, int *pKillFlag);
    virtual ~CMACProgressHelper();

    void UpdateProgress(int nCurrentStep = -1, BOOL bForceUpdate = FALSE);
    void UpdateProgressComplete() { UpdateProgress(m_nTotalSteps, TRUE); }

    int ProcessKillFlag(BOOL bSleep = TRUE);

private:

    BOOL                    m_bUseCallback;
    APE_PROGRESS_CALLBACK   m_CallbackFunction;

    int                     *m_pPercentageDone;

    int                     m_nTotalSteps;
    int                     m_nCurrentStep;
    int                     m_nLastCallbackFiredPercentageDone;

    int                     *m_pKillFlag;
};

#endif // !defined(AFX_MACPROGRESSHELPER_H__5ACFF299_B004_4A19_85D9_CC49296D5611__INCLUDED_)
