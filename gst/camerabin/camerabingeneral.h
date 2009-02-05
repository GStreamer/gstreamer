/*
 * GStreamer
 * Copyright (C) 2008 Nokia Corporation <multimedia@maemo.org>
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

#ifndef __CAMERABIN_GENERAL_H_
#define __CAMERABIN_GENERAL_H_

#include <sys/time.h>
#include <time.h>

#include <gst/gst.h>


typedef struct timeval TIME_TYPE;
#define GET_TIME(t) do { gettimeofday(&(t), NULL); } while(0)
#define DIFF_TIME(t2,t1,d) do { d = ((t2).tv_sec - (t1).tv_sec) * 1000000 + \
      (t2).tv_usec - (t1).tv_usec; } while(0)

#define _INIT_TIMER_BLOCK TIME_TYPE t1, t2; guint32 d; do {;}while (0)

#define _OPEN_TIMER_BLOCK { GET_TIME(t1); do {;}while (0)
#define _CLOSE_TIMER_BLOCK GET_TIME(t2); DIFF_TIME(t2,t1,d); \
  GST_DEBUG("elapsed time = %u\n", d); \
  } do {;}while (0)


extern void
camerabin_general_dbg_set_probe (GstElement * elem, gchar * pad_name,
    gboolean buf, gboolean evt);

gboolean gst_camerabin_try_add_element (GstBin * bin, GstElement * new_elem);

gboolean gst_camerabin_add_element (GstBin * bin, GstElement * new_elem);

GstElement *gst_camerabin_create_and_add_element (GstBin * bin,
    const gchar * elem_name);

void gst_camerabin_remove_elements_from_bin (GstBin * bin);

gboolean
gst_camerabin_drop_eos_probe (GstPad * pad, GstEvent * event, gpointer u_data);

GST_DEBUG_CATEGORY_EXTERN (gst_camerabin_debug);
#define GST_CAT_DEFAULT gst_camerabin_debug

#endif /* #ifndef __CAMERABIN_GENERAL_H_ */
