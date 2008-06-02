/* GStreamer
 * Copyright (C) 2008 Michael Smith <msmith@songbirdnest.com>
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
#ifndef __DSHOWVIDEOFAKESRC_H__
#define __DSHOWVIDEOFAKESRC_H__

#include <streams.h>
#include <gst/gst.h>

class VideoFakeSrcPin : public CBaseOutputPin
{
protected:
  /* members */
  CMediaType m_MediaType;
  unsigned int m_SampleSize;

public:
  /* methods */
  VideoFakeSrcPin (CBaseFilter *pFilter, CCritSec *sec, HRESULT *hres);
  ~VideoFakeSrcPin ();

  STDMETHODIMP CopyToDestinationBuffer (byte *src, byte *dst);
  GstFlowReturn (PushBuffer) (GstBuffer *buf);

  /* Overrides */
  virtual HRESULT CheckMediaType(const CMediaType *pmt);
  HRESULT GetMediaType(int iPosition, CMediaType *pMediaType);
  virtual HRESULT DecideBufferSize (IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *ppropInputRequest);
  STDMETHOD (SetMediaType) (AM_MEDIA_TYPE *pmt);
  STDMETHOD (Flush) ();
  STDMETHODIMP Notify(IBaseFilter * pSender, Quality q);


};

class VideoFakeSrc : public CBaseFilter
{
private:
  /* members */
  CCritSec m_critsec;
  VideoFakeSrcPin *m_pOutputPin;

public:
  /* methods */
  VideoFakeSrc  (void);
  ~VideoFakeSrc (void) {};

  VideoFakeSrcPin *GetOutputPin();

  /* Overrides */
  int       GetPinCount();
  CBasePin *GetPin(int n);
};

#endif /* __DSHOWVIDEOFAKESRC_H__ */