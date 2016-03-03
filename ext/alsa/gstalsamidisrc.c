/* GStreamer
 * Copyright (C) 2014  Antonio Ospite <ao2@ao2.it>
 *
 * gstalsamidisrc.c: Source element for ALSA MIDI sequencer events
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
/**
 * SECTION:element-alsamidisrc
 * @see_also: #GstPushSrc
 *
 * The alsamidisrc element is an element that fetches ALSA MIDI sequencer
 * events and makes them available to elements understanding
 * audio/x-midi-events in their sink pads.
 *
 * It can be used to generate notes from a MIDI input device.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v alsamidisrc ports=129:0 ! fluiddec ! audioconvert ! autoaudiosink
 * ]| This pipeline will listen for events from the sequencer device at port 129:0,
 * and generate notes using the fluiddec element.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstalsamidisrc.h"

GST_DEBUG_CATEGORY_STATIC (gst_alsa_midi_src_debug);
#define GST_CAT_DEFAULT gst_alsa_midi_src_debug

/*
 * The MIDI specification declares some status bytes undefined:
 *
 *  - 0xF4 System common - Undefined (Reserved)
 *  - 0xF5 System common - Undefined (Reserved)
 *  - 0xF9 System real-time - Undefined (Reserved)
 *  - 0xFD System real-time - Undefined (Reserved)
 *
 * See: http://www.midi.org/techspecs/midimessages.php#2
 *
 * Some other documents define status 0xf9 as a tick message with a period of
 * 10ms:
 *
 *  - http://www.blitter.com/~russtopia/MIDI/~jglatt/tech/midispec/tick.htm
 *  - http://www.sequencer.de/synth/index.php/MIDI_Format#0xf9_-_MIDI_Tick
 *
 * Even if non-standard it looks like this convention is quite widespread.
 *
 * For instance Fluidsynth uses 0xF9 as a "midi tick" message:
 * http://sourceforge.net/p/fluidsynth/code-git/ci/master/tree/fluidsynth/src/midi/fluid_midi.h#l62
 *
 * And then so does the midiparse element in order to be compatible with
 * Fluidsynth and the fluiddec element.
 *
 * Do the same to behave like midiparse.
 */
#define MIDI_TICK 0xf9
#define MIDI_TICK_PERIOD_MS 10

/* Functions specific to the Alsa MIDI sequencer API */

#define DEFAULT_BUFSIZE 65536
#define DEFAULT_CLIENT_NAME "alsamidisrc"
#define DEFAULT_POLL_TIMEOUT_MS (MIDI_TICK_PERIOD_MS / 2)

static int
init_seq (GstAlsaMidiSrc * alsamidisrc)
{
  int ret;

  ret = snd_seq_open (&alsamidisrc->seq, "default", SND_SEQ_OPEN_DUPLEX, 0);
  if (ret < 0) {
    GST_ERROR_OBJECT (alsamidisrc, "Cannot open sequencer - %s",
        snd_strerror (ret));
    goto error;
  }

  ret = snd_seq_set_client_name (alsamidisrc->seq, DEFAULT_CLIENT_NAME);
  if (ret < 0) {
    GST_ERROR_OBJECT (alsamidisrc, "Cannot set client name - %s",
        snd_strerror (ret));
    goto error_seq_close;
  }

  return 0;

error_seq_close:
  snd_seq_close (alsamidisrc->seq);
error:
  return ret;
}

/* Parses one or more port addresses from the string */
static int
parse_ports (const char *arg, GstAlsaMidiSrc * alsamidisrc)
{
  gchar **ports_list;
  guint i;
  int ret = 0;

  GST_DEBUG_OBJECT (alsamidisrc, "ports: %s", arg);

  /*
   * Assume that ports are separated by commas.
   *
   * Commas are used instead of spaces because those are valid in client
   * names.
   */
  ports_list = g_strsplit (arg, ",", 0);

  alsamidisrc->port_count = g_strv_length (ports_list);
  alsamidisrc->seq_ports = g_try_new (snd_seq_addr_t, alsamidisrc->port_count);
  if (!alsamidisrc->seq_ports) {
    GST_ERROR_OBJECT (alsamidisrc, "Out of memory");
    ret = -ENOMEM;
    goto out_free_ports_list;
  }

  for (i = 0; i < alsamidisrc->port_count; i++) {
    gchar *port_name = ports_list[i];

    ret = snd_seq_parse_address (alsamidisrc->seq, &alsamidisrc->seq_ports[i],
        port_name);
    if (ret < 0) {
      GST_ERROR_OBJECT (alsamidisrc, "Invalid port %s - %s", port_name,
          snd_strerror (ret));
      goto error_free_seq_ports;
    }
  }

  goto out_free_ports_list;

error_free_seq_ports:
  g_free (alsamidisrc->seq_ports);
out_free_ports_list:
  g_strfreev (ports_list);
  return ret;
}

