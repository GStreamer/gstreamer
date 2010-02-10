/* GStreamer FAAD (Free AAC Decoder) plugin
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#ifndef __GST_FAAD_H__
#define __GST_FAAD_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#ifdef FAAD_IS_NEAAC
#include <neaacdec.h>
#else
#include <faad.h>
#endif

G_BEGIN_DECLS

#define GST_TYPE_FAAD \
  (gst_faad_get_type ())
#define GST_FAAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_FAAD, GstFaad))
#define GST_FAAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_FAAD, GstFaadClass))
#define GST_IS_FAAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_FAAD))
#define GST_IS_FAAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_FAAD))

typedef struct _GstFaad {
  GstElement element;

  GstPad    *srcpad;
  GstPad    *sinkpad;

  guint      samplerate; /* sample rate of the last MPEG frame    */
  guint      channels;   /* number of channels of the last frame  */
  guint      bps;        /* bytes per sample                      */
  guchar    *channel_positions;

  guint8     fake_codec_data[2];

  GstAdapter *adapter;

  /* FAAD object */
  faacDecHandle handle;
  gboolean init;

  gboolean packetised; /* We must differentiate between raw and packetised streams */

  gint64  prev_ts;     /* timestamp of previous buffer                    */
  gint64  next_ts;     /* timestamp of next buffer                        */
  guint64 bytes_in;    /* bytes received                                  */
  guint64 sum_dur_out; /* sum of durations of decoded buffers we sent out */
  gint    error_count;
  gboolean discont;

  /* segment handling */
  GstSegment segment;

  /* list of raw output buffers for reverse playback */
  GList *queued;
  /* gather/decode queues for reverse playback */
  GList *gather;
  GList *decode;
} GstFaad;

typedef struct _GstFaadClass {
  GstElementClass parent_class;
} GstFaadClass;

GType gst_faad_get_type (void);

G_END_DECLS

#endif /* __GST_FAAD_H__ */
