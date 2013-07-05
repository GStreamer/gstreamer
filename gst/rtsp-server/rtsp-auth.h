/* GStreamer
 * Copyright (C) 2010 Wim Taymans <wim.taymans at gmail.com>
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

#include <gst/gst.h>

#ifndef __GST_RTSP_AUTH_H__
#define __GST_RTSP_AUTH_H__

typedef struct _GstRTSPAuth GstRTSPAuth;
typedef struct _GstRTSPAuthClass GstRTSPAuthClass;
typedef struct _GstRTSPAuthPrivate GstRTSPAuthPrivate;

#include "rtsp-client.h"

G_BEGIN_DECLS

#define GST_TYPE_RTSP_AUTH              (gst_rtsp_auth_get_type ())
#define GST_IS_RTSP_AUTH(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_RTSP_AUTH))
#define GST_IS_RTSP_AUTH_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_RTSP_AUTH))
#define GST_RTSP_AUTH_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_RTSP_AUTH, GstRTSPAuthClass))
#define GST_RTSP_AUTH(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_RTSP_AUTH, GstRTSPAuth))
#define GST_RTSP_AUTH_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_RTSP_AUTH, GstRTSPAuthClass))
#define GST_RTSP_AUTH_CAST(obj)         ((GstRTSPAuth*)(obj))
#define GST_RTSP_AUTH_CLASS_CAST(klass) ((GstRTSPAuthClass*)(klass))

/**
 * GstRTSPAuth:
 *
 * The authentication structure.
 */
struct _GstRTSPAuth {
  GObject       parent;

  GstRTSPAuthPrivate *priv;
};

struct _GstRTSPAuthClass {
  GObjectClass  parent_class;

  gboolean (*setup)         (GstRTSPAuth *auth, GstRTSPClient * client,
                             GstRTSPClientState *state);
  gboolean (*validate)      (GstRTSPAuth *auth, GstRTSPClient * client,
                             GstRTSPClientState *state);
  gboolean (*check)         (GstRTSPAuth *auth, GstRTSPClient * client,
                             GQuark hint, GstRTSPClientState *state);
};

GType               gst_rtsp_auth_get_type          (void);

GstRTSPAuth *       gst_rtsp_auth_new               (void);

void                gst_rtsp_auth_add_basic         (GstRTSPAuth *auth, const gchar * basic,
                                                     const gchar *authgroup);
void                gst_rtsp_auth_remove_basic      (GstRTSPAuth *auth, const gchar * basic);

gboolean            gst_rtsp_auth_setup             (GstRTSPAuth *auth, GstRTSPClient * client,
                                                     GstRTSPClientState *state);

gboolean            gst_rtsp_auth_check             (GstRTSPAuth *auth, GstRTSPClient * client,
                                                     GQuark hint, GstRTSPClientState *state);

/* helpers */
gchar *             gst_rtsp_auth_make_basic        (const gchar * user, const gchar * pass);

G_END_DECLS

#endif /* __GST_RTSP_AUTH_H__ */
