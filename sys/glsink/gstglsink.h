/* GStreamer
 * Copyright (C) 2002 The GStreamer Team 
 *                           <gstreamer-devel@lists.sourceforge.net>
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


#ifndef __GST_VIDEOSINK_H__
#define __GST_VIDEOSINK_H__

#include <gst/gst.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define MAX_FLIP_BUFFERS 1

typedef struct _GstImageInfo GstImageInfo;
struct _GstImageInfo {
  gulong id;
  int demo;
  int dumpvideo;
  void (*free_info) (GstImageInfo *info);
};


typedef struct _GstImageData GstImageData;
struct _GstImageData {
  gint size;
  gchar *data;
};

typedef struct _GstImageConnection GstImageConnection;
struct _GstImageConnection {
  void (*open_conn) (GstImageConnection *conn, GstImageInfo *info);
  void (*close_conn) (GstImageConnection *conn, GstImageInfo *info);  
  void (*free_conn) (GstImageConnection *conn);
};

typedef GstCaps *		(*GstImagePluginGetCapsFunc)	(GstImageInfo *info); 
typedef GstImageConnection *	(*GstImagePluginSetCapsFunc) 	(GstImageInfo *info, GstCaps *caps);
typedef GstImageData*		(*GstImagePluginGetImageFunc)	(GstImageInfo *info, GstImageConnection *conn);
typedef void 			(*GstImagePluginPutImageFunc)	(GstImageInfo *info, GstImageData *image);
typedef void 			(*GstImagePluginFreeImageFunc)	(GstImageData *image);

typedef struct _GstImagePlugin GstImagePlugin;
struct _GstImagePlugin {
  GstImagePluginGetCapsFunc	get_caps;
  GstImagePluginSetCapsFunc	set_caps;
  GstImagePluginGetImageFunc	get_image;
  GstImagePluginPutImageFunc	put_image;
  GstImagePluginFreeImageFunc	free_image;
};

typedef struct _GstGLImageInfo GstGLImageInfo;
/* stuff about our window grouped together */
struct _GstGLImageInfo {
  GstImageInfo info;
  Display *dpy;
  int screen;
  Window win;
  GLXContext ctx;
  XSetWindowAttributes attr;
  Bool fs;
  //XF86VidModeModeInfo deskMode;
  int x, y;
  unsigned int width, height;
  unsigned int depth;    
  /* window specific from here */
  GstElement *sink;
  gulong handler_id;
  float rotX,rotY,zoom, zoomdir;
};


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_VIDEOSINK_H__ */
