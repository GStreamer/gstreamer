/* GStreamer
 * Copyright (C) 2007 Sebastien Moutte <sebastien@moutte.net>
 *
 * gstdshowfakesink.h:
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

#include "gstdshowinterface.h"

class CDshowFakeSink :  public CBaseRenderer,
                        public IGstDshowInterface
{
public:
  CDshowFakeSink ();
  virtual ~CDshowFakeSink ();

  static CUnknown * WINAPI CreateInstance (LPUNKNOWN pUnk, HRESULT *pHr);

  virtual HRESULT CheckMediaType (const CMediaType *pmt);
  virtual HRESULT DoRenderSample (IMediaSample *pMediaSample);
  
  STDMETHOD (QueryInterface)(REFIID riid, void **ppvObject);
  ULONG STDMETHODCALLTYPE AddRef();
  ULONG STDMETHODCALLTYPE Release();
  STDMETHOD (gst_set_media_type) (AM_MEDIA_TYPE *pmt);
  STDMETHOD (gst_set_buffer_callback) (push_buffer_func push, byte *data);
  STDMETHOD (gst_push_buffer) (byte *buffer, __int64 start, __int64 stop, unsigned int size, bool discount);
  STDMETHOD (gst_flush) ();
  STDMETHOD (gst_set_sample_size) (unsigned int size);

protected:
  HRESULT m_hres;
  CMediaType m_MediaType;
  push_buffer_func m_callback;
  byte *m_data;
};