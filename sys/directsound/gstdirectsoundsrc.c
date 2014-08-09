/*
 * GStreamer
 * Copyright 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright 2005 S�bastien Moutte <sebastien@moutte.net>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/*
  TODO: add device selection and check rate etc.
*/

/**
 * SECTION:element-directsoundsrc
 *
 * Reads audio data using the DirectSound API.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch -v directsoundsrc ! audioconvert ! vorbisenc ! oggmux ! filesink location=dsound.ogg
 * ]| Record from DirectSound and encode to Ogg/Vorbis.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <gst/audio/gstaudiobasesrc.h>

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
  PROP_DEVICE_NAME
};

static HRESULT (WINAPI * pDSoundCaptureCreate) (LPGUID,
    LPDIRECTSOUNDCAPTURE *, LPUNKNOWN);

static void gst_directsound_src_finalize (GObject * object);

static void gst_directsound_src_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);

static void gst_directsound_src_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_directsound_src_open (GstAudioSrc * asrc);
static gboolean gst_directsound_src_close (GstAudioSrc * asrc);
static gboolean gst_directsound_src_prepare (GstAudioSrc * asrc,
    GstAudioRingBufferSpec * spec);
static gboolean gst_directsound_src_unprepare (GstAudioSrc * asrc);
static void gst_directsound_src_reset (GstAudioSrc * asrc);
static GstCaps *gst_directsound_src_getcaps (GstBaseSrc * bsrc,
    GstCaps * filter);

static guint gst_directsound_src_read (GstAudioSrc * asrc,
    gpointer data, guint length, GstClockTime * timestamp);

static void gst_directsound_src_dispose (GObject * object);

static guint gst_directsound_src_delay (GstAudioSrc * asrc);

static GstStaticPadTemplate directsound_src_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw, "
        "format = (string) { S16LE, S8 }, "
        "layout = (string) interleaved, "
        "rate = (int) [ 1, MAX ], " "channels = (int) [ 1, 2 ]"));

#define gst_directsound_src_parent_class parent_class
G_DEFINE_TYPE (GstDirectSoundSrc, gst_directsound_src, GST_TYPE_AUDIO_SRC);

static void
gst_directsound_src_dispose (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_directsound_src_finalize (GObject * object)
{
  GstDirectSoundSrc *dsoundsrc = GST_DIRECTSOUND_SRC (object);

  g_mutex_clear (&dsoundsrc->dsound_lock);

  g_free (dsoundsrc->device_name);
  g_free (dsoundsrc->device_guid);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_directsound_src_class_init (GstDirectSoundSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstAudioSrcClass *gstaudiosrc_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstaudiosrc_class = (GstAudioSrcClass *) klass;

  GST_DEBUG_CATEGORY_INIT (directsoundsrc_debug, "directsoundsrc", 0,
      "DirectSound Src");

  GST_DEBUG ("initializing directsoundsrc class");

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_directsound_src_finalize);
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

  gst_element_class_set_static_metadata (gstelement_class,
      "DirectSound audio source", "Source/Audio",
      "Capture from a soundcard via DirectSound",
      "Joni Valtanen <joni.valtanen@movial.fi>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&directsound_src_src_factory));

  g_object_class_install_property
      (gobject_class, PROP_DEVICE_NAME,
      g_param_spec_string ("device-name", "Device name",
          "Human-readable name of the sound device", NULL, G_PARAM_READWRITE));
}

static GstCaps *
gst_directsound_src_getcaps (GstBaseSrc * bsrc, GstCaps * filter)
{
  GstCaps *caps = NULL;
  GST_DEBUG_OBJECT (bsrc, "get caps");

  caps = gst_pad_get_pad_template_caps (GST_BASE_SRC_PAD (bsrc));
  return caps;
}

static void
gst_directsound_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDirectSoundSrc *src = GST_DIRECTSOUND_SRC (object);
  GST_DEBUG ("set property");

  switch (prop_id) {
    case PROP_DEVICE_NAME:
      if (src->device_name) {
        g_free (src->device_name);
        src->device_name = NULL;
      }
      if (g_value_get_string (value)) {
        src->device_name = g_strdup (g_value_get_string (value));
      }

      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_directsound_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDirectSoundSrc *src = GST_DIRECTSOUND_SRC (object);

  GST_DEBUG ("get property");

  switch (prop_id) {
    case PROP_DEVICE_NAME:
      g_value_set_string (value, src->device_name);
      break;
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
gst_directsound_src_init (GstDirectSoundSrc * src)
{
  GST_DEBUG_OBJECT (src, "initializing directsoundsrc");
  g_mutex_init (&src->dsound_lock);
  src->device_guid = NULL;
  src->device_name = NULL;
}


/* Enumeration callback called by DirectSoundCaptureEnumerate.
 * Gets the GUID of request audio device
 */
