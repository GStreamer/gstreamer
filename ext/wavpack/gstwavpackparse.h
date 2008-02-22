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
#include <gst/base/gstadapter.h>

G_BEGIN_DECLS
#define GST_TYPE_WAVPACK_PARSE \
  (gst_wavpack_parse_get_type())
#define GST_WAVPACK_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_WAVPACK_PARSE,GstWavpackParse))
#define GST_WAVPACK_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_WAVPACK_PARSE,GstWavpackParseClass))
#define GST_IS_WAVPACK_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_WAVPACK_PARSE))
#define GST_IS_WAVPACK_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_WAVPACK_PARSE))
typedef struct _GstWavpackParse GstWavpackParse;
typedef struct _GstWavpackParseClass GstWavpackParseClass;
typedef struct _GstWavpackParseIndexEntry GstWavpackParseIndexEntry;

struct _GstWavpackParseIndexEntry
{
  gint64 byte_offset;           /* byte offset of this chunk  */
  gint64 sample_offset;         /* first sample in this chunk */
  gint64 sample_offset_end;     /* first sample in next chunk  */
};

struct _GstWavpackParse
{
  GstElement element;

  /*< private > */
  GstPad *sinkpad;
  GstPad *srcpad;

  guint samplerate;
  guint channels;
  gint64 total_samples;

  gboolean need_newsegment;
  gboolean discont;

  gint64 current_offset;        /* byte offset on sink pad */
  gint64 upstream_length;       /* length of file in bytes */

  GstSegment segment;           /* the currently configured segment, in
                                 * samples/audio frames (DEFAULT format) */

  GstBuffer *pending_buffer;
  gint32 pending_offset;
  guint32 next_block_index;

  GstAdapter *adapter;          /* when operating chain-based, otherwise NULL */

  /* List of GstWavpackParseIndexEntry structs, mapping known
   * sample offsets to byte offsets. Is kept increasing without
   * gaps (ie. append only and consecutive entries must always
   * map to consecutive chunks in the file). */
  GSList *entries;

  /* Queued events (e.g. tag events we receive before we create the src pad) */
  GList *queued_events;         /* STREAM_LOCK */
};

struct _GstWavpackParseClass
{
  GstElementClass parent;
};

GType gst_wavpack_parse_get_type (void);

gboolean gst_wavpack_parse_plugin_init (GstPlugin * plugin);

G_END_DECLS
#endif /* __GST_WAVPACK_PARSE_H__ */
