/*
 * gstwildmidi - wildmidi plugin for gstreamer
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Wrapper element for libtimidity.  This element works in pull
 * based mode because that's essentially how libwildmidi works. 
 * We create a libwildmidi stream that operates on the srcpad.  
 * The sinkpad is in pull mode.
 */

#ifndef __GST_WILDMIDI_H__
#define __GST_WILDMIDI_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <wildmidi_lib.h>

G_BEGIN_DECLS
#define GST_TYPE_WILDMIDI \
  (gst_wildmidi_get_type())
#define GST_WILDMIDI(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_WILDMIDI,GstWildmidi))
#define GST_WILDMIDI_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_WILDMIDI,GstWildmidiClass))
#define GST_IS_WILDMIDI(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_WILDMIDI))
#define GST_IS_WILDMIDI_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_WILDMIDI))
typedef struct _GstWildmidi GstWildmidi;
typedef struct _GstWildmidiClass GstWildmidiClass;

struct _GstWildmidi
{
    GstElement element;

    GstPad *sinkpad, *srcpad;

    /* input stream properties */
    gint64 mididata_size, mididata_offset;
    gchar *mididata;
    gboolean mididata_filled;

    midi *song;

    /* output data */
    gboolean o_new_segment, o_segment_changed, o_seek;
    GstSegment o_segment[1];
    gint64 o_len;

    /* format of the stream */
    gint64 bytes_per_frame;
    GstClockTime time_per_frame;

    /* options */
    gboolean accurate_seek;

    /* wildmidi settings */
    gboolean high_quality;
    gboolean linear_volume;

    GstCaps *out_caps;
};

struct _GstWildmidiClass
{
    GstElementClass parent_class;
};

GType gst_wildmidi_get_type (void);

G_END_DECLS
#endif /* __GST_WILDMIDI_H__ */
