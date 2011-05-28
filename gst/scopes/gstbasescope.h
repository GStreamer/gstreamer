/* GStreamer
 * Copyright (C) <2011> Stefan Kost <ensonic@users.sf.net>
 *
 * gstbasescope.c: base class for audio visualisation elements
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __GST_BASE_SCOPE_H__
#define __GST_BASE_SCOPE_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

#include <gst/video/video.h>
#include <gst/audio/audio.h>
#include <gst/base/gstadapter.h>

G_BEGIN_DECLS
#define GST_TYPE_BASE_SCOPE            (gst_base_scope_get_type())
#define GST_BASE_SCOPE(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BASE_SCOPE,GstBaseScope))
#define GST_BASE_SCOPE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_BASE_SCOPE,GstBaseScopeClass))
#define GST_IS_SYNAESTHESIA(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BASE_SCOPE))
#define GST_IS_SYNAESTHESIA_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_BASE_SCOPE))
typedef struct _GstBaseScope GstBaseScope;
typedef struct _GstBaseScopeClass GstBaseScopeClass;

typedef void (*GstBaseScopeShaderFunc)(GstBaseScope *scope, const guint8 *s, guint8 *d);

/**
 * GstBaseScopeShader:
 * @GST_BASE_SCOPE_SHADER_NONE: no shading
 * @GST_BASE_SCOPE_SHADER_FADE: plain fading
 * @GST_BASE_SCOPE_SHADER_FADE_AND_MOVE_UP: fade and move up
 *
 * Different types of supported background shading functions.
 */
typedef enum {
  GST_BASE_SCOPE_SHADER_NONE,
  GST_BASE_SCOPE_SHADER_FADE,
  GST_BASE_SCOPE_SHADER_FADE_AND_MOVE_UP
} GstBaseScopeShader;

struct _GstBaseScope
{
  GstElement parent;

  /* pads */
  GstPad *srcpad, *sinkpad;

  GstAdapter *adapter;
  GstBuffer *inbuf;
  guint8 *pixelbuf;

  GstBaseScopeShader shader_type;
  GstBaseScopeShaderFunc shader;
  guint32 shade_amount;

  guint64 next_ts;              /* the timestamp of the next frame */
  guint64 frame_duration;
  guint bpf;                    /* bytes per frame */
  guint bps;                    /* bytes per sample */
  guint spf;                    /* samples per video frame */
  guint req_spf;                /* min samples per frame wanted by the subclass */

  /* video state */
  GstVideoFormat video_format;
  gint fps_n, fps_d;
  gint width;
  gint height;
  gint channels;

  /* audio state */
  gint sample_rate;
  gint rate;
};

struct _GstBaseScopeClass
{
  GstElementClass parent_class;

  /* virtual function, called whenever the format changes */
  gboolean (*setup) (GstBaseScope * scope);

  /* virtual function for rendering a frame */
  gboolean (*render) (GstBaseScope * scope, GstBuffer * audio, GstBuffer * video);
};

GType gst_base_scope_get_type (void);

G_END_DECLS
#endif /* __GST_BASE_SCOPE_H__ */
