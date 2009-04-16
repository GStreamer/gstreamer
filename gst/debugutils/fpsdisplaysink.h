/*
 *  Copyright 2009 Nokia Corporation <multimedia@maemo.org>
 *            2006 Zeeshan Ali <zeeshan.ali@nokia.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __FPS_DISPLAY_SINK_H__
#define __FPS_DISPLAY_SINK_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define FPS_TYPE_DISPLAY_SINK \
  (fps_display_sink_get_type())
#define FPS_DISPLAY_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),FPS_TYPE_DISPLAY_SINK,FPSDisplaySink))
#define FPS_DISPLAY_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),FPS_TYPE_DISPLAY_SINK,FPSDisplaySinkClass))
#define FPS_IS_DISPLAY_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),FPS_TYPE_DISPLAY_SINK))
#define FPS_IS_DISPLAY_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),FPS_TYPE_DISPLAY_SINK))

GType fps_display_sink_get_type (void);

typedef struct _FPSDisplaySink FPSDisplaySink;
typedef struct _FPSDisplaySinkClass FPSDisplaySinkClass;

typedef struct _FPSDisplaySinkPrivate FPSDisplaySinkPrivate;

struct _FPSDisplaySink
{
  GstBin bin;                   /* we extend GstBin */
  FPSDisplaySinkPrivate *priv;
};

struct _FPSDisplaySinkClass
{
  GstBinClass parent_class;
};

G_END_DECLS

#endif /* __FPS_DISPLAY_SINK_H__ */
