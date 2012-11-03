/* GStreamer
 *
 * Copyright (C) 2006 Lutz Mueller <lutz@topfrose.de>
 * Copyright (C) 2006 Edward Hervey <bilboed@bilbod.com>
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

#ifndef __GST_REAL_VIDEO_DEC_H__
#define __GST_REAL_VIDEO_DEC_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>

G_BEGIN_DECLS

#define GST_TYPE_REAL_VIDEO_DEC (gst_real_video_dec_get_type())
#define GST_REAL_VIDEO_DEC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_REAL_VIDEO_DEC,GstRealVideoDec))
#define GST_REAL_VIDEO_DEC_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_REAL_VIDEO_DEC,GstRealVideoDecClass))

typedef struct _GstRealVideoDec GstRealVideoDec;
typedef struct _GstRealVideoDecClass GstRealVideoDecClass;
typedef enum _GstRealVideoDecVersion GstRealVideoDecVersion;

enum _GstRealVideoDecVersion
{
  GST_REAL_VIDEO_DEC_VERSION_2 = 2,
  GST_REAL_VIDEO_DEC_VERSION_3 = 3,
  GST_REAL_VIDEO_DEC_VERSION_4 = 4
};

typedef struct {
  GModule *module;

  gpointer context;

  guint32 (*Init) (gpointer, gpointer);
  guint32 (*Free) (gpointer);
  guint32 (*Transform) (gchar *, gchar *, gpointer, gpointer, gpointer);
  guint32 (*Message) (gpointer, gpointer);

  /*
  GstRealVideoDecMessageFunc custom_message;
  GstRealVideoDecFreeFunc free;
  GstRealVideoDecInitFunc init;
  GstRealVideoDecTransformFunc transform;
  */

} GstRVDecLibrary;

struct _GstRealVideoDec
{
  GstElement parent;

  GstPad *src, *snk;

  /* Caps */
  GstRealVideoDecVersion version;
  guint width, height;
  gint format, subformat;
  gint framerate_num, framerate_denom;

  gint error_count;

  /* Library functions */
  GstRVDecLibrary lib;

  /* Properties */
  gchar *real_codecs_path;
  gboolean checked_modules;
  gchar *rv20_names;
  gboolean valid_rv20;
  gchar *rv30_names;
  gboolean valid_rv30;
  gchar *rv40_names;
  gboolean valid_rv40;
  gint max_errors;
};

struct _GstRealVideoDecClass
{
  GstElementClass parent_class;
};

GType gst_real_video_dec_get_type (void);

G_END_DECLS

#endif /* __GST_REAL_VIDEO_DEC_H__ */


