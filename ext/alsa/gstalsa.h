/*
    Copyright (C) 2001 CodeFactory AB
    Copyright (C) 2001 Thomas Nyberg <thomas@codefactory.se>
    Copyright (C) 2001 Andy Wingo <apwingo@eos.ncsu.edu>
                            
    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free
    Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifndef __GST_ALSA_H__
#define __GST_ALSA_H__

#include <alsa/asoundlib.h>
#include <gst/gst.h>
#include <libs/bytestream/gstbytestream.h>
#include <glib.h>

#define GST_ALSA(obj) G_TYPE_CHECK_INSTANCE_CAST(obj, GST_TYPE_ALSA, GstAlsa)
#define GST_ALSA_CLASS(klass) G_TYPE_CHECK_CLASS_CAST(klass, GST_TYPE_ALSA, GstAlsaClass)
#define GST_IS_ALSA(obj) G_TYPE_CHECK_INSTANCE_TYPE(obj, GST_TYPE_ALSA)
#define GST_IS_ALSA_CLASS(klass) G_TYPE_CHECK_CLASS_TYPE(klass, GST_TYPE_ALSA)
#define GST_TYPE_ALSA gst_alsa_get_type()

#define GST_ALSA_SINK(obj) G_TYPE_CHECK_INSTANCE_CAST(obj, GST_TYPE_ALSA_SINK, GstAlsa)
#define GST_ALSA_SINK_CLASS(klass) G_TYPE_CHECK_CLASS_CAST(klass, GST_TYPE_ALSA_SINK, GstAlsaClass)
#define GST_IS_ALSA_SINK(obj) G_TYPE_CHECK_INSTANCE_TYPE(obj, GST_TYPE_ALSA_SINK)
#define GST_IS_ALSA_SINK_CLASS(klass) G_TYPE_CHECK_CLASS_TYPE(klass, GST_TYPE_ALSA_SINK)
#define GST_TYPE_ALSA_SINK gst_alsa_sink_get_type()

#define GST_ALSA_SRC(obj) G_TYPE_CHECK_INSTANCE_CAST(obj, GST_TYPE_ALSA_SRC, GstAlsa)
#define GST_ALSA_SRC_CLASS(klass) G_TYPE_CHECK_CLASS_CAST(klass, GST_TYPE_ALSA_SRC, GstAlsaClass)
#define GST_IS_ALSA_SRC(obj) G_TYPE_CHECK_INSTANCE_TYPE(obj, GST_TYPE_ALSA_SRC)
#define GST_IS_ALSA_SRC_CLASS(klass) G_TYPE_CHECK_CLASS_TYPE(klass, GST_TYPE_ALSA_SRC)
#define GST_TYPE_ALSA_SRC gst_alsa_src_get_type()

#define GST_ALSA_PAD(obj) ((GstAlsaPad*)obj->data) /* obj is a GList */

/* I would have preferred to avoid this variety of trickery, but without it i
 * can't tell whether I'm a source or a sink upon creation. */

typedef struct _GstAlsa GstAlsa;
typedef struct _GstAlsaClass GstAlsaClass;
typedef GstAlsa GstAlsaSink;
typedef GstAlsaClass GstAlsaSinkClass;
typedef GstAlsa GstAlsaSrc;
typedef GstAlsaClass GstAlsaSrcClass;

enum {
    GST_ALSA_OPEN = GST_ELEMENT_FLAG_LAST,
    GST_ALSA_RUNNING,
    GST_ALSA_FLAG_LAST = GST_ELEMENT_FLAG_LAST + 3,
};

typedef gboolean (*GstAlsaProcessFunc) (GstAlsa *, snd_pcm_uframes_t frames);

typedef struct {
    gint channel;
    GstPad *pad;
    GstByteStream *bs;
    /* pointer to where we can access mmap_areas */
    char *access_addr;
    char *buf;
    /* how much of the buffer we have used */
    snd_pcm_uframes_t offset;
} GstAlsaPad;

struct _GstAlsa {
    GstElement parent;

    /* list of GstAlsaPads */
    GList *pads;
    
    gchar *device;
    snd_pcm_stream_t stream;
    
    snd_pcm_t *handle;
    snd_output_t *out;
    
    /* our mmap'd data areas */
    gboolean mmap_open;
    const snd_pcm_channel_area_t *mmap_areas;
    char **access_addr;
    snd_pcm_uframes_t offset;
    snd_pcm_sframes_t avail;
    
    GstAlsaProcessFunc process;
    
    snd_pcm_format_t format;
    guint rate;
    gint channels;
    guint32 mute; /* bitmask. */
    
    /* the gstreamer data */
    gboolean data_interleaved;
    
    /* access to the hardware */
    gboolean access_interleaved;
    guint sample_bytes;
    guint interleave_unit;
    guint interleave_skip;
    
    guint buffer_frames;
    guint period_count; /* 'number of fragments' in oss-speak */
    guint period_frames;
};

struct _GstAlsaClass {
    GstElementClass parent_class;
};

GType gst_alsa_get_type (void);
GType gst_alsa_sink_get_type (void);
GType gst_alsa_src_get_type (void);

#endif /* __ALSA_H__ */
