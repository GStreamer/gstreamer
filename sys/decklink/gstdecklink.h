/* GStreamer
 * Copyright (C) 2011 David Schleef <ds@schleef.org>
 * Copyright (C) 2014 Sebastian Dröge <sebastian@centricular.com>
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

#ifndef _GST_DECKLINK_H_
#define _GST_DECKLINK_H_

#include <gst/gst.h>
#ifdef G_OS_UNIX
#include "linux/DeckLinkAPI.h"
#endif

#ifdef G_OS_WIN32
#include "win/DeckLinkAPI.h"

#include <comutil.h>

#define bool BOOL

#define COMSTR_T BSTR*
#define FREE_COM_STRING(s) delete[] s;
#define CONVERT_COM_STRING(s) BSTR _s = (BSTR)s; s = _com_util::ConvertBSTRToString(_s); ::SysFreeString(_s);
#else
#define COMSTR_T char*
#define FREE_COM_STRING(s) free ((void *) s)
#define CONVERT_COM_STRING(s)
#endif /* _MSC_VER */

typedef enum {
  GST_DECKLINK_MODE_NTSC,
  GST_DECKLINK_MODE_NTSC2398,
  GST_DECKLINK_MODE_PAL,
  GST_DECKLINK_MODE_NTSC_P,
  GST_DECKLINK_MODE_PAL_P,

  GST_DECKLINK_MODE_1080p2398,
  GST_DECKLINK_MODE_1080p24,
  GST_DECKLINK_MODE_1080p25,
  GST_DECKLINK_MODE_1080p2997,
  GST_DECKLINK_MODE_1080p30,

  GST_DECKLINK_MODE_1080i50,
  GST_DECKLINK_MODE_1080i5994,
  GST_DECKLINK_MODE_1080i60,

  GST_DECKLINK_MODE_1080p50,
  GST_DECKLINK_MODE_1080p5994,
  GST_DECKLINK_MODE_1080p60,

  GST_DECKLINK_MODE_720p50,
  GST_DECKLINK_MODE_720p5994,
  GST_DECKLINK_MODE_720p60,

  GST_DECKLINK_MODE_2048p2398,
  GST_DECKLINK_MODE_2048p24,
  GST_DECKLINK_MODE_2048p25
} GstDecklinkModeEnum;
#define GST_TYPE_DECKLINK_MODE (gst_decklink_mode_get_type ())
GType gst_decklink_mode_get_type (void);

typedef enum {
  GST_DECKLINK_CONNECTION_SDI,
  GST_DECKLINK_CONNECTION_HDMI,
  GST_DECKLINK_CONNECTION_OPTICAL_SDI,
  GST_DECKLINK_CONNECTION_COMPONENT,
  GST_DECKLINK_CONNECTION_COMPOSITE,
  GST_DECKLINK_CONNECTION_SVIDEO
} GstDecklinkConnectionEnum;
#define GST_TYPE_DECKLINK_CONNECTION (gst_decklink_connection_get_type ())
GType gst_decklink_connection_get_type (void);

typedef enum {
  GST_DECKLINK_AUDIO_CONNECTION_AUTO,
  GST_DECKLINK_AUDIO_CONNECTION_EMBEDDED,
  GST_DECKLINK_AUDIO_CONNECTION_AES_EBU,
  GST_DECKLINK_AUDIO_CONNECTION_ANALOG
} GstDecklinkAudioConnectionEnum;
#define GST_TYPE_DECKLINK_AUDIO_CONNECTION (gst_decklink_audio_connection_get_type ())
GType gst_decklink_audio_connection_get_type (void);

typedef struct _GstDecklinkMode GstDecklinkMode;
struct _GstDecklinkMode {
  BMDDisplayMode mode;
  int width;
  int height;
  int fps_n;
  int fps_d;
  gboolean interlaced;
  int par_n;
  int par_d;
  gboolean tff;
  gboolean is_hdtv;
};

const GstDecklinkMode * gst_decklink_get_mode (GstDecklinkModeEnum e);
GstCaps * gst_decklink_mode_get_caps (GstDecklinkModeEnum e);
GstCaps * gst_decklink_mode_get_template_caps (void);

typedef struct _GstDecklinkOutput GstDecklinkOutput;
struct _GstDecklinkOutput {
  IDeckLink *device;
  IDeckLinkOutput *output;
  GstClock *clock;

  /* Everything below protected by mutex */
  GMutex lock;

  /* Set by the audio sink */
  GstClock *audio_clock;

  /* <private> */
  GstElement *audiosink;
  GstElement *videosink;
};

typedef struct _GstDecklinkInput GstDecklinkInput;
struct _GstDecklinkInput {
  IDeckLink *device;
  IDeckLinkInput *input;
  IDeckLinkConfiguration *config;
  GstClock *clock;

  /* Everything below protected by mutex */
  GMutex lock;

  /* Set by the audio source */
  GstClock *audio_clock;

  /* <private> */
  GstElement *audiosrc;
  GstElement *videosrc;
};

GstDecklinkOutput * gst_decklink_acquire_nth_output (gint n, GstElement * sink, gboolean is_audio);
void                gst_decklink_release_nth_output (gint n, GstElement * sink, gboolean is_audio);

void                gst_decklink_output_set_audio_clock (GstDecklinkOutput * output, GstClock * clock);
GstClock *          gst_decklink_output_get_audio_clock (GstDecklinkOutput * output);

#endif
