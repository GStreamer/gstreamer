/*
 * gstcmmlutils.h - GStreamer CMML utility functions
 * Copyright (C) 2005 Alessandro Decina
 * 
 * Authors:
 *   Alessandro Decina <alessandro@nnva.org>
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

#ifndef __GST_CMML_CLOCK_TIME_H__
#define __GST_CMML_CLOCK_TIME_H__

#include <gst/gst.h>
#include "gstcmmltag.h"

/* time utils */
GstClockTime gst_cmml_clock_time_from_npt (const gchar * time);
GstClockTime gst_cmml_clock_time_from_smpte (const gchar * time);
gchar * gst_cmml_clock_time_to_npt (const GstClockTime time);
gint64 gst_cmml_clock_time_to_granule (GstClockTime prev_time,
    GstClockTime current_time, gint64 granulerate_n, gint64 granulerate_d,
    guint8 granuleshift);

/* tracklist */
GHashTable * gst_cmml_track_list_new (void);
void gst_cmml_track_list_destroy (GHashTable * tracks);
void gst_cmml_track_list_add_clip (GHashTable * tracks, GstCmmlTagClip * clip);
gboolean gst_cmml_track_list_del_clip (GHashTable * tracks,
    GstCmmlTagClip * clip);
gboolean gst_cmml_track_list_has_clip (GHashTable * tracks,
    GstCmmlTagClip * clip);
GstCmmlTagClip * gst_cmml_track_list_get_track_last_clip (GHashTable * tracks,
    const gchar * track_name);
GList * gst_cmml_track_list_get_track_clips (GHashTable * tracks,
    const gchar * track_name);
GList * gst_cmml_track_list_get_clips (GHashTable * tracks);
void gst_cmml_track_list_set_track_data (GHashTable * tracks, gpointer data);
gpointer gst_cmml_track_list_get_track_data (GHashTable * tracks);
#endif /* __GST_CMML_CLOCK_TIME_H__ */
