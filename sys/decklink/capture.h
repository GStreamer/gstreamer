#ifndef __CAPTURE_H__
#define __CAPTURE_H__

#include "gstdecklink.h"

class DeckLinkCaptureDelegate : public IDeckLinkInputCallback
{
  public:
    DeckLinkCaptureDelegate();
    ~DeckLinkCaptureDelegate();

    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, LPVOID *ppv) { return E_NOINTERFACE; }
    virtual ULONG STDMETHODCALLTYPE AddRef(void);
    virtual ULONG STDMETHODCALLTYPE  Release(void);
    virtual HRESULT STDMETHODCALLTYPE VideoInputFormatChanged(BMDVideoInputFormatChangedEvents, IDeckLinkDisplayMode*, BMDDetectedVideoInputFormatFlags);
    virtual HRESULT STDMETHODCALLTYPE VideoInputFrameArrived(IDeckLinkVideoInputFrame*, IDeckLinkAudioInputPacket*);

    void *priv;

  private:
    ULONG m_refCount;
    GMutex m_mutex;
};

#endif