static BOOL CALLBACK
gst_directsound_enum_callback (GUID * pGUID, TCHAR * strDesc,
    TCHAR * strDrvName, VOID * pContext)
{
  GstDirectSoundSrc *dsoundsrc = GST_DIRECTSOUND_SRC (pContext);

  if (pGUID && dsoundsrc && dsoundsrc->device_name &&
      !g_strcmp0 (dsoundsrc->device_name, strDesc)) {
    g_free (dsoundsrc->device_guid);
    dsoundsrc->device_guid = (GUID *) g_malloc0 (sizeof (GUID));
    memcpy (dsoundsrc->device_guid, pGUID, sizeof (GUID));
    GST_INFO_OBJECT (dsoundsrc, "found the requested audio device :%s",
        dsoundsrc->device_name);
    return FALSE;
  }

  GST_INFO_OBJECT (dsoundsrc, "sound device names: %s, %s, requested device:%s",
      strDesc, strDrvName, dsoundsrc->device_name);

  return TRUE;
}

static gboolean
gst_directsound_src_open (GstAudioSrc * asrc)
{
  GstDirectSoundSrc *dsoundsrc;
  HRESULT hRes;                 /* Result for windows functions */

  GST_DEBUG_OBJECT (asrc, "opening directsoundsrc");

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

  hRes = DirectSoundCaptureEnumerate ((LPDSENUMCALLBACK)
      gst_directsound_enum_callback, (VOID *) dsoundsrc);
  if (FAILED (hRes)) {
    goto capture_enumerate;
  }

  /* Create capture object */
  hRes = pDSoundCaptureCreate (dsoundsrc->device_guid, &dsoundsrc->pDSC, NULL);
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
capture_enumerate:
  {
    FreeLibrary (dsoundsrc->DSoundDLL);
    GST_ELEMENT_ERROR (dsoundsrc, RESOURCE, OPEN_READ,
        ("Unable to enumerate audio capture devices"), (NULL));
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

  GST_DEBUG_OBJECT (asrc, "closing directsoundsrc");

  dsoundsrc = GST_DIRECTSOUND_SRC (asrc);

  /* Release capture handler  */
  IDirectSoundCapture_Release (dsoundsrc->pDSC);

  /* Close library */
  FreeLibrary (dsoundsrc->DSoundDLL);

  return TRUE;
}

static gboolean
gst_directsound_src_prepare (GstAudioSrc * asrc, GstAudioRingBufferSpec * spec)
{
  GstDirectSoundSrc *dsoundsrc;
  WAVEFORMATEX wfx;             /* Wave format structure */
  HRESULT hRes;                 /* Result for windows functions */
  DSCBUFFERDESC descSecondary;  /* Capturebuffer description */

  dsoundsrc = GST_DIRECTSOUND_SRC (asrc);

  GST_DEBUG_OBJECT (asrc, "preparing directsoundsrc");

  /* Define buffer */
  memset (&wfx, 0, sizeof (WAVEFORMATEX));
  wfx.wFormatTag = WAVE_FORMAT_PCM;
  wfx.nChannels = GST_AUDIO_INFO_CHANNELS (&spec->info);
  wfx.nSamplesPerSec = GST_AUDIO_INFO_RATE (&spec->info);
  wfx.wBitsPerSample = GST_AUDIO_INFO_BPF (&spec->info) * 8 / wfx.nChannels;
  wfx.nBlockAlign = GST_AUDIO_INFO_BPF (&spec->info);
  wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
  /* Ignored for WAVE_FORMAT_PCM. */
  wfx.cbSize = 0;

  if (wfx.wBitsPerSample != 16 && wfx.wBitsPerSample != 8)
    goto dodgy_width;

  /* Set the buffer size to two seconds. 
     This should never reached. 
   */
  dsoundsrc->buffer_size = wfx.nAvgBytesPerSec * 2;

  GST_DEBUG_OBJECT (asrc, "Buffer size: %d", dsoundsrc->buffer_size);

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
  if (hRes != DS_OK)
    goto capture_buffer;

  dsoundsrc->bytes_per_sample = GST_AUDIO_INFO_BPF (&spec->info);

  GST_DEBUG ("latency time: %" G_GUINT64_FORMAT " - buffer time: %"
      G_GUINT64_FORMAT, spec->latency_time, spec->buffer_time);

  /* Buffer-time should be always more than 2*latency */
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

  spec->segtotal = GST_AUDIO_INFO_BPF (&spec->info) * 8 *
      (wfx.nAvgBytesPerSec / spec->segsize);

  GST_DEBUG_OBJECT (asrc,
      "bytes/sec: %lu, buffer size: %d, segsize: %d, segtotal: %d",
      wfx.nAvgBytesPerSec, dsoundsrc->buffer_size, spec->segsize,
      spec->segtotal);

  /* Not read anything yet */
  dsoundsrc->current_circular_offset = 0;

  GST_DEBUG_OBJECT (asrc, "channels: %d, rate: %d, bytes_per_sample: %d"
      " WAVEFORMATEX.nSamplesPerSec: %ld, WAVEFORMATEX.wBitsPerSample: %d,"
      " WAVEFORMATEX.nBlockAlign: %d, WAVEFORMATEX.nAvgBytesPerSec: %ld",
      GST_AUDIO_INFO_CHANNELS (&spec->info), GST_AUDIO_INFO_RATE (&spec->info),
      GST_AUDIO_INFO_BPF (&spec->info), wfx.nSamplesPerSec, wfx.wBitsPerSample,
      wfx.nBlockAlign, wfx.nAvgBytesPerSec);

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
        ("Unexpected width %d", wfx.wBitsPerSample), (NULL));
    return FALSE;
  }
}