static int
create_port (GstAlsaMidiSrc * alsamidisrc)
{
  int ret;

  ret = snd_seq_create_simple_port (alsamidisrc->seq, DEFAULT_CLIENT_NAME,
      SND_SEQ_PORT_CAP_WRITE |
      SND_SEQ_PORT_CAP_SUBS_WRITE,
      SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
  if (ret < 0)
    GST_ERROR_OBJECT (alsamidisrc, "Cannot create port - %s",
        snd_strerror (ret));

  return ret;
}

static void
connect_ports (GstAlsaMidiSrc * alsamidisrc)
{
  int i;
  int ret;

  for (i = 0; i < alsamidisrc->port_count; ++i) {
    ret =
        snd_seq_connect_from (alsamidisrc->seq, 0,
        alsamidisrc->seq_ports[i].client, alsamidisrc->seq_ports[i].port);
    if (ret < 0)
      /* Issue a warning and try the other ports */
      GST_WARNING_OBJECT (alsamidisrc, "Cannot connect from port %d:%d - %s",
          alsamidisrc->seq_ports[i].client, alsamidisrc->seq_ports[i].port,
          snd_strerror (ret));
  }
}

/* GStreamer specific functions */

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-midi-event"));

#define DEFAULT_PORTS           NULL

enum
{
  PROP_0,
  PROP_PORTS,
  PROP_LAST,
};

#define _do_init \
    GST_DEBUG_CATEGORY_INIT (gst_alsa_midi_src_debug, "alsamidisrc", 0, "alsamidisrc element");
#define gst_alsa_midi_src_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstAlsaMidiSrc, gst_alsa_midi_src, GST_TYPE_PUSH_SRC,
    _do_init);

static void gst_alsa_midi_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_alsa_midi_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_alsa_midi_src_start (GstBaseSrc * basesrc);
static gboolean gst_alsa_midi_src_stop (GstBaseSrc * basesrc);

static GstFlowReturn
gst_alsa_midi_src_create (GstPushSrc * src, GstBuffer ** buf);

