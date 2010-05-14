/* Video mixer pad
 * Copyright (C) 2008 Wim Taymans <wim@fluendo.com>
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

#ifndef __GST_VIDEO_MIXER_PAD_H__
#define __GST_VIDEO_MIXER_PAD_H__

#include <gst/gst.h>
#include <gst/base/gstcollectpads.h>

G_BEGIN_DECLS

#define GST_TYPE_VIDEO_MIXER_PAD (gst_videomixer_pad_get_type())
#define GST_VIDEO_MIXER_PAD(obj) \
        (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEO_MIXER_PAD, GstVideoMixerPad))
#define GST_VIDEO_MIXER_PAD_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEO_MIXER_PAD, GstVideoMixerPadiClass))
#define GST_IS_VIDEO_MIXER_PAD(obj) \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEO_MIXER_PAD))
#define GST_IS_VIDEO_MIXER_PAD_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEO_MIXER_PAD))

typedef struct _GstVideoMixerPad GstVideoMixerPad;
typedef struct _GstVideoMixerPadClass GstVideoMixerPadClass;
typedef struct _GstVideoMixerCollect GstVideoMixerCollect;

struct _GstVideoMixerCollect
{
  GstCollectData collect;       /* we extend the CollectData */

  GstBuffer *buffer;            /* the queued buffer for this pad */

  GstVideoMixerPad *mixpad;
};

/* all information needed for one video stream */
struct _GstVideoMixerPad
{
  GstPad parent;                /* subclass the pad */

  gint64 queued;

  guint in_width, in_height;
  gint fps_n;
  gint fps_d;
  gint par_n;
  gint par_d;

  gint xpos, ypos;
  guint zorder;
  gint blend_mode;
  gdouble alpha;

  GstVideoMixerCollect *mixcol;
};

struct _GstVideoMixerPadClass
{
  GstPadClass parent_class;
};

G_END_DECLS
#endif /* __GST_VIDEO_MIXER_PAD_H__ */
