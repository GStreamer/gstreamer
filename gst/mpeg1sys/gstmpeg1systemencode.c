/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <string.h>
#include <math.h>

/*#define DEBUG_ENABLED */
#include "gstmpeg1systemencode.h"
#include "main.h"

/*#define GST_DEBUG (b...) g_print (##b) */

/* elementfactory information */
static GstElementDetails system_encode_details = {
  "MPEG1 Multiplexer",
  "Codec/Muxer",
  "Multiplexes MPEG-1 Streams",
  "Wim Taymans <wim.taymans@chello.be>"
};

/* GstMPEG1SystemEncode signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  /* FILL ME */
};

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, " "systemstream = (boolean) TRUE")
    );
static GstStaticPadTemplate video_sink_factory =
GST_STATIC_PAD_TEMPLATE ("video_%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("video/mpeg, "
        "mpegversion = (int) 1, " "systemstream = (boolean) FALSE")
    );

static GstStaticPadTemplate audio_sink_factory =
GST_STATIC_PAD_TEMPLATE ("audio_%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("audio/mpeg, "
        "mpegversion = (int) 1, " "layer = (int) [ 1, 2 ] ")
    );

static void gst_system_encode_class_init (GstMPEG1SystemEncodeClass * klass);
static void gst_system_encode_base_init (GstMPEG1SystemEncodeClass * klass);
static void gst_system_encode_init (GstMPEG1SystemEncode * system_encode);

static GstPad *gst_system_encode_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * unused);
static void gst_system_encode_chain (GstPad * pad, GstData * _data);

static void gst_system_encode_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_system_encode_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstElementClass *parent_class = NULL;

