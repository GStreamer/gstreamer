/* GStreamer Base Class for Tag Demuxing
 * Copyright (C) 2005  Jan Schmidt <thaytan@mad.scientist.com>
 * Copyright (C) 2006  Tim-Philipp Müller <tim centricular net>
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

#ifndef __GST_TAG_DEMUX_H__
#define __GST_TAG_DEMUX_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_TAG_DEMUX            (gst_tag_demux_get_type())
#define GST_TAG_DEMUX(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TAG_DEMUX,GstTagDemux))
#define GST_TAG_DEMUX_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_TAG_DEMUX,GstTagDemuxClass))
#define GST_IS_TAG_DEMUX(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TAG_DEMUX))
#define GST_IS_TAG_DEMUX_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_TAG_DEMUX))

typedef struct _GstTagDemux        GstTagDemux;
typedef struct _GstTagDemuxClass   GstTagDemuxClass;
typedef struct _GstTagDemuxPrivate GstTagDemuxPrivate;

/**
 * GstTagDemuxResult:
 * @GST_TAG_DEMUX_RESULT_BROKEN_TAG: cannot parse tag, just skip it
 * @GST_TAG_DEMUX_RESULT_AGAIN     : call again with less or more data
 * @GST_TAG_DEMUX_RESULT_OK	   : parsed tag successfully
 *
 * Result values from the parse_tag virtual function.
 */
typedef enum {
  GST_TAG_DEMUX_RESULT_BROKEN_TAG,
  GST_TAG_DEMUX_RESULT_AGAIN,
  GST_TAG_DEMUX_RESULT_OK
} GstTagDemuxResult;

struct _GstTagDemux
{
  GstElement element;

  /* Minimum size required to identify a tag at the start and
   * determine its total size (0 = not interested in start) */
  guint         min_start_size;

  /* Minimum size required to identify a tag at the end and
   * determine its total size (0 = not interested in end) */
  guint         min_end_size;

  /* Prefer start tags over end tags (default: yes) */
  gboolean      prefer_start_tag;

  /*< private >*/
  gpointer      reserved[GST_PADDING];
  GstTagDemuxPrivate *priv;
};

/* Note: subclass must also add a sink pad template in its base_init */
struct _GstTagDemuxClass 
{
  GstElementClass parent_class;

  /* vtable */

  /* Identify tag and determine the size required to parse the tag. Buffer
   * may be larger than the specified minimum size. */
  gboolean               (*identify_tag)  (GstTagDemux * demux,
                                           GstBuffer   * buffer,
                                           gboolean      start_tag,
                                           guint       * tag_size);

  /* Parse the tag. Buffer should be exactly the size determined by
   * identify_tag() before. parse_tag() may change tag_size and return
   * GST_TAG_DEMUX_RESULT_AGAIN to request a larger or smaller buffer.
   * parse_tag() is also permitted to adjust tag_size to a smaller value
   * and return GST_TAG_DEMUX_RESULT_OK. */
  GstTagDemuxResult      (*parse_tag)     (GstTagDemux * demux,
                                           GstBuffer   * buffer,
                                           gboolean      start_tag,
                                           guint       * tag_size,
                                           GstTagList ** tags);

  /*< private >*/
  gpointer   reserved[GST_PADDING];
};

GType gst_tag_demux_get_type (void);

G_END_DECLS

#endif /* __GST_TAG_DEMUX_H__ */
