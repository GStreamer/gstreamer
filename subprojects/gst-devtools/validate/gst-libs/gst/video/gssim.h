/* GStreamer
 *
 * Copyright (C) 2014 Mathieu Duponchelle <mathieu.duponchelle@opencreed.com>
 * Copyright (C) 2015 Raspberry Pi Foundation
 *  Author: Thibault Saunier <thibault.saunier@collabora.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef _GSSIM_H
#define _GSSIM_H

#include <glib.h>
#include <gst/gst.h>
#include <glib-object.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

typedef struct _GssimPrivate GssimPrivate;

typedef struct {
  GstObject parent;

  GssimPrivate *priv;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
} Gssim;

typedef struct {
  GstObjectClass parent;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
} GssimClass;

#define GSSIM_TYPE (gssim_get_type ())
#define GSSIM(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSSIM_TYPE, Gssim))
#define GSSIM_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), GSSIM_TYPE, GssimClass))
#define IS_GSSIM(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSSIM_TYPE))
#define IS_GSSIM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GSSIM_TYPE))
#define GSSIM_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GSSIM_TYPE, GssimClass))

GType gssim_get_type (void);
Gssim * gssim_new    (void);

void gssim_compare       (Gssim * self, guint8 * org, guint8 * mod,
                          guint8 * out, gfloat * mean, gfloat * lowest,
                          gfloat * highest);
gboolean gssim_configure (Gssim * self, gint width, gint height);

G_END_DECLS

#endif
