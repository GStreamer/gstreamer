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


#ifndef __GST_VIDEOFILTER_H__
#define __GST_VIDEOFILTER_H__


#include <gst/gst.h>


G_BEGIN_DECLS

typedef struct _GstVideofilter GstVideofilter;
typedef struct _GstVideofilterClass GstVideofilterClass;

typedef void (*GstVideofilterFilterFunc)(GstVideofilter *filter,
    void *out_data, void *in_data);

typedef void (*GstVideofilterSetupFunc)(GstVideofilter *filter);

typedef struct _GstVideofilterFormat GstVideofilterFormat;
struct _GstVideofilterFormat {
  char *fourcc;
  int depth;
  GstVideofilterFilterFunc filter_func;
  int bpp;
  unsigned int endianness;
  unsigned int red_mask;
  unsigned int green_mask;
  unsigned int blue_mask;
};

#define GST_TYPE_VIDEOFILTER \
  (gst_videofilter_get_type())
#define GST_VIDEOFILTER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEOFILTER,GstVideofilter))
#define GST_VIDEOFILTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEOFILTER,GstVideofilterClass))
#define GST_IS_VIDEOFILTER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEOFILTER))
#define GST_IS_VIDEOFILTER_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEOFILTER))

struct _GstVideofilter {
  GstElement element;

  GstPad *sinkpad,*srcpad;

  /* video state */
  gboolean inited;
  GstVideofilterFormat *format;
  gint to_width;
  gint to_height;
  gint from_width;
  gint from_height;
  gboolean passthru;

  /* private */
  gint from_buf_size;
  gint to_buf_size;
  gdouble framerate;

  GstBuffer *in_buf;
  GstBuffer *out_buf;
};

struct _GstVideofilterClass {
  GstElementClass parent_class;

  GPtrArray *formats;
  GstVideofilterSetupFunc setup;
};

GType gst_videofilter_get_type(void);

int gst_videofilter_get_input_width(GstVideofilter *videofilter);
int gst_videofilter_get_input_height(GstVideofilter *videofilter);
void gst_videofilter_set_output_size(GstVideofilter *videofilter,
    int width, int height);
GstVideofilterFormat *gst_videofilter_find_format_by_caps(GstVideofilter *filter,
    const GstCaps *caps);
GstCaps *gst_videofilter_class_get_capslist(GstVideofilterClass *videofilterclass);

void gst_videofilter_class_add_format(GstVideofilterClass *videofilterclass,
    GstVideofilterFormat *format);
void gst_videofilter_class_add_pad_templates (GstVideofilterClass *videofilterclass);

G_END_DECLS

#endif /* __GST_VIDEOFILTER_H__ */