/*static guint gst_system_encode_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_mpeg1_system_encode_get_type (void)
{
  static GType system_encode_type = 0;

  if (!system_encode_type) {
    static const GTypeInfo system_encode_info = {
      sizeof (GstMPEG1SystemEncodeClass),
      (GBaseInitFunc) gst_system_encode_base_init,
      NULL,
      (GClassInitFunc) gst_system_encode_class_init,
      NULL,
      NULL,
      sizeof (GstMPEG1SystemEncode),
      0,
      (GInstanceInitFunc) gst_system_encode_init,
      NULL
    };

    system_encode_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstMPEG1SystemEncode",
        &system_encode_info, 0);
  }
  return system_encode_type;
}

static void
gst_system_encode_base_init (GstMPEG1SystemEncodeClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&audio_sink_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&video_sink_factory));
  gst_element_class_set_details (element_class, &system_encode_details);
}

static void
gst_system_encode_class_init (GstMPEG1SystemEncodeClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_system_encode_set_property;
  gobject_class->get_property = gst_system_encode_get_property;

  gstelement_class->request_new_pad = gst_system_encode_request_new_pad;
}

static void
gst_system_encode_init (GstMPEG1SystemEncode * system_encode)
{
  system_encode->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&src_factory),
      "src");
  gst_element_add_pad (GST_ELEMENT (system_encode), system_encode->srcpad);

  system_encode->video_buffer = mpeg1mux_buffer_new (BUFFER_TYPE_VIDEO, 0xE0);
  system_encode->audio_buffer = mpeg1mux_buffer_new (BUFFER_TYPE_AUDIO, 0xC0);
  system_encode->have_setup = FALSE;
  system_encode->mta = NULL;
  system_encode->packet_size = 2048;
  system_encode->lock = g_mutex_new ();
  system_encode->current_pack = system_encode->packets_per_pack = 3;
  system_encode->video_delay_ms = 0;
  system_encode->audio_delay_ms = 0;
  system_encode->sectors_delay = 0;
  system_encode->startup_delay = ~1;
  system_encode->which_streams = 0;
  system_encode->num_audio_pads = 0;
  system_encode->num_video_pads = 0;
  system_encode->pack = g_malloc (sizeof (Pack_struc));
  system_encode->sys_header = g_malloc (sizeof (Sys_header_struc));
  system_encode->sector = g_malloc (sizeof (Sector_struc));

}

static GstPad *
gst_system_encode_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * unused)
{
  GstMPEG1SystemEncode *system_encode;
  gchar *name = NULL;
  GstPad *newpad;

  g_return_val_if_fail (templ != NULL, NULL);

  if (templ->direction != GST_PAD_SINK) {
    g_warning ("system_encode: request pad that is not a SINK pad\n");
    return NULL;
  }
  system_encode = GST_SYSTEM_ENCODE (element);

  if (templ == gst_static_pad_template_get (&audio_sink_factory)) {
    name = g_strdup_printf ("audio_%02d", system_encode->num_audio_pads);
    g_print ("%s\n", name);
    newpad = gst_pad_new_from_template (templ, name);
    gst_pad_set_element_private (newpad,
        GINT_TO_POINTER (system_encode->num_audio_pads));

    system_encode->audio_pad[system_encode->num_audio_pads] = newpad;
    system_encode->num_audio_pads++;
    system_encode->which_streams |= STREAMS_AUDIO;
  } else if (templ == gst_static_pad_template_get (&video_sink_factory)) {
    name = g_strdup_printf ("video_%02d", system_encode->num_video_pads);
    g_print ("%s\n", name);
    newpad = gst_pad_new_from_template (templ, name);
    gst_pad_set_element_private (newpad,
        GINT_TO_POINTER (system_encode->num_video_pads));

    system_encode->video_pad[system_encode->num_video_pads] = newpad;
    system_encode->num_video_pads++;
    system_encode->which_streams |= STREAMS_VIDEO;
  } else {
    g_warning ("system_encode: this is not our template!\n");
    return NULL;
  }

  gst_pad_set_chain_function (newpad, gst_system_encode_chain);
  gst_element_add_pad (GST_ELEMENT (system_encode), newpad);

  return newpad;
}

/* return a list of all the highest prioripty streams */
static GList *
gst_system_encode_pick_streams (GList * mta,
    GstMPEG1SystemEncode * system_encode)
{
  guint64 lowest = ~1;

  GST_DEBUG ("pick_streams: %" G_GINT64_FORMAT ", %" G_GINT64_FORMAT,
      system_encode->video_buffer->next_frame_time,
      system_encode->audio_buffer->next_frame_time);

  if (system_encode->which_streams & STREAMS_VIDEO) {
    if (system_encode->video_buffer->next_frame_time <
        lowest - system_encode->video_delay) {
      lowest = system_encode->video_buffer->next_frame_time;
    }
  }
  if (system_encode->which_streams & STREAMS_AUDIO) {
    if (system_encode->audio_buffer->next_frame_time <
        lowest - system_encode->audio_delay) {
      lowest = system_encode->audio_buffer->next_frame_time;
    }
  }

  if (system_encode->which_streams & STREAMS_VIDEO) {
    if (system_encode->video_buffer->next_frame_time == lowest) {
      mta = g_list_append (mta, system_encode->video_buffer);
    }
  }
  if (system_encode->which_streams & STREAMS_AUDIO) {
    if (system_encode->audio_buffer->next_frame_time == lowest) {
      mta = g_list_append (mta, system_encode->audio_buffer);
    }
  }
  return mta;
}

static gboolean
gst_system_encode_have_data (GstMPEG1SystemEncode * system_encode)
{

  if (system_encode->which_streams == (STREAMS_VIDEO | STREAMS_AUDIO)) {
    if (MPEG1MUX_BUFFER_QUEUED (system_encode->audio_buffer) > 2 &&
        MPEG1MUX_BUFFER_SPACE (system_encode->audio_buffer) >
        system_encode->packet_size * 2
        && MPEG1MUX_BUFFER_QUEUED (system_encode->video_buffer) > 2
        && MPEG1MUX_BUFFER_SPACE (system_encode->video_buffer) >
        system_encode->packet_size * 2) {
      return TRUE;
    }
  }
  if (system_encode->which_streams == STREAMS_VIDEO) {
    if (MPEG1MUX_BUFFER_QUEUED (system_encode->video_buffer) > 2 &&
        MPEG1MUX_BUFFER_SPACE (system_encode->video_buffer) >
        system_encode->packet_size * 2) {
      return TRUE;
    }
  }
  if (system_encode->which_streams == STREAMS_VIDEO) {
    if (MPEG1MUX_BUFFER_QUEUED (system_encode->audio_buffer) > 2 &&
        MPEG1MUX_BUFFER_SPACE (system_encode->audio_buffer) >
        system_encode->packet_size * 2) {
      return TRUE;
    }
  }

  return FALSE;
}

