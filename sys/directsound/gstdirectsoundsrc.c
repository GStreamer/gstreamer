/*
 * GStreamer
 * Copyright 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright 2005 Sébastien Moutte <sebastien@moutte.net>
 * Copyright 2006 Joni Valtanen <joni.valtanen@movial.fi>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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

/*
  TODO: add device selection and check rate etc.
*/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/gst.h>
#include <gst/audio/gstbaseaudiosrc.h>

#include "gstdirectsoundsrc.h"

#include <windows.h>
#include <dsound.h>

GST_DEBUG_CATEGORY_STATIC (directsoundsrc_debug);
#define GST_CAT_DEFAULT directsoundsrc_debug

/* defaults here */
#define DEFAULT_DEVICE 0

/* properties */
enum
{
  PROP_0,
  PROP_DEVICE
};


static HRESULT (WINAPI * pDSoundCaptureCreate) (LPGUID,
    LPDIRECTSOUNDCAPTURE *, LPUNKNOWN);

static void gst_directsound_src_finalise (GObject * object);

static void gst_directsound_src_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);

static void gst_directsound_src_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_directsound_src_open (GstAudioSrc * asrc);
static gboolean gst_directsound_src_close (GstAudioSrc * asrc);
static gboolean gst_directsound_src_prepare (GstAudioSrc * asrc,
    GstRingBufferSpec * spec);
static gboolean gst_directsound_src_unprepare (GstAudioSrc * asrc);
static void gst_directsound_src_reset (GstAudioSrc * asrc);
static GstCaps *gst_directsound_src_getcaps (GstBaseSrc * bsrc);

static guint gst_directsound_src_read (GstAudioSrc * asrc,
    gpointer data, guint length);

static void gst_directsound_src_dispose (GObject * object);

static void gst_directsound_src_do_init (GType type);

static guint gst_directsound_src_delay (GstAudioSrc * asrc);

static GstStaticPadTemplate directsound_src_src_factory =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, "
        "signed = (boolean) { TRUE, FALSE }, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "rate = (int) [ 1, MAX ], " "channels = (int) [ 1, 2 ]; "
        "audio/x-raw-int, "
        "signed = (boolean) { TRUE, FALSE }, "
        "width = (int) 8, "
        "depth = (int) 8, "
        "rate = (int) [ 1, MAX ], " "channels = (int) [ 1, 2 ]"));

static void
gst_directsound_src_do_init (GType type)
{
  GST_DEBUG_CATEGORY_INIT (directsoundsrc_debug, "directsoundsrc", 0,
      "DirectSound Src");


}

GST_BOILERPLATE_FULL (GstDirectSoundSrc, gst_directsound_src, GstAudioSrc,
    GST_TYPE_AUDIO_SRC, gst_directsound_src_do_init);

static void
gst_directsound_src_dispose (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_directsound_src_finalise (GObject * object)
{
  GstDirectSoundSrc *dsoundsrc = GST_DIRECTSOUND_SRC (object);

  g_mutex_free (dsoundsrc->dsound_lock);
}

static void
gst_directsound_src_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  GST_DEBUG ("initializing directsoundsrc base\n");

  gst_element_class_set_details_simple (element_class, "Direct Sound Audio Src",
      "Source/Audio",
      "Capture from a soundcard via DIRECTSOUND",
      "Joni Valtanen <joni.valtanen@movial.fi>");

  gst_element_class_add_static_pad_template (element_class,
      &directsound_src_src_factory);
}


/* initialize the plugin's class */
static void
gst_directsound_src_class_init (GstDirectSoundSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstBaseAudioSrcClass *gstbaseaudiosrc_class;
  GstAudioSrcClass *gstaudiosrc_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstbaseaudiosrc_class = (GstBaseAudioSrcClass *) klass;
  gstaudiosrc_class = (GstAudioSrcClass *) klass;

  GST_DEBUG ("initializing directsoundsrc class\n");

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_directsound_src_finalise);
  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_directsound_src_dispose);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_directsound_src_get_property);
  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_directsound_src_set_property);

  gstbasesrc_class->get_caps = GST_DEBUG_FUNCPTR (gst_directsound_src_getcaps);

  gstaudiosrc_class->open = GST_DEBUG_FUNCPTR (gst_directsound_src_open);
  gstaudiosrc_class->close = GST_DEBUG_FUNCPTR (gst_directsound_src_close);
  gstaudiosrc_class->read = GST_DEBUG_FUNCPTR (gst_directsound_src_read);
  gstaudiosrc_class->prepare = GST_DEBUG_FUNCPTR (gst_directsound_src_prepare);
  gstaudiosrc_class->unprepare =
      GST_DEBUG_FUNCPTR (gst_directsound_src_unprepare);
  gstaudiosrc_class->delay = GST_DEBUG_FUNCPTR (gst_directsound_src_delay);
  gstaudiosrc_class->reset = GST_DEBUG_FUNCPTR (gst_directsound_src_reset);


}

