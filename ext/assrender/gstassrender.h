/*
 * Copyright (c) 2008 Benjamin Schmitz <vortex@wolpzone.de>
 * Copyright (c) 2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifndef __GST_ASS_RENDER_H__
#define __GST_ASS_RENDER_H__

#include <gst/gst.h>
#include <gst/video/video.h>

#include <ass/ass.h>
#include <ass/ass_types.h>

G_BEGIN_DECLS

#if !defined(LIBASS_VERSION) || LIBASS_VERSION < 0x00907010
#define ASS_Library ass_library_t
#define ASS_Renderer ass_renderer_t
#define ASS_Track ass_track_t
#define ASS_Image ass_image_t
#endif

#define GST_TYPE_ASS_RENDER (gst_ass_render_get_type())
#define GST_ASS_RENDER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ASS_RENDER,GstAssRender))
#define GST_ASS_RENDER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ASS_RENDER,GstAssRenderClass))
#define GST_IS_ASS_RENDER(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ASS_RENDER))
#define GST_IS_ASS_RENDER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ASS_RENDER))

typedef struct _GstAssRender GstAssRender;
typedef struct _GstAssRenderClass GstAssRenderClass;
typedef void (*GstAssRenderBlitFunction) (GstAssRender *render, ASS_Image *ass_image, GstBuffer *buffer);

struct _GstAssRender
{
  GstElement element;

  GstPad *video_sinkpad, *text_sinkpad, *srcpad;

  /* properties */
  gboolean enable, embeddedfonts;

  /* <private> */
  GstSegment video_segment;

  GstVideoFormat format;
  gint width, height;
  gint fps_n, fps_d;
  GstAssRenderBlitFunction blit;

  GMutex *subtitle_mutex;
  GCond *subtitle_cond;
  GstBuffer *subtitle_pending;
  gboolean subtitle_flushing;
  GstSegment subtitle_segment;

  GMutex *ass_mutex;
  ASS_Library *ass_library;
  ASS_Renderer *ass_renderer;
  ASS_Track *ass_track;

  gboolean renderer_init_ok, track_init_ok;
};

struct _GstAssRenderClass
{
  GstElementClass parent_class;
};

GType gst_ass_render_get_type (void);

G_END_DECLS

#endif
