/* GStreamer
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_GSETTINGS_VIDEO_SRC_H__
#define __GST_GSETTINGS_VIDEO_SRC_H__

#include <gst/gst.h>
#include <gio/gio.h>
#include "gstswitchsrc.h"

G_BEGIN_DECLS

#define GST_TYPE_GSETTINGS_VIDEO_SRC \
  (gst_gsettings_video_src_get_type ())
#define GST_GSETTINGS_VIDEO_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_GSETTINGS_VIDEO_SRC, \
                               GstGSettingsVideoSrc))
#define GST_GSETTINGS_VIDEO_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_GSETTINGS_VIDEO_SRC, \
                            GstGSettingsVideoSrcClass))
#define GST_IS_GSETTINGS_VIDEO_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_GSETTINGS_VIDEO_SRC))
#define GST_IS_GSETTINGS_VIDEO_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_GSETTINGS_VIDEO_SRC))

typedef struct _GstGSettingsVideoSrc {
  GstSwitchSrc parent;

  GSettings *settings;

  GMainContext *context;
  GMainLoop *loop;
  gulong changed_id;

  gchar *gsettings_str;
} GstGSettingsVideoSrc;

typedef struct _GstGSettingsVideoSrcClass {
  GstSwitchSrcClass parent_class;
} GstGSettingsVideoSrcClass;

GType   gst_gsettings_video_src_get_type   (void);

G_END_DECLS

#endif /* __GST_GSETTINGS_VIDEO_SRC_H__ */