static GList *
gst_system_encode_update_mta (GstMPEG1SystemEncode * system_encode, GList * mta,
    gulong size)
{
  GList *streams = g_list_first (mta);
  Mpeg1MuxBuffer *mb = (Mpeg1MuxBuffer *) streams->data;

  GST_DEBUG ("system_encode::multiplex: update mta");

  mpeg1mux_buffer_shrink (mb, size);

  mta = g_list_remove (mta, mb);

  return mta;
}

static void
gst_system_setup_multiplex (GstMPEG1SystemEncode * system_encode)
{
  Mpeg1MuxTimecode *video_tc, *audio_tc;

  system_encode->audio_buffer_size = 4 * 1024;
  system_encode->video_buffer_size = 46 * 1024;
  system_encode->bytes_output = 0;
  system_encode->min_packet_data =
      system_encode->packet_size - PACK_HEADER_SIZE - SYS_HEADER_SIZE -
      PACKET_HEADER_SIZE - AFTER_PACKET_LENGTH;
  system_encode->max_packet_data =
      system_encode->packet_size - PACKET_HEADER_SIZE - AFTER_PACKET_LENGTH;

  if (system_encode->which_streams & STREAMS_VIDEO) {
    system_encode->video_rate =
        system_encode->video_buffer->info.video.bit_rate * 50;
  } else
    system_encode->video_rate = 0;
  if (system_encode->which_streams & STREAMS_AUDIO)
    system_encode->audio_rate =
        system_encode->audio_buffer->info.audio.bit_rate * 128;
  else
    system_encode->audio_rate = 0;

  system_encode->data_rate =
      system_encode->video_rate + system_encode->audio_rate;

  system_encode->dmux_rate = ceil ((double) (system_encode->data_rate) *
      ((double) (system_encode->packet_size) /
          (double) (system_encode->min_packet_data) +
          ((double) (system_encode->packet_size) /
              (double) (system_encode->max_packet_data) *
              (double) (system_encode->packets_per_pack -
                  1.))) / (double) (system_encode->packets_per_pack));
  system_encode->data_rate = ceil (system_encode->dmux_rate / 50.) * 50;

  GST_DEBUG
      ("system_encode::multiplex: data_rate %u, video_rate: %u, audio_rate: %u",
      system_encode->data_rate, system_encode->video_rate,
      system_encode->audio_rate);

  system_encode->video_delay =
      (double) system_encode->video_delay_ms * (double) (CLOCKS / 1000);
  system_encode->audio_delay =
      (double) system_encode->audio_delay_ms * (double) (CLOCKS / 1000);

  system_encode->mux_rate = ceil (system_encode->dmux_rate / 50.);
  system_encode->dmux_rate = system_encode->mux_rate * 50.;

  video_tc = MPEG1MUX_BUFFER_FIRST_TIMECODE (system_encode->video_buffer);
  audio_tc = MPEG1MUX_BUFFER_FIRST_TIMECODE (system_encode->audio_buffer);

  GST_DEBUG ("system_encode::video tc %" G_GINT64_FORMAT ", audio tc %"
      G_GINT64_FORMAT ":", video_tc->DTS, audio_tc->DTS);

  system_encode->delay = ((double) system_encode->sectors_delay +
      ceil ((double) video_tc->length /
          (double) system_encode->min_packet_data) +
      ceil ((double) video_tc->length /
          (double) system_encode->min_packet_data)) *
      (double) system_encode->packet_size / system_encode->dmux_rate *
      (double) CLOCKS;

  system_encode->audio_delay += system_encode->delay;
  system_encode->video_delay += system_encode->delay;

  system_encode->audio_delay = 0;
  system_encode->video_delay = 0;
  system_encode->delay = 0;

  GST_DEBUG ("system_encode::multiplex: delay %g, mux_rate: %lu",
      system_encode->delay, system_encode->mux_rate);
}

