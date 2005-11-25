/* GStreamer wavpack plugin
 * (c) 2005 Arwed v. Merkatz <v.merkatz@gmx.net>
 *
 * gstwavpackparse.h: wavpack file parser
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

#ifndef __GST_WAVPACK_PARSE_H__
#define __GST_WAVPACK_PARSE_H__

#include <gst/gst.h>
#include <gst/bytestream/bytestream.h>

G_BEGIN_DECLS

/* #define's don't like whitespacey bits */
#define GST_TYPE_WAVPACK_PARSE \
  (gst_wavpack_parse_get_type())
#define GST_WAVPACK_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_WAVPACK_PARSE,GstWavpackParse))
#define GST_WAVPACK_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_WAVPACK_PARSE,GstWavpackParse))
#define GST_IS_WAVPACK_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_WAVPACK_PARSE))
#define GST_IS_WAVPACK_PARSE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_WAVPACK_PARSE))

typedef struct _GstWavpackParse      GstWavpackParse;
typedef struct _GstWavpackParseClass GstWavpackParseClass;

struct _GstWavpackParse
{
  GstElement element;

  GstPad *sinkpad, *srcpad;
  
  GstByteStream* bs;

  guint32 samplerate;
  guint32 channels;
  guint32 total_samples;
  guint64 timestamp;

  guint64 seek_offset;
  gboolean seek_pending;
  gboolean need_discont;
  gboolean need_flush;
};

struct _GstWavpackParseClass 
{
  GstElementClass parent;
};

GType gst_wavpack_parse_get_type (void);

gboolean gst_wavpack_parse_plugin_init (GstPlugin *plugin);

G_END_DECLS

#endif /* __GST_WAVPACK_PARSE_H__ */
