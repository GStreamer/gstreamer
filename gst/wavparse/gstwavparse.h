/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


#ifndef __GST_WAVPARSE_H__
#define __GST_WAVPARSE_H__


#include <gst/gst.h>
#include "gst/riff/riff-ids.h"
#include "gst/riff/riff-read.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_WAVPARSE \
  (gst_wavparse_get_type())
#define GST_WAVPARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_WAVPARSE,GstWavParse))
#define GST_WAVPARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_WAVPARSE,GstWavParse))
#define GST_IS_WAVPARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_WAVPARSE))
#define GST_IS_WAVPARSE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_WAVPARSE))

typedef enum {
  GST_WAVPARSE_START,
  GST_WAVPARSE_FMT,
  GST_WAVPARSE_OTHER,
  GST_WAVPARSE_DATA,
} GstWavParseState;

typedef struct _GstWavParse GstWavParse;
typedef struct _GstWavParseClass GstWavParseClass;

struct _GstWavParse {
  GstRiffRead parent;

  /* pads */
  GstPad *sinkpad,*srcpad;

  /* WAVE decoding state */
  GstWavParseState state;

  /* format of audio, see defines below */
  gint format;

  /* useful audio data */
  guint16 depth;
  gint rate;
  guint16 channels;
  guint16 width;
  guint32 bps;

  guint64 dataleft, datasize, datastart;
  int byteoffset;
  
  gboolean seek_pending;
  guint64 seek_offset;
};

struct _GstWavParseClass {
  GstElementClass parent_class;
};

GType gst_wavparse_get_type(void);

typedef struct _gst_riff_fmt {
  gint16 wFormatTag;
  guint16 wChannels;
  guint32 dwSamplesPerSec;
  guint32 dwAvgBytesPerSec;
  guint16 wBlockAlign;
  guint16 wBitsPerSample;
} gst_riff_fmt;
  
  
/**** from public Microsoft RIFF docs ******/
#define GST_RIFF_WAVE_FORMAT_UNKNOWN        (0x0000)
#define GST_RIFF_WAVE_FORMAT_PCM            (0x0001)
#define GST_RIFF_WAVE_FORMAT_ADPCM          (0x0002)
#define GST_RIFF_WAVE_FORMAT_IBM_CVSD       (0x0005)
#define GST_RIFF_WAVE_FORMAT_ALAW           (0x0006)
#define GST_RIFF_WAVE_FORMAT_MULAW          (0x0007)
#define GST_RIFF_WAVE_FORMAT_OKI_ADPCM      (0x0010)
#define GST_RIFF_WAVE_FORMAT_DVI_ADPCM      (0x0011)
#define GST_RIFF_WAVE_FORMAT_DIGISTD        (0x0015)
#define GST_RIFF_WAVE_FORMAT_DIGIFIX        (0x0016)
#define GST_RIFF_WAVE_FORMAT_YAMAHA_ADPCM   (0x0020)
#define GST_RIFF_WAVE_FORMAT_DSP_TRUESPEECH (0x0022)
#define GST_RIFF_WAVE_FORMAT_GSM610         (0x0031)
#define GST_RIFF_WAVE_FORMAT_MSN            (0x0032)
#define GST_RIFF_WAVE_FORMAT_MPEGL12        (0x0050)
#define GST_RIFF_WAVE_FORMAT_MPEGL3         (0x0055)
#define GST_RIFF_IBM_FORMAT_MULAW           (0x0101)
#define GST_RIFF_IBM_FORMAT_ALAW            (0x0102)
#define GST_RIFF_IBM_FORMAT_ADPCM           (0x0103)
#define GST_RIFF_WAVE_FORMAT_WMAV1          (0x0160)
#define GST_RIFF_WAVE_FORMAT_WMAV2          (0x0161)
#define GST_RIFF_WAVE_FORMAT_WMAV3          (0x0162)
#define GST_RIFF_WAVE_FORMAT_VORBIS1        (0x674f)
#define GST_RIFF_WAVE_FORMAT_VORBIS2        (0x6750)
#define GST_RIFF_WAVE_FORMAT_VORBIS3        (0x6751)
#define GST_RIFF_WAVE_FORMAT_VORBIS1PLUS    (0x676f)
#define GST_RIFF_WAVE_FORMAT_VORBIS2PLUS    (0x6770)
#define GST_RIFF_WAVE_FORMAT_VORBIS3PLUS    (0x6771)

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_WAVPARSE_H__ */
