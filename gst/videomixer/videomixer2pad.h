/* Generic video mixer plugin
 * Copyright (C) 2008 Wim Taymans <wim@fluendo.com>
 * Copyright (C) 2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
 
#ifndef __GST_VIDEO_MIXER2_PAD_H__
#define __GST_VIDEO_MIXER2_PAD_H__

#include <gst/gst.h>
#include <gst/video/video.h>

#include <gst/base/gstcollectpads2.h>

G_BEGIN_DECLS

#define GST_TYPE_VIDEO_MIXER2_PAD (gst_videomixer2_pad_get_type())
#define GST_VIDEO_MIXER2_PAD(obj) \
        (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEO_MIXER2_PAD, GstVideoMixer2Pad))
#define GST_VIDEO_MIXER2_PAD_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEO_MIXER_PAD, GstVideoMixer2PadClass))
#define GST_IS_VIDEO_MIXER2_PAD(obj) \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEO_MIXER2_PAD))
#define GST_IS_VIDEO_MIXER2_PAD_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEO_MIXER2_PAD))

typedef struct _GstVideoMixer2Pad GstVideoMixer2Pad;
typedef struct _GstVideoMixer2PadClass GstVideoMixer2PadClass;
typedef struct _GstVideoMixer2Collect GstVideoMixer2Collect;

/**
 * GstVideoMixer2Pad:
 *
 * The opaque #GstVideoMixer2Pad structure.
 */
struct _GstVideoMixer2Pad
{
  GstPad parent;

  /* < private > */

  /* caps */
  gint width, height;
  gint fps_n;
  gint fps_d;

  /* properties */
  gint xpos, ypos;
  guint zorder;
  gdouble alpha;

  GstVideoMixer2Collect *mixcol;
};

struct _GstVideoMixer2PadClass
{
  GstPadClass parent_class;
};

GType gst_videomixer2_pad_get_type (void);

G_END_DECLS
#endif /* __GST_VIDEO_MIXER2_PAD_H__ */
