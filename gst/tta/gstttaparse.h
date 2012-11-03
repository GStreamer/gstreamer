/* GStreamer TTA plugin
 * (c) 2004 Arwed v. Merkatz <v.merkatz@gmx.net>
 *
 * gstttaparse.h: TTA file parser
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

#ifndef __GST_TTA_PARSE_H__
#define __GST_TTA_PARSE_H__

#include <gst/gst.h>

G_BEGIN_DECLS

/* #define's don't like whitespacey bits */
#define GST_TYPE_TTA_PARSE \
  (gst_tta_parse_get_type())
#define GST_TTA_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TTA_PARSE,GstTtaParse))
#define GST_TTA_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_TTA_PARSE,GstTtaParseClass))
#define GST_IS_TTA_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TTA_PARSE))
#define GST_IS_TTA_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_TTA_PARSE))

typedef struct _GstTtaParse      GstTtaParse;
typedef struct _GstTtaParseClass GstTtaParseClass;

typedef struct _GstTtaIndex {
  guint32   size;     /* size of frame frameno */
  guint64   pos;      /* start of the frame */
  guint64   time;     /* in nanoseconds */
} GstTtaIndex;

struct _GstTtaParse
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  gboolean header_parsed;
  guint32 samplerate;
  guint16 channels;
  guint16 bits;
  guint32 data_length;
  guint num_frames;

  GstTtaIndex *index;

  guint32 current_frame;
};

struct _GstTtaParseClass 
{
  GstElementClass parent;
};

GType gst_tta_parse_get_type (void);

gboolean gst_tta_parse_plugin_init (GstPlugin *plugin);

G_END_DECLS

#endif /* __GST_TTA_PARSE_H__ */