static void
gst_alsa_midi_src_class_init (GstAlsaMidiSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbase_src_class;
  GstPushSrcClass *gstpush_src_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);
  gstbase_src_class = GST_BASE_SRC_CLASS (klass);
  gstpush_src_class = GST_PUSH_SRC_CLASS (klass);

  gobject_class->set_property = gst_alsa_midi_src_set_property;
  gobject_class->get_property = gst_alsa_midi_src_get_property;

  g_object_class_install_property (gobject_class, PROP_PORTS,
      g_param_spec_string ("ports", "Ports",
          "Comma separated list of sequencer ports (e.g. client:port,...)",
          DEFAULT_PORTS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (gstelement_class,
      "AlsaMidi Source",
      "Source",
      "Push ALSA MIDI sequencer events around", "Antonio Ospite <ao2@ao2.it>");
  gst_element_class_add_static_pad_template (gstelement_class, &srctemplate);

  gstbase_src_class->start = GST_DEBUG_FUNCPTR (gst_alsa_midi_src_start);
  gstbase_src_class->stop = GST_DEBUG_FUNCPTR (gst_alsa_midi_src_stop);
  gstpush_src_class->create = GST_DEBUG_FUNCPTR (gst_alsa_midi_src_create);
}

static void
gst_alsa_midi_src_init (GstAlsaMidiSrc * alsamidisrc)
{
  alsamidisrc->ports = DEFAULT_PORTS;

  gst_base_src_set_format (GST_BASE_SRC (alsamidisrc), GST_FORMAT_TIME);
  gst_base_src_set_live (GST_BASE_SRC (alsamidisrc), TRUE);
}

static void
gst_alsa_midi_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAlsaMidiSrc *src;

  src = GST_ALSA_MIDI_SRC (object);

  switch (prop_id) {
    case PROP_PORTS:
      g_free (src->ports);
      src->ports = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_alsa_midi_src_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstAlsaMidiSrc *src;

  g_return_if_fail (GST_IS_ALSA_MIDI_SRC (object));

  src = GST_ALSA_MIDI_SRC (object);

  switch (prop_id) {
    case PROP_PORTS:
      g_value_set_string (value, src->ports);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstBuffer *
prepare_buffer (GstAlsaMidiSrc * alsamidisrc, gpointer data, guint size)
{
  GstClockTime time;
  gpointer local_data;
  GstBuffer *buffer;

  buffer = gst_buffer_new ();

  time = alsamidisrc->tick * MIDI_TICK_PERIOD_MS * GST_MSECOND;

  GST_BUFFER_DTS (buffer) = time;
  GST_BUFFER_PTS (buffer) = time;
  GST_BUFFER_OFFSET (buffer) = time;
  GST_BUFFER_DURATION (buffer) = MIDI_TICK_PERIOD_MS * GST_MSECOND;

  local_data = g_memdup (data, size);

  gst_buffer_append_memory (buffer,
      gst_memory_new_wrapped (0, local_data, size, 0, size, local_data,
          g_free));

  GST_MEMDUMP_OBJECT (alsamidisrc, "MIDI data:", local_data, size);

  alsamidisrc->tick += 1;

  return buffer;
}

static void
push_buffer (GstAlsaMidiSrc * alsamidisrc, gpointer data, guint size,
    GstBufferList * buffer_list)
{
  gst_buffer_list_add (buffer_list, prepare_buffer (alsamidisrc, data, size));
}

static void
push_tick_buffer (GstAlsaMidiSrc * alsamidisrc, GstBufferList * buffer_list)
{
  alsamidisrc->buffer[0] = MIDI_TICK;
  push_buffer (alsamidisrc, alsamidisrc->buffer, 1, buffer_list);
}

static GstFlowReturn
gst_alsa_midi_src_create (GstPushSrc * src, GstBuffer ** buf)
{
  GstAlsaMidiSrc *alsamidisrc;
  GstBufferList *buffer_list;
  long size_ev = 0;
  int err;
  int ret;
  guint len;

  alsamidisrc = GST_ALSA_MIDI_SRC (src);

  buffer_list = gst_buffer_list_new ();

  snd_seq_poll_descriptors (alsamidisrc->seq, alsamidisrc->pfds,
      alsamidisrc->npfds, POLLIN);

  /*
   * The file descriptors are polled with a timeout _less_ than 10ms (the MIDI
   * tick period) in order not to loose events because of possible overlaps
   * with MIDI ticks.
   *
   * If the polling times out (no new events) then a MIDI-tick event gets
   * generated in order to keep the pipeline alive and progressing.
   *
   * If new events are present, then they are decoded and queued in
   * a buffer_list. One buffer per event will be queued, all with different
   * timestamps (see the prepare_buffer() function); maybe this can be
   * optimized but a as a proof-of-concept mechanism it works OK.
   */
  ret = poll (alsamidisrc->pfds, alsamidisrc->npfds, DEFAULT_POLL_TIMEOUT_MS);
  if (ret < 0) {
    GST_ERROR_OBJECT (alsamidisrc, "ERROR in poll: %s", strerror (errno));
  } else if (ret == 0) {
    push_tick_buffer (alsamidisrc, buffer_list);
  } else {
    /* There are events available */
    do {
      snd_seq_event_t *event;
      err = snd_seq_event_input (alsamidisrc->seq, &event);
      if (err < 0)
        break;                  /* Processed all events */

      if (event) {
        size_ev =
            snd_midi_event_decode (alsamidisrc->parser, alsamidisrc->buffer,
            DEFAULT_BUFSIZE, event);
        if (size_ev < 0) {
          /* ENOENT indicates an event that is not a MIDI message, silently skip it */
          if (-ENOENT == size_ev) {
            GST_WARNING_OBJECT (alsamidisrc,
                "Warning: Received non-MIDI message");
            push_tick_buffer (alsamidisrc, buffer_list);
          } else {
            GST_ERROR_OBJECT (alsamidisrc,
                "Error decoding event from ALSA to output: %s",
                strerror (-size_ev));
            goto error;
          }
        } else {
          push_buffer (alsamidisrc, alsamidisrc->buffer, size_ev, buffer_list);
        }
      }
    } while (err > 0);
  }

  len = gst_buffer_list_length (buffer_list);
  if (len == 0)
    goto error;

  /* Pop the _last_ buffer in the list */
  *buf = gst_buffer_copy (gst_buffer_list_get (buffer_list, len - 1));
  gst_buffer_list_remove (buffer_list, len - 1, 1);
  --len;

  /* 
   * If there are no more buffers left, free the list, otherwise push all the
   * _previous_ buffers left in the list.
   *
   * The one popped above will be pushed last when this function returns.
   */
  if (len == 0)
    gst_buffer_list_unref (buffer_list);
  else
    gst_pad_push_list (GST_BASE_SRC (src)->srcpad, buffer_list);

  return GST_FLOW_OK;

error:
  gst_buffer_list_unref (buffer_list);
  return GST_FLOW_ERROR;
}

static gboolean
gst_alsa_midi_src_start (GstBaseSrc * basesrc)
{
  GstAlsaMidiSrc *alsamidisrc;
  int ret;

  alsamidisrc = GST_ALSA_MIDI_SRC (basesrc);

  alsamidisrc->tick = 0;
  alsamidisrc->port_count = 0;

  ret = init_seq (alsamidisrc);
  if (ret < 0)
    goto err;

  if (alsamidisrc->ports) {
    ret = parse_ports (alsamidisrc->ports, alsamidisrc);
    if (ret < 0)
      goto error_seq_close;
  }

  ret = create_port (alsamidisrc);
  if (ret < 0)
    goto error_free_seq_ports;

  connect_ports (alsamidisrc);

  ret = snd_seq_nonblock (alsamidisrc->seq, 1);
  if (ret < 0) {
    GST_ERROR_OBJECT (alsamidisrc, "Cannot set nonblock mode - %s",
        snd_strerror (ret));
    goto error_free_seq_ports;
  }

  snd_midi_event_new (DEFAULT_BUFSIZE, &alsamidisrc->parser);
  snd_midi_event_init (alsamidisrc->parser);
  snd_midi_event_reset_decode (alsamidisrc->parser);

  snd_midi_event_no_status (alsamidisrc->parser, 1);

  alsamidisrc->buffer = g_try_malloc (DEFAULT_BUFSIZE);
  if (alsamidisrc->buffer == NULL)
    goto error_free_parser;

  alsamidisrc->npfds =
      snd_seq_poll_descriptors_count (alsamidisrc->seq, POLLIN);
  alsamidisrc->pfds =
      g_try_malloc (sizeof (*alsamidisrc->pfds) * alsamidisrc->npfds);
  if (alsamidisrc->pfds == NULL)
    goto error_free_buffer;

  return TRUE;

error_free_buffer:
  g_free (alsamidisrc->buffer);
error_free_parser:
  snd_midi_event_free (alsamidisrc->parser);
error_free_seq_ports:
  g_free (alsamidisrc->seq_ports);
error_seq_close:
  snd_seq_close (alsamidisrc->seq);
err:
  return FALSE;
}

static gboolean
gst_alsa_midi_src_stop (GstBaseSrc * basesrc)
{
  GstAlsaMidiSrc *alsamidisrc;

  alsamidisrc = GST_ALSA_MIDI_SRC (basesrc);

  g_free (alsamidisrc->pfds);
  g_free (alsamidisrc->buffer);
  snd_midi_event_free (alsamidisrc->parser);
  g_free (alsamidisrc->seq_ports);
  snd_seq_close (alsamidisrc->seq);

  return TRUE;
}
