/* GStreamer TTA plugin
 * (c) 2004 Arwed v. Merkatz <v.merkatz@gmx.net>
 *
 * gstttadec.h: raw TTA bitstream decoder
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

#ifndef __GST_TTA_DEC_H__
#define __GST_TTA_DEC_H__

#include <gst/gst.h>

#include "ttadec.h"

G_BEGIN_DECLS

/* #define's don't like whitespacey bits */
#define GST_TYPE_TTA_DEC \
  (gst_tta_dec_get_type())
#define GST_TTA_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TTA_DEC,GstTtaDec))
#define GST_TTA_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_TTA_DEC,GstTtaDecClass))
#define GST_IS_TTA_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TTA_DEC))
#define GST_IS_TTA_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_TTA_DEC))

typedef struct _GstTtaDec      GstTtaDec;
typedef struct _GstTtaDecClass GstTtaDecClass;

typedef struct _tta_buffer
{
  guchar *buffer;
  guchar *buffer_end;
  gulong bit_count;
  gulong bit_cache;
  guchar *bitpos;
  gulong offset;
} tta_buffer;

struct _GstTtaDec
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  guint32 samplerate;
  guint channels;
  guint bytes;
  long frame_length;

  decoder *tta;
  guchar *decdata;
  tta_buffer tta_buf;
  long *cache;
};

struct _GstTtaDecClass 
{
  GstElementClass parent;
};

GType gst_tta_dec_get_type (void);

gboolean gst_tta_dec_plugin_init (GstPlugin *plugin);

G_END_DECLS

#endif /* __GST_TTA_DEC_H__ */
