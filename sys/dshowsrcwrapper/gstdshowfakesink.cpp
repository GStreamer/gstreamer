/* GStreamer
 * Copyright (C) 2007 Sebastien Moutte <sebastien@moutte.net>
 *
 * gstdshowfakesink.cpp:
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

#include "gstdshowfakesink.h"


CDshowFakeSink::CDshowFakeSink()
  : m_hres(S_OK), CBaseRenderer(CLSID_DshowFakeSink, "DshowFakeSink", NULL, &m_hres)
{
  m_callback = NULL;
}

CDshowFakeSink::~CDshowFakeSink()
{

}

//Object creation.
CUnknown* WINAPI CDshowFakeSink::CreateInstance(LPUNKNOWN pUnk, HRESULT *pHr) 
{
	CDshowFakeSink *pNewObject = new CDshowFakeSink();
  g_print ("CDshowFakeSink::CreateInstance\n");
  if (pNewObject == NULL) {
    *pHr = E_OUTOFMEMORY;
  }
  return pNewObject;
} 

STDMETHODIMP CDshowFakeSink::QueryInterface(REFIID riid, void **ppvObject)
{
  if (riid == IID_IGstDshowInterface) {
      *ppvObject = (IGstDshowInterface*) this;
      AddRef();
      return S_OK;
  }
  else
    return CBaseRenderer::QueryInterface (riid, ppvObject);
}

ULONG STDMETHODCALLTYPE CDshowFakeSink::AddRef()
{
  return CBaseRenderer::AddRef();
}

ULONG STDMETHODCALLTYPE CDshowFakeSink::Release()
{
  return CBaseRenderer::Release();
}


STDMETHODIMP CDshowFakeSink::gst_set_media_type (AM_MEDIA_TYPE *pmt)
{
  m_MediaType.Set (*pmt);
  return S_OK;
}

STDMETHODIMP CDshowFakeSink::gst_set_buffer_callback (push_buffer_func push, byte *data)
{
  m_callback = push;
  m_data = data;
  return S_OK;
}

STDMETHODIMP CDshowFakeSink::gst_push_buffer (byte *buffer, __int64 start, __int64 stop, unsigned int size, bool discount)
{
  return E_NOTIMPL;
}

STDMETHODIMP CDshowFakeSink::gst_flush ()
{
  return E_NOTIMPL;
}

STDMETHODIMP CDshowFakeSink::gst_set_sample_size(unsigned int size)
{
  return E_NOTIMPL;
}

HRESULT CDshowFakeSink::CheckMediaType(const CMediaType *pmt)
{
  VIDEOINFOHEADER *p1;
  VIDEOINFOHEADER *p2;
  if(pmt != NULL)
  {
    p1 = (VIDEOINFOHEADER *)pmt->Format();
    p2 = (VIDEOINFOHEADER *)m_MediaType.Format();
    if (*pmt == m_MediaType)
      return S_OK;
  }

  return S_FALSE;
}

HRESULT CDshowFakeSink::DoRenderSample(IMediaSample *pMediaSample)
{
  if(pMediaSample && m_callback)
  {
    BYTE *pBuffer = NULL;
    LONGLONG lStart = 0, lStop = 0;
    pMediaSample->GetPointer(&pBuffer);
    long size = pMediaSample->GetActualDataLength();
    pMediaSample->GetTime(&lStart, &lStop);
    lStart*=100;
    lStop*=100;
    m_callback(pBuffer, size, m_data, lStart, lStop);
  }
  
  return S_OK;
}
