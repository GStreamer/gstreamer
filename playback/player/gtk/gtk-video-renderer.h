/* GStreamer
 *
 * Copyright (C) 2015 Sebastian Dröge <sebastian@centricular.com>
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

#ifndef __GTK_VIDEO_RENDERER_H__
#define __GTK_VIDEO_RENDERER_H__

#include <gst/play/play.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef struct _GstPlayGtkVideoRenderer
    GstPlayGtkVideoRenderer;
typedef struct _GstPlayGtkVideoRendererClass
    GstPlayGtkVideoRendererClass;

#define GST_TYPE_PLAY_GTK_VIDEO_RENDERER             (gst_player_gtk_video_renderer_get_type ())
#define GST_IS_PLAY_GTK_VIDEO_RENDERER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_PLAY_GTK_VIDEO_RENDERER))
#define GST_IS_PLAY_GTK_VIDEO_RENDERER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_PLAY_GTK_VIDEO_RENDERER))
#define GST_PLAY_GTK_VIDEO_RENDERER_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_PLAY_GTK_VIDEO_RENDERER, GstPlayGtkVideoRendererClass))
#define GST_PLAY_GTK_VIDEO_RENDERER(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_PLAY_GTK_VIDEO_RENDERER, GstPlayGtkVideoRenderer))
#define GST_PLAY_GTK_VIDEO_RENDERER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_PLAY_GTK_VIDEO_RENDERER, GstPlayGtkVideoRendererClass))
#define GST_PLAY_GTK_VIDEO_RENDERER_CAST(obj)        ((GstPlayGtkVideoRenderer*)(obj))

GType gst_player_gtk_video_renderer_get_type (void);

GstPlayVideoRenderer * gst_play_gtk_video_renderer_new (void);
GtkWidget * gst_play_gtk_video_renderer_get_widget (GstPlayGtkVideoRenderer * self);

G_END_DECLS

#endif /* __GTK_VIDEO_RENDERER_H__ */
