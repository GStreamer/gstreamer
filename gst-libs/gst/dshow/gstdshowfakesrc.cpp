/* GStreamer
 * Copyright (C) 2007 Sebastien Moutte <sebastien@moutte.net>
 *
 * gstdshowfakesrc.cpp:
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "gstdshowfakesrc.h"

static CCritSec g_pCriticSec;

/* output pin*/
CDshowFakeOutputPin::CDshowFakeOutputPin (CBaseFilter *pFilter, CCritSec *sec):
  CBaseOutputPin("FakeOutputPin", pFilter, sec, &m_hres, L"output")
{
}

CDshowFakeOutputPin::~CDshowFakeOutputPin()
{

}

HRESULT CDshowFakeOutputPin::GetMediaType(int iPosition, CMediaType *pMediaType)
{
  if(iPosition == 0) {
    *pMediaType = m_MediaType;
    return S_OK;
  }
  
  return VFW_S_NO_MORE_ITEMS;
}

HRESULT CDshowFakeOutputPin::CheckMediaType(const CMediaType *pmt)
{
    if (m_MediaType == *pmt) {
        return S_OK;
    }

    return S_FALSE;
}

HRESULT CDshowFakeOutputPin::DecideBufferSize (IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *ppropInputRequest)
{
  ALLOCATOR_PROPERTIES properties;
  ppropInputRequest->cbBuffer = m_SampleSize;
  ppropInputRequest->cBuffers = 1;
  HRESULT hres = pAlloc->SetProperties(ppropInputRequest, &properties);
  pAlloc->Commit();

  return S_OK;
}

STDMETHODIMP CDshowFakeOutputPin::SetMediaType (AM_MEDIA_TYPE *pmt)
{
  m_MediaType.Set (*pmt);
  m_SampleSize = m_MediaType.GetSampleSize();
  return S_OK;
}

STDMETHODIMP CDshowFakeOutputPin::PushBuffer(byte *buffer, __int64 start, __int64 stop, unsigned int size, bool discount)
{
  IMediaSample *pSample = NULL;
  
  if (start != -1) {
    start /= 100;
    stop /= 100;
  }

  HRESULT hres = GetDeliveryBuffer(&pSample, NULL, NULL, 0);
  if (hres == S_OK && pSample)
  {
    BYTE *sample_buffer;
    pSample->GetPointer(&sample_buffer);
    if(sample_buffer)
    {
      memcpy (sample_buffer, buffer, size);
      pSample->SetActualDataLength(size);
    }
    if (discount)
      pSample->SetDiscontinuity(TRUE);
    else
      pSample->SetDiscontinuity(FALSE);
    
    pSample->SetSyncPoint(TRUE);
    pSample->SetPreroll(FALSE);
  
    if (start != -1)
      pSample->SetTime(&start, &stop);

    hres = Deliver(pSample);
    pSample->Release();
  }

  return S_OK;
}

STDMETHODIMP CDshowFakeOutputPin::Flush ()
{
  DeliverBeginFlush();
  DeliverEndFlush();
  return S_OK;
}

STDMETHODIMP CDshowFakeOutputPin::SetSampleSize (unsigned int size)
{
  m_SampleSize = size;
  return S_OK;
}

/* filter */
CDshowFakeSrc::CDshowFakeSrc():CBaseFilter("DshowFakeSink", NULL, &g_pCriticSec, CLSID_DshowFakeSrc)
{
  m_pOutputPin = new CDshowFakeOutputPin((CSource *)this, m_pLock);
}

CDshowFakeSrc::~CDshowFakeSrc()
{
  if (m_pOutputPin)
    delete m_pOutputPin;
}

//Object creation.
CUnknown* WINAPI CDshowFakeSrc::CreateInstance(LPUNKNOWN pUnk, HRESULT *pHr) 
{
	CDshowFakeSrc *pNewObject = new CDshowFakeSrc();
  if (pNewObject == NULL) {
    *pHr = E_OUTOFMEMORY;
  }
  return pNewObject;
} 

int CDshowFakeSrc::GetPinCount()
{
  return 1;
}

CBasePin *CDshowFakeSrc::GetPin(int n)
{
  return (CBasePin *)m_pOutputPin;
}


STDMETHODIMP CDshowFakeSrc::QueryInterface(REFIID riid, void **ppvObject)
{
  if (riid == IID_IGstDshowInterface) {
      *ppvObject = (IGstDshowInterface*) this;
      AddRef();
      return S_OK;
  }
  else
    return CBaseFilter::QueryInterface (riid, ppvObject);
}

ULONG STDMETHODCALLTYPE CDshowFakeSrc::AddRef()
{
  return CBaseFilter::AddRef();
}

ULONG STDMETHODCALLTYPE CDshowFakeSrc::Release()
{
  return CBaseFilter::Release();
}

STDMETHODIMP CDshowFakeSrc::gst_set_media_type (AM_MEDIA_TYPE *pmt)
{
  m_pOutputPin->SetMediaType(pmt); 
  return S_OK;
}

STDMETHODIMP CDshowFakeSrc::gst_set_buffer_callback (push_buffer_func push, byte *data)
{
  return E_NOTIMPL;
}

STDMETHODIMP CDshowFakeSrc::gst_push_buffer (byte *buffer, __int64 start, __int64 stop, unsigned int size, bool discount)
{
  m_pOutputPin->PushBuffer(buffer, start, stop, size, discount);
  return S_OK;
}

STDMETHODIMP CDshowFakeSrc::gst_flush ()
{
  m_pOutputPin->Flush();
  return S_OK;
}

STDMETHODIMP CDshowFakeSrc::gst_set_sample_size(unsigned int size)
{
  m_pOutputPin->SetSampleSize(size);
  return S_OK;
}