static GstCaps *
gst_directsound_src_getcaps (GstBaseSrc * bsrc)
{
  GstDirectSoundSrc *dsoundsrc;
  GstCaps *caps = NULL;
  GST_DEBUG ("get caps\n");

  dsoundsrc = GST_DIRECTSOUND_SRC (bsrc);


  caps = gst_caps_copy (gst_pad_get_pad_template_caps (GST_BASE_SRC_PAD
          (bsrc)));
  return caps;

}

static void
gst_directsound_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  //  GstDirectSoundSrc *src = GST_DIRECTSOUND_SRC (object);
  GST_DEBUG ("set property\n");

  switch (prop_id) {
#if 0
      /* FIXME */
    case PROP_DEVICE:
      src->device = g_value_get_uint (value);
      break;
#endif
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_directsound_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
#if 0
  GstDirectSoundSrc *src = GST_DIRECTSOUND_SRC (object);
#endif

  GST_DEBUG ("get property\n");

  switch (prop_id) {
#if 0
      /* FIXME */
    case PROP_DEVICE:
      g_value_set_uint (value, src->device);
      break;
#endif
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


/* initialize the new element
 * instantiate pads and add them to element
 * set functions
 * initialize structure
 */
static void
gst_directsound_src_init (GstDirectSoundSrc * src,
    GstDirectSoundSrcClass * gclass)
{
  GST_DEBUG ("initializing directsoundsrc\n");
  src->dsound_lock = g_mutex_new ();
}



static gboolean
gst_directsound_src_open (GstAudioSrc * asrc)
{
  GstDirectSoundSrc *dsoundsrc;
  HRESULT hRes;                 /* Result for windows functions */

  GST_DEBUG ("initializing directsoundsrc\n");

  dsoundsrc = GST_DIRECTSOUND_SRC (asrc);

  /* Open dsound.dll */
  dsoundsrc->DSoundDLL = LoadLibrary ("dsound.dll");
  if (!dsoundsrc->DSoundDLL) {
    goto dsound_open;
  }

  /* Building the DLL Calls */
  pDSoundCaptureCreate =
      (void *) GetProcAddress (dsoundsrc->DSoundDLL,
      TEXT ("DirectSoundCaptureCreate"));

  /* If everything is not ok */
  if (!pDSoundCaptureCreate) {
    goto capture_function;
  }

  /* FIXME: add here device selection */
  /* Create capture object */
  hRes = pDSoundCaptureCreate (NULL, &dsoundsrc->pDSC, NULL);
  if (FAILED (hRes)) {
    goto capture_object;
  }

  return TRUE;

capture_function:
  {
    FreeLibrary (dsoundsrc->DSoundDLL);
    GST_ELEMENT_ERROR (dsoundsrc, RESOURCE, OPEN_READ,
        ("Unable to get capturecreate function"), (NULL));
    return FALSE;
  }
capture_object:
  {
    FreeLibrary (dsoundsrc->DSoundDLL);
    GST_ELEMENT_ERROR (dsoundsrc, RESOURCE, OPEN_READ,
        ("Unable to create capture object"), (NULL));
    return FALSE;
  }
dsound_open:
  {
    DWORD err = GetLastError ();
    GST_ELEMENT_ERROR (dsoundsrc, RESOURCE, OPEN_READ,
        ("Unable to open dsound.dll"), (NULL));
    g_print ("0x%lx\n", HRESULT_FROM_WIN32 (err));
    return FALSE;
  }
}

static gboolean
gst_directsound_src_close (GstAudioSrc * asrc)
{
  GstDirectSoundSrc *dsoundsrc;
  HRESULT hRes;                 /* Result for windows functions */

  GST_DEBUG ("initializing directsoundsrc\n");

  dsoundsrc = GST_DIRECTSOUND_SRC (asrc);

  /* Release capture handler  */
  hRes = IDirectSoundCapture_Release (dsoundsrc->pDSC);

  /* Close library */
  FreeLibrary (dsoundsrc->DSoundDLL);

  return TRUE;
}

static gboolean
gst_directsound_src_prepare (GstAudioSrc * asrc, GstRingBufferSpec * spec)
{

  GstDirectSoundSrc *dsoundsrc;
  WAVEFORMATEX wfx;             /* Wave format structure */
  HRESULT hRes;                 /* Result for windows functions */
  DSCBUFFERDESC descSecondary;  /* Capturebuffer decsiption */

  dsoundsrc = GST_DIRECTSOUND_SRC (asrc);

  GST_DEBUG ("initializing directsoundsrc\n");

  /* Define buffer */
  memset (&wfx, 0, sizeof (WAVEFORMATEX));
  wfx.wFormatTag = WAVE_FORMAT_PCM;     /* should be WAVE_FORMAT_PCM */
  wfx.nChannels = spec->channels;
  wfx.nSamplesPerSec = spec->rate;      /* 8000|11025|22050|44100 */
  wfx.wBitsPerSample = spec->width;     // 8|16;

  wfx.nBlockAlign = wfx.nChannels * (wfx.wBitsPerSample / 8);
  wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
  wfx.cbSize = 0;               /* This size is allways for PCM-format */

  /* 1 or 2 Channels etc...
     FIXME: Never really tested. Is this ok?
   */
  if (spec->width == 16 && spec->channels == 1) {
    spec->format = GST_S16_LE;
  } else if (spec->width == 16 && spec->channels == 2) {
    spec->format = GST_U16_LE;
  } else if (spec->width == 8 && spec->channels == 1) {
    spec->format = GST_S8;
  } else if (spec->width == 8 && spec->channels == 2) {
    spec->format = GST_U8;
  }

  /* Set the buffer size to two seconds. 
     This should never reached. 
   */
  dsoundsrc->buffer_size = wfx.nAvgBytesPerSec * 2;

  //notifysize * 16; //spec->width; /*original 16*/
  GST_DEBUG ("Buffer size: %d", dsoundsrc->buffer_size);

  /* Init secondary buffer desciption */
  memset (&descSecondary, 0, sizeof (DSCBUFFERDESC));
  descSecondary.dwSize = sizeof (DSCBUFFERDESC);
  descSecondary.dwFlags = 0;
  descSecondary.dwReserved = 0;

  /* This is not primary buffer so have to set size  */
  descSecondary.dwBufferBytes = dsoundsrc->buffer_size;
  descSecondary.lpwfxFormat = &wfx;

  /* Create buffer */
  hRes = IDirectSoundCapture_CreateCaptureBuffer (dsoundsrc->pDSC,
      &descSecondary, &dsoundsrc->pDSBSecondary, NULL);
  if (hRes != DS_OK) {
    goto capture_buffer;
  }

  spec->channels = wfx.nChannels;
  spec->rate = wfx.nSamplesPerSec;
  spec->bytes_per_sample = (spec->width / 8) * spec->channels;
  dsoundsrc->bytes_per_sample = spec->bytes_per_sample;

  GST_DEBUG ("latency time: %" G_GUINT64_FORMAT " - buffer time: %" G_GUINT64_FORMAT,
      spec->latency_time, spec->buffer_time);

  /* Buffer-time should be allways more than 2*latency */
  if (spec->buffer_time < spec->latency_time * 2) {
    spec->buffer_time = spec->latency_time * 2;
    GST_WARNING ("buffer-time was less than latency");
  }

  /* Save the times */
  dsoundsrc->buffer_time = spec->buffer_time;
  dsoundsrc->latency_time = spec->latency_time;

  dsoundsrc->latency_size = (gint) wfx.nAvgBytesPerSec *
      dsoundsrc->latency_time / 1000000.0;


  spec->segsize = (guint) (((double) spec->buffer_time / 1000000.0) *
      wfx.nAvgBytesPerSec);

  /* just in case */
  if (spec->segsize < 1)
    spec->segsize = 1;

  spec->segtotal = spec->width * (wfx.nAvgBytesPerSec / spec->segsize);

  GST_DEBUG ("bytes/sec: %lu, buffer size: %d, segsize: %d, segtotal: %d",
      wfx.nAvgBytesPerSec,
      dsoundsrc->buffer_size, spec->segsize, spec->segtotal);

  spec->silence_sample[0] = 0;
  spec->silence_sample[1] = 0;
  spec->silence_sample[2] = 0;
  spec->silence_sample[3] = 0;

  if (spec->width != 16 && spec->width != 8)
    goto dodgy_width;

  /* Not readed anything yet */
  dsoundsrc->current_circular_offset = 0;

  GST_DEBUG ("GstRingBufferSpec->channels: %d, GstRingBufferSpec->rate: %d, \
GstRingBufferSpec->bytes_per_sample: %d\n\
WAVEFORMATEX.nSamplesPerSec: %ld, WAVEFORMATEX.wBitsPerSample: %d, \
WAVEFORMATEX.nBlockAlign: %d, WAVEFORMATEX.nAvgBytesPerSec: %ld\n", spec->channels, spec->rate, spec->bytes_per_sample, wfx.nSamplesPerSec, wfx.wBitsPerSample, wfx.nBlockAlign, wfx.nAvgBytesPerSec);

  return TRUE;

capture_buffer:
  {
    GST_ELEMENT_ERROR (dsoundsrc, RESOURCE, OPEN_READ,
        ("Unable to create capturebuffer"), (NULL));
    return FALSE;
  }
dodgy_width:
  {
    GST_ELEMENT_ERROR (dsoundsrc, RESOURCE, OPEN_READ,
        ("Unexpected width %d", spec->width), (NULL));
    return FALSE;
  }

}

static gboolean
gst_directsound_src_unprepare (GstAudioSrc * asrc)
{
  GstDirectSoundSrc *dsoundsrc;

  HRESULT hRes;                 /* Result for windows functions */

  /* Resets */
  GST_DEBUG ("unpreparing directsoundsrc");

  dsoundsrc = GST_DIRECTSOUND_SRC (asrc);

  /* Stop capturing */
  hRes = IDirectSoundCaptureBuffer_Stop (dsoundsrc->pDSBSecondary);

  /* Release buffer  */
  hRes = IDirectSoundCaptureBuffer_Release (dsoundsrc->pDSBSecondary);

  return TRUE;

}

/* 
return number of readed bytes */
static guint
gst_directsound_src_read (GstAudioSrc * asrc, gpointer data, guint length)
{
  GstDirectSoundSrc *dsoundsrc;

  HRESULT hRes;                 /* Result for windows functions */
  DWORD dwCurrentCaptureCursor = 0;
  DWORD dwBufferSize = 0;

  LPVOID pLockedBuffer1 = NULL;
  LPVOID pLockedBuffer2 = NULL;
  DWORD dwSizeBuffer1 = 0;
  DWORD dwSizeBuffer2 = 0;

  DWORD dwStatus = 0;

  GST_DEBUG ("reading directsoundsrc\n");

  dsoundsrc = GST_DIRECTSOUND_SRC (asrc);

  GST_DSOUND_LOCK (dsoundsrc);

  /* Get current buffer status */
  hRes = IDirectSoundCaptureBuffer_GetStatus (dsoundsrc->pDSBSecondary,
      &dwStatus);

  /* Starting capturing if not allready */
  if (!(dwStatus & DSCBSTATUS_CAPTURING)) {
    hRes = IDirectSoundCaptureBuffer_Start (dsoundsrc->pDSBSecondary,
        DSCBSTART_LOOPING);
    //    Sleep (dsoundsrc->latency_time/1000);
    GST_DEBUG ("capture started");
  }
  //  calculate_buffersize:
  while (length > dwBufferSize) {
    Sleep (dsoundsrc->latency_time / 1000);

    hRes =
        IDirectSoundCaptureBuffer_GetCurrentPosition (dsoundsrc->pDSBSecondary,
        &dwCurrentCaptureCursor, NULL);

    /* calculate the buffer */
    if (dwCurrentCaptureCursor < dsoundsrc->current_circular_offset) {
      dwBufferSize = dsoundsrc->buffer_size -
          (dsoundsrc->current_circular_offset - dwCurrentCaptureCursor);
    } else {
      dwBufferSize =
          dwCurrentCaptureCursor - dsoundsrc->current_circular_offset;
    }


  }                             // while (...

  /* Lock the buffer */
  hRes = IDirectSoundCaptureBuffer_Lock (dsoundsrc->pDSBSecondary,
      dsoundsrc->current_circular_offset,
      length,
      &pLockedBuffer1, &dwSizeBuffer1, &pLockedBuffer2, &dwSizeBuffer2, 0L);

  /* Copy buffer data to another buffer */
  if (hRes == DS_OK) {
    memcpy (data, pLockedBuffer1, dwSizeBuffer1);
  }

  /* ...and if something is in another buffer */
  if (pLockedBuffer2 != NULL) {
    memcpy (((guchar *) data + dwSizeBuffer1), pLockedBuffer2, dwSizeBuffer2);
  }

  dsoundsrc->current_circular_offset += dwSizeBuffer1 + dwSizeBuffer2;
  dsoundsrc->current_circular_offset %= dsoundsrc->buffer_size;

  IDirectSoundCaptureBuffer_Unlock (dsoundsrc->pDSBSecondary,
      pLockedBuffer1, dwSizeBuffer1, pLockedBuffer2, dwSizeBuffer2);

  GST_DSOUND_UNLOCK (dsoundsrc);

  /* return length (readed data size in bytes) */
  return length;

}

static guint
gst_directsound_src_delay (GstAudioSrc * asrc)
{
  GstDirectSoundSrc *dsoundsrc;
  HRESULT hRes;
  DWORD dwCurrentCaptureCursor;
  DWORD dwBytesInQueue = 0;
  gint nNbSamplesInQueue = 0;

  GST_DEBUG ("Delay\n");

  dsoundsrc = GST_DIRECTSOUND_SRC (asrc);

  /* evaluate the number of samples in queue in the circular buffer */
  hRes =
      IDirectSoundCaptureBuffer_GetCurrentPosition (dsoundsrc->pDSBSecondary,
      &dwCurrentCaptureCursor, NULL);
  /* FIXME: Check is this calculated right */
  if (hRes == S_OK) {
    if (dwCurrentCaptureCursor < dsoundsrc->current_circular_offset) {
      dwBytesInQueue =
          dsoundsrc->buffer_size - (dsoundsrc->current_circular_offset -
          dwCurrentCaptureCursor);
    } else {
      dwBytesInQueue =
          dwCurrentCaptureCursor - dsoundsrc->current_circular_offset;
    }

    nNbSamplesInQueue = dwBytesInQueue / dsoundsrc->bytes_per_sample;
  }

  return nNbSamplesInQueue;
}

static void
gst_directsound_src_reset (GstAudioSrc * asrc)
{
  GstDirectSoundSrc *dsoundsrc;
  LPVOID pLockedBuffer = NULL;
  DWORD dwSizeBuffer = 0;

  GST_DEBUG ("reset directsoundsrc\n");

  dsoundsrc = GST_DIRECTSOUND_SRC (asrc);

#if 0
  IDirectSoundCaptureBuffer_Stop (dsoundsrc->pDSBSecondary);
#endif

  GST_DSOUND_LOCK (dsoundsrc);

  if (dsoundsrc->pDSBSecondary) {
    /*stop capturing */
    HRESULT hRes = IDirectSoundCaptureBuffer_Stop (dsoundsrc->pDSBSecondary);

    /*reset position */
    /*    hRes = IDirectSoundCaptureBuffer_SetCurrentPosition (dsoundsrc->pDSBSecondary, 0); */

    /*reset the buffer */
    hRes = IDirectSoundCaptureBuffer_Lock (dsoundsrc->pDSBSecondary,
        dsoundsrc->current_circular_offset, dsoundsrc->buffer_size,
        pLockedBuffer, &dwSizeBuffer, NULL, NULL, 0L);

    if (SUCCEEDED (hRes)) {
      memset (pLockedBuffer, 0, dwSizeBuffer);

      hRes =
          IDirectSoundCaptureBuffer_Unlock (dsoundsrc->pDSBSecondary,
          pLockedBuffer, dwSizeBuffer, NULL, 0);
    }
    dsoundsrc->current_circular_offset = 0;

  }

  GST_DSOUND_UNLOCK (dsoundsrc);
}