static void
gst_system_encode_multiplex (GstMPEG1SystemEncode * system_encode)
{
  GList *streams;
  Mpeg1MuxBuffer *mb = (Mpeg1MuxBuffer *) streams->data;
  guchar timestamps;
  guchar buffer_scale;
  GstBuffer *outbuf;
  Pack_struc *pack;
  Sys_header_struc *sys_header;
  Mpeg1MuxTimecode *tc;
  gulong buffer_size, non_scaled_buffer_size, total_queued;
  guint64 PTS, DTS;

  g_mutex_lock (system_encode->lock);

  while (gst_system_encode_have_data (system_encode)) {
    GST_DEBUG ("system_encode::multiplex: multiplexing");

    if (!system_encode->have_setup) {
      gst_system_setup_multiplex (system_encode);
      system_encode->have_setup = TRUE;
    }

    if (system_encode->mta == NULL) {
      system_encode->mta =
          gst_system_encode_pick_streams (system_encode->mta, system_encode);
    }
    if (system_encode->mta == NULL)
      break;


    system_encode->SCR =
        (guint64) (system_encode->bytes_output +
        LAST_SCR_BYTE_IN_PACK) * CLOCKS / system_encode->dmux_rate;


    streams = g_list_first (system_encode->mta);
    mb = (Mpeg1MuxBuffer *) streams->data;

    if (system_encode->current_pack == system_encode->packets_per_pack) {
      create_pack (system_encode->pack, system_encode->SCR,
          system_encode->mux_rate);
      create_sys_header (system_encode->sys_header, system_encode->mux_rate, 1,
          1, 1, 1, 1, 1, AUDIO_STR_0, 0, system_encode->audio_buffer_size / 128,
          VIDEO_STR_0, 1, system_encode->video_buffer_size / 1024,
          system_encode->which_streams);
      system_encode->current_pack = 0;
      pack = system_encode->pack;
      sys_header = system_encode->sys_header;
    } else {
      system_encode->current_pack++;
      pack = NULL;
      sys_header = NULL;
    }

    tc = MPEG1MUX_BUFFER_FIRST_TIMECODE (mb);
    if (mb->new_frame) {
      GST_DEBUG ("system_encode::multiplex: new frame");
      if (tc->frame_type == FRAME_TYPE_AUDIO
          || tc->frame_type == FRAME_TYPE_IFRAME
          || tc->frame_type == FRAME_TYPE_PFRAME) {
        timestamps = TIMESTAMPS_PTS;
      } else {
        timestamps = TIMESTAMPS_PTS_DTS;
      }
    } else {
      timestamps = TIMESTAMPS_NO;
    }

    if (tc->frame_type != FRAME_TYPE_AUDIO) {
      if (tc->PTS < system_encode->startup_delay)
        system_encode->startup_delay = tc->PTS;
    }

    if (tc->frame_type == FRAME_TYPE_AUDIO) {
      buffer_scale = 0;
      non_scaled_buffer_size = system_encode->audio_buffer_size;
      buffer_size = system_encode->audio_buffer_size / 128;
      PTS = tc->PTS + system_encode->audio_delay + system_encode->startup_delay;
      DTS = tc->PTS + system_encode->audio_delay + system_encode->startup_delay;
    } else {
      buffer_scale = 1;
      non_scaled_buffer_size = system_encode->video_buffer_size;
      buffer_size = system_encode->video_buffer_size / 1024;
      PTS = tc->PTS + system_encode->video_delay;
      DTS = tc->DTS + system_encode->video_delay;
    }

    total_queued = mpeg1mux_buffer_update_queued (mb, system_encode->SCR);

    if (non_scaled_buffer_size - total_queued >= system_encode->packet_size) {

      /* write the pack/packet here */
      create_sector (system_encode->sector, pack, sys_header,
          system_encode->packet_size,
          MPEG1MUX_BUFFER_DATA (mb), mb->stream_id, buffer_scale,
          buffer_size, TRUE, PTS, DTS,
          timestamps, system_encode->which_streams);
      /* update mta */
      system_encode->mta =
          gst_system_encode_update_mta (system_encode, system_encode->mta,
          system_encode->sector->length_of_packet_data);
    } else {
      /* write  a padding packet */
      create_sector (system_encode->sector, pack, sys_header,
          system_encode->packet_size, NULL, PADDING_STR, 0,
          0, FALSE, 0, 0, TIMESTAMPS_NO, system_encode->which_streams);
    }

    outbuf = gst_buffer_new ();
    GST_BUFFER_DATA (outbuf) =
        g_malloc (system_encode->sector->length_of_sector);
    GST_BUFFER_SIZE (outbuf) = system_encode->sector->length_of_sector;
    memcpy (GST_BUFFER_DATA (outbuf), system_encode->sector->buf,
        system_encode->sector->length_of_sector);
    system_encode->bytes_output += GST_BUFFER_SIZE (outbuf);
    gst_pad_push (system_encode->srcpad, GST_DATA (outbuf));

    GST_DEBUG ("system_encode::multiplex: writing %02x", mb->stream_id);

  }
  gst_info ("system_encode::multiplex: data left in video buffer %lu\n",
      MPEG1MUX_BUFFER_SPACE (system_encode->video_buffer));
  gst_info ("system_encode::multiplex: data left in audio buffer %lu\n",
      MPEG1MUX_BUFFER_SPACE (system_encode->audio_buffer));

  g_mutex_unlock (system_encode->lock);
}

