/*
 *  output.h - Video output helpers
 *
 *  Copyright (C) 2012-2013 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#ifndef OUTPUT_H
#define OUTPUT_H

#include <glib.h>
#include <gst/vaapi/gstvaapidisplay.h>
#include <gst/vaapi/gstvaapiwindow.h>

typedef GstVaapiDisplay *(*CreateDisplayFunc)(const gchar *display_name);
typedef GstVaapiWindow *(*CreateWindowFunc)(GstVaapiDisplay *display,
                                            guint width, guint height);

typedef struct _VideoOutputInfo VideoOutputInfo;
struct _VideoOutputInfo {
    const gchar        *name;
    CreateDisplayFunc   create_display;
    CreateWindowFunc    create_window;
};

gboolean
video_output_init(int *argc, char *argv[], GOptionEntry *options);

void
video_output_exit(void);

const VideoOutputInfo *
video_output_lookup(const gchar *output_name);

GstVaapiDisplay *
video_output_create_display(const gchar *display_name);

GstVaapiWindow *
video_output_create_window(GstVaapiDisplay *display, guint width, guint height);

#endif /* OUTPUT_H */
