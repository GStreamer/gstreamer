/*
 * Copyright (c) 2008 Benjamin Schmitz <vortex@wolpzone.de>
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

#ifndef __GST_ASSRENDER_H__
#define __GST_ASSRENDER_H__

#include <gst/gst.h>

#include <ass/ass.h>
#include <ass/ass_types.h>

G_BEGIN_DECLS

#define GST_TYPE_ASSRENDER (gst_assrender_get_type())
#define GST_ASSRENDER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ASSRENDER,Gstassrender))
#define GST_ASSRENDER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ASSRENDER,GstassrenderClass))
#define GST_IS_ASSRENDER(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ASSRENDER))
#define GST_IS_ASSRENDER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ASSRENDER))
typedef struct _Gstassrender Gstassrender;
typedef struct _GstassrenderClass GstassrenderClass;

struct _Gstassrender
{
  GstElement element;

  GstPad *video_sinkpad, *text_sinkpad, *srcpad;

  GstSegment video_segment;

  gint width, height;

  ass_library_t *ass_library;
  ass_renderer_t *ass_renderer;
  ass_track_t *ass_track;

  gboolean renderer_init_ok, track_init_ok, enable, embeddedfonts;
};

struct _GstassrenderClass
{
  GstElementClass parent_class;
};

GType gst_assrender_get_type (void);

G_END_DECLS

#endif
