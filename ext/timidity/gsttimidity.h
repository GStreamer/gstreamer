/*
 * gsttimdity - timidity plugin for gstreamer
 * 
 * Copyright 2007 Wouter Paesen <wouter@blue-gate.be>
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
 *
 * Wrapper element for libtimidity.  This element works in pull
 * based mode because that's essentially how libtimidity works. 
 * We create a libtimidity stream that operates on the srcpad.  
 * The sinkpad is in pull mode.
 */

#ifndef __GST_TIMIDITY_H__
#define __GST_TIMIDITY_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <timidity.h>

G_BEGIN_DECLS
#define GST_TYPE_TIMIDITY \
  (gst_timidity_get_type())
#define GST_TIMIDITY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TIMIDITY,GstTimidity))
#define GST_TIMIDITY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_TIMIDITY,GstTimidityClass))
#define GST_IS_TIMIDITY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TIMIDITY))
#define GST_IS_TIMIDITY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_TIMIDITY))
typedef struct _GstTimidity GstTimidity;
typedef struct _GstTimidityClass GstTimidityClass;

struct _GstTimidity
{
    GstElement element;

    GstPad *sinkpad, *srcpad;

    gboolean initialized;

    /* input stream properties */
    gint64 mididata_size, mididata_offset;
    gchar *mididata;
    gboolean mididata_filled;

    MidSong *song;

    /* output data */
    gboolean o_new_segment, o_segment_changed, o_seek;
    GstSegment o_segment[1];
    gint64 o_len;

    /* format of the stream */
    MidSongOptions song_options[1];
    gint64 bytes_per_frame;
    GstClockTime time_per_frame;

    GstCaps *out_caps;
};

struct _GstTimidityClass
{
    GstElementClass parent_class;
};

GType gst_timidity_get_type (void);

G_END_DECLS
#endif /* __GST_TIMIDITY_H__ */