static void
gst_system_encode_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstMPEG1SystemEncode *system_encode;
  guchar *data;
  gulong size;
  const gchar *padname;
  gint channel;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  system_encode = GST_SYSTEM_ENCODE (GST_OBJECT_PARENT (pad));
  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  GST_DEBUG ("system_encode::chain: system_encode: have buffer of size %lu",
      size);
  padname = GST_OBJECT_NAME (pad);

  if (strncmp (padname, "audio_", 6) == 0) {
    channel = atoi (&padname[6]);
    GST_DEBUG
        ("gst_system_encode_chain: got audio buffer in from audio channel %02d",
        channel);

    mpeg1mux_buffer_queue (system_encode->audio_buffer, buf);
  } else if (strncmp (padname, "video_", 6) == 0) {
    channel = atoi (&padname[6]);
    GST_DEBUG
        ("gst_system_encode_chain: got video buffer in from video channel %02d",
        channel);

    mpeg1mux_buffer_queue (system_encode->video_buffer, buf);

  } else {
    g_assert_not_reached ();
  }
  gst_system_encode_multiplex (system_encode);

  gst_buffer_unref (buf);
}

static void
gst_system_encode_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMPEG1SystemEncode *system_encode;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_SYSTEM_ENCODE (object));
  system_encode = GST_SYSTEM_ENCODE (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_system_encode_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstMPEG1SystemEncode *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_SYSTEM_ENCODE (object));
  src = GST_SYSTEM_ENCODE (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  /* this filter needs the getbits functions */
  if (!gst_library_load ("gstgetbits"))
    return FALSE;

  return gst_element_register (plugin, "system_encode",
      GST_RANK_NONE, GST_TYPE_SYSTEM_ENCODE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "system_encode",
    "MPEG-1 system stream encoder",
    plugin_init, VERSION, "GPL", GST_PACKAGE, GST_ORIGIN)