static gboolean
gst_directsound_src_unprepare (GstAudioSrc * asrc)
{
  GstDirectSoundSrc *dsoundsrc;

  GST_DEBUG_OBJECT (asrc, "unpreparing directsoundsrc");

  dsoundsrc = GST_DIRECTSOUND_SRC (asrc);

  /* Stop capturing */
  IDirectSoundCaptureBuffer_Stop (dsoundsrc->pDSBSecondary);

  /* Release buffer  */
  IDirectSoundCaptureBuffer_Release (dsoundsrc->pDSBSecondary);

  return TRUE;
}

/* 
return number of readed bytes */
static guint
gst_directsound_src_read (GstAudioSrc * asrc, gpointer data, guint length,
    GstClockTime * timestamp)
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

  GST_DEBUG_OBJECT (asrc, "reading directsoundsrc");

  dsoundsrc = GST_DIRECTSOUND_SRC (asrc);

  GST_DSOUND_LOCK (dsoundsrc);

  /* Get current buffer status */
  hRes = IDirectSoundCaptureBuffer_GetStatus (dsoundsrc->pDSBSecondary,
      &dwStatus);

  /* Starting capturing if not already */
  if (!(dwStatus & DSCBSTATUS_CAPTURING)) {
    hRes = IDirectSoundCaptureBuffer_Start (dsoundsrc->pDSBSecondary,
        DSCBSTART_LOOPING);
    //    Sleep (dsoundsrc->latency_time/1000);
    GST_DEBUG_OBJECT (asrc, "capture started");
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

  GST_DEBUG_OBJECT (asrc, "Delay");

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

  GST_DEBUG_OBJECT (asrc, "reset directsoundsrc");

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
