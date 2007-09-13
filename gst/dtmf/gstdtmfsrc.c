/* GStreamer DTMF source
 *
 * gstdtmfsrc.c:
 *
 * Copyright (C) <2007> Collabora.
 *   Contact: Youness Alaoui <youness.alaoui@collabora.co.uk>
 * Copyright (C) <2007> Nokia Corporation.
 *   Contact: Zeeshan Ali <zeeshan.ali@nokia.com>
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *               2000,2005 Wim Taymans <wim@fluendo.com>
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

/**
 * SECTION:element-dtmfsrc
 * @short_description: Generates DTMF packets
 *
 * <refsect2>
 *
 * <para>
 * The DTMFSrc element generates DTMF (ITU-T Q.23 Specification) tone packets on request
 * from application. The application communicates the beginning and end of a
 * DTMF event using custom upstream gstreamer events. To report a DTMF event, an
 * application must send an event of type GST_EVENT_CUSTOM_UPSTREAM, having a
 * structure of name "dtmf-event" with fields set according to the following
 * table:
 * </para>
 *
 * <para>
 * <informaltable>
 * <tgroup cols='4'>
 * <colspec colname='Name' />
 * <colspec colname='Type' />
 * <colspec colname='Possible values' />
 * <colspec colname='Purpose' />
 *
 * <thead>
 * <row>
 * <entry>Name</entry>
 * <entry>GType</entry>
 * <entry>Possible values</entry>
 * <entry>Purpose</entry>
 * </row>
 * </thead>
 *
 * <tbody>
 * <row>
 * <entry>type</entry>
 * <entry>G_TYPE_INT</entry>
 * <entry>0-1</entry>
 * <entry>The application uses this field to specify which of the two methods
 * specified in RFC 2833 to use. The value should be 0 for tones and 1 for
 * named events. This element is only capable of generating tones.
 * </entry>
 * </row>
 * <row>
 * <entry>number</entry>
 * <entry>G_TYPE_INT</entry>
 * <entry>0-16</entry>
 * <entry>The event number.</entry>
 * </row>
 * <row>
 * <entry>volume</entry>
 * <entry>G_TYPE_INT</entry>
 * <entry>0-36</entry>
 * <entry>This field describes the power level of the tone, expressed in dBm0
 * after dropping the sign. Power levels range from 0 to -63 dBm0. The range of
 * valid DTMF is from 0 to -36 dBm0. Can be omitted if start is set to FALSE.
 * </entry>
 * </row>
 * <row>
 * <entry>start</entry>
 * <entry>G_TYPE_BOOLEAN</entry>
 * <entry>True or False</entry>
 * <entry>Whether the event is starting or ending.</entry>
 * </row>
 * <row>
 * <entry>method</entry>
 * <entry>G_TYPE_INT</entry>
 * <entry>1</entry>
 * <entry>The method used for sending event, this element will react if this field
 * is absent or 2.
 * </entry>
 * </row>
 * </tbody>
 * </tgroup>
 * </informaltable>
 * </para>
 *
 * <para>For example, the following code informs the pipeline (and in turn, the
 * DTMFSrc element inside the pipeline) about the start of a DTMF named
 * event '1' of volume -25 dBm0:
 * </para>
 *
 * <para>
 * <programlisting>
 * structure = gst_structure_new ("dtmf-event",
 *                    "type", G_TYPE_INT, 0,
 *                    "number", G_TYPE_INT, 1,
 *                    "volume", G_TYPE_INT, 25,
 *                    "start", G_TYPE_BOOLEAN, TRUE, NULL);
 *
 * event = gst_event_new_custom (GST_EVENT_CUSTOM_UPSTREAM, structure);
 * gst_element_send_event (pipeline, event);
 * </programlisting>
 * </para>
 *
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <glib.h>

#ifndef M_PI
# define M_PI           3.14159265358979323846  /* pi */
#endif


#include "gstdtmfsrc.h"

#define GST_TONE_DTMF_TYPE_EVENT 0
#define DEFAULT_PACKET_INTERVAL  50 /* ms */
#define MIN_PACKET_INTERVAL      10 /* ms */
#define MAX_PACKET_INTERVAL      50 /* ms */
#define SAMPLE_RATE              8000
#define SAMPLE_SIZE              16
#define CHANNELS                 1
#define MIN_EVENT                0
#define MAX_EVENT                16
#define MIN_VOLUME               0
#define MAX_VOLUME               36
#define MIN_INTER_DIGIT_INTERVAL 100
#define MIN_PULSE_DURATION       250
#define MIN_DUTY_CYCLE           (MIN_INTER_DIGIT_INTERVAL + MIN_PULSE_DURATION)


typedef struct st_dtmf_key {
        char *event_name;
        int event_encoding;
        float low_frequency;
        float high_frequency;
} DTMF_KEY;

static const DTMF_KEY DTMF_KEYS[] = {
        {"DTMF_KEY_EVENT_0",  0, 941, 1336},
        {"DTMF_KEY_EVENT_1",  1, 697, 1209},
        {"DTMF_KEY_EVENT_2",  2, 697, 1336},
        {"DTMF_KEY_EVENT_3",  3, 697, 1477},
        {"DTMF_KEY_EVENT_4",  4, 770, 1209},
        {"DTMF_KEY_EVENT_5",  5, 770, 1336},
        {"DTMF_KEY_EVENT_6",  6, 770, 1477},
        {"DTMF_KEY_EVENT_7",  7, 852, 1209},
        {"DTMF_KEY_EVENT_8",  8, 852, 1336},
        {"DTMF_KEY_EVENT_9",  9, 852, 1477},
        {"DTMF_KEY_EVENT_S", 10, 941, 1209},
        {"DTMF_KEY_EVENT_P", 11, 941, 1477},
        {"DTMF_KEY_EVENT_A", 12, 697, 1633},
        {"DTMF_KEY_EVENT_B", 13, 770, 1633},
        {"DTMF_KEY_EVENT_C", 14, 852, 1633},
        {"DTMF_KEY_EVENT_D", 15, 941, 1633},
};

#define MAX_DTMF_EVENTS 16

enum {
DTMF_KEY_EVENT_1 = 1,
DTMF_KEY_EVENT_2 = 2,
DTMF_KEY_EVENT_3 = 3,
DTMF_KEY_EVENT_4 = 4,
DTMF_KEY_EVENT_5 = 5,
DTMF_KEY_EVENT_6 = 6,
DTMF_KEY_EVENT_7 = 7,
DTMF_KEY_EVENT_8 = 8,
DTMF_KEY_EVENT_9 = 9,
DTMF_KEY_EVENT_0 = 0,
DTMF_KEY_EVENT_STAR = 10,
DTMF_KEY_EVENT_POUND = 11,
DTMF_KEY_EVENT_A = 12,
DTMF_KEY_EVENT_B = 13,
DTMF_KEY_EVENT_C = 14,
DTMF_KEY_EVENT_D = 15,
};

/* elementfactory information */
static const GstElementDetails gst_dtmf_src_details =
GST_ELEMENT_DETAILS ("DTMF tone generator",
    "Source/Audio",
    "Generates DTMF tones",
    "Youness Alaoui <youness.alaoui@collabora.co.uk>");

GST_DEBUG_CATEGORY_STATIC (gst_dtmf_src_debug);
#define GST_CAT_DEFAULT gst_dtmf_src_debug

enum
{
  PROP_0,
  PROP_INTERVAL,
};

static GstStaticPadTemplate gst_dtmf_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "endianness = (int) 1234, "
        "signed = (bool) true, "
        "rate = (int) 8000, "
        "channels = (int) 1")
    );

GST_BOILERPLATE (GstDTMFSrc, gst_dtmf_src, GstBaseSrc, GST_TYPE_BASE_SRC);

static void gst_dtmf_src_finalize (GObject * object);

static void gst_dtmf_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_dtmf_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_dtmf_src_handle_event (GstBaseSrc *src, GstEvent * event);
static GstStateChangeReturn gst_dtmf_src_change_state (GstElement * element,
    GstStateChange transition);
static void gst_dtmf_src_generate_tone(GstDTMFSrcEvent *event, DTMF_KEY key,
    float duration, GstBuffer * buffer);
static GstFlowReturn gst_dtmf_src_create (GstBaseSrc * basesrc,
    guint64 offset, guint length, GstBuffer ** buffer);
static void gst_dtmf_src_add_start_event (GstDTMFSrc *dtmfsrc,
    gint event_number, gint event_volume);
static void gst_dtmf_src_add_stop_event (GstDTMFSrc *dtmfsrc);

static void gst_dtmf_src_get_times (GstBaseSrc * basesrc,
    GstBuffer * buffer, GstClockTime * start, GstClockTime * end);



static void
gst_dtmf_src_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  GST_DEBUG_CATEGORY_INIT (gst_dtmf_src_debug,
          "dtmfsrc", 0, "dtmfsrc element");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_dtmf_src_template));

  gst_element_class_set_details (element_class, &gst_dtmf_src_details);
}

static void
gst_dtmf_src_class_init (GstDTMFSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstElementClass *gstelement_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstbasesrc_class = GST_BASE_SRC_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);


  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_dtmf_src_finalize);
  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_dtmf_src_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_dtmf_src_get_property);

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_INTERVAL,
      g_param_spec_int ("interval", "Interval between tone packets",
          "Interval in ms between two tone packets", MIN_PACKET_INTERVAL,
          MAX_PACKET_INTERVAL, DEFAULT_PACKET_INTERVAL, G_PARAM_READWRITE));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_dtmf_src_change_state);

  gstbasesrc_class->event =
      GST_DEBUG_FUNCPTR (gst_dtmf_src_handle_event);
  gstbasesrc_class->get_times =
      GST_DEBUG_FUNCPTR (gst_dtmf_src_get_times);
  gstbasesrc_class->create =
      GST_DEBUG_FUNCPTR (gst_dtmf_src_create);
}


static void
gst_dtmf_src_init (GstDTMFSrc * dtmfsrc, GstDTMFSrcClass *g_class)
{
  /* we operate in time */
  gst_base_src_set_format (GST_BASE_SRC (dtmfsrc), GST_FORMAT_TIME);
  gst_base_src_set_live (GST_BASE_SRC (dtmfsrc), TRUE);

  dtmfsrc->interval = DEFAULT_PACKET_INTERVAL;

  dtmfsrc->event_queue = g_async_queue_new ();
  dtmfsrc->last_event = NULL;

  dtmfsrc->clock_id = NULL;

  GST_DEBUG_OBJECT (dtmfsrc, "init done");
}

static void
gst_dtmf_src_finalize (GObject * object)
{
  GstDTMFSrc *dtmfsrc;

  dtmfsrc = GST_DTMF_SRC (object);

  if (dtmfsrc->event_queue) {
    g_async_queue_unref (dtmfsrc->event_queue);
    dtmfsrc->event_queue = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_dtmf_src_handle_dtmf_event (GstDTMFSrc *dtmfsrc,
        const GstStructure * event_structure)
{
  gint event_type;
  gboolean start;
  gint method;

  if (!gst_structure_get_int (event_structure, "type", &event_type) ||
      !gst_structure_get_boolean (event_structure, "start", &start) ||
      (start == TRUE && event_type != GST_TONE_DTMF_TYPE_EVENT))
    goto failure;

  if (gst_structure_get_int (event_structure, "method", &method)) {
    if (method != 2) {
      goto failure;
    }
  }

  if (start) {
    gint event_number;
    gint event_volume;

    if (!gst_structure_get_int (event_structure, "number", &event_number) ||
            !gst_structure_get_int (event_structure, "volume", &event_volume))
      goto failure;

    GST_DEBUG_OBJECT (dtmfsrc, "Received start event %d with volume %d",
            event_number, event_volume);
    gst_dtmf_src_add_start_event (dtmfsrc, event_number, event_volume);
  }

  else {
    GST_DEBUG_OBJECT (dtmfsrc, "Received stop event");
    gst_dtmf_src_add_stop_event (dtmfsrc);
  }

  return TRUE;
failure:
  return FALSE;
}

static gboolean
gst_dtmf_src_handle_custom_upstream (GstDTMFSrc *dtmfsrc,
    GstEvent * event)
{
  gboolean result = FALSE;
  const GstStructure *structure;
  GstState state;
  GstStateChangeReturn ret;

  ret = gst_element_get_state (GST_ELEMENT (dtmfsrc), &state, NULL, 0);
  if (ret != GST_STATE_CHANGE_SUCCESS || state != GST_STATE_PLAYING) {
    GST_DEBUG_OBJECT (dtmfsrc, "Received event while not in PLAYING state");
    goto ret;
  }

  GST_DEBUG_OBJECT (dtmfsrc, "Received event is of our interest");
  structure = gst_event_get_structure (event);
  if (structure && gst_structure_has_name (structure, "dtmf-event"))
    result = gst_dtmf_src_handle_dtmf_event (dtmfsrc, structure);

ret:
  return result;
}

static gboolean
gst_dtmf_src_handle_event (GstBaseSrc * src, GstEvent * event)
{
  GstDTMFSrc *dtmfsrc;
  gboolean result = FALSE;

  dtmfsrc = GST_DTMF_SRC (src);

  GST_DEBUG_OBJECT (dtmfsrc, "Received an event on the src pad");
  if (GST_EVENT_TYPE (event) == GST_EVENT_CUSTOM_UPSTREAM) {
    result = gst_dtmf_src_handle_custom_upstream (dtmfsrc, event);
  }

  return result;
}

static void
gst_dtmf_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDTMFSrc *dtmfsrc;

  dtmfsrc = GST_DTMF_SRC (object);

  switch (prop_id) {
    case PROP_INTERVAL:
      dtmfsrc->interval = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_dtmf_src_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstDTMFSrc *dtmfsrc;

  dtmfsrc = GST_DTMF_SRC (object);

  switch (prop_id) {
    case PROP_INTERVAL:
      g_value_set_uint (value, dtmfsrc->interval);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_dtmf_src_set_stream_lock (GstDTMFSrc *dtmfsrc, gboolean lock)
{
   GstPad *srcpad = GST_BASE_SRC_PAD (dtmfsrc);
   GstEvent *event;
   GstStructure *structure;

   structure = gst_structure_new ("stream-lock",
                      "lock", G_TYPE_BOOLEAN, lock, NULL);

   event = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM_OOB, structure);
   if (!gst_pad_push_event (srcpad, event)) {
     GST_WARNING_OBJECT (dtmfsrc, "stream-lock event not handled");
   }
}

static void
gst_dtmf_prepare_timestamps (GstDTMFSrc *dtmfsrc)
{
  GstClock *clock;
  GstClockTime base_time;

  base_time = GST_ELEMENT_CAST (dtmfsrc)->base_time;

  clock = gst_element_get_clock (GST_ELEMENT (dtmfsrc));
  if (clock != NULL) {
    dtmfsrc->timestamp = gst_clock_get_time (clock) - base_time;
    gst_object_unref (clock);
  } else {
    gchar *dtmf_name = gst_element_get_name (dtmfsrc);
    GST_ERROR_OBJECT (dtmfsrc, "No clock set for element %s", dtmf_name);
    dtmfsrc->timestamp = GST_CLOCK_TIME_NONE;
    g_free (dtmf_name);
  }
}

static void
gst_dtmf_src_add_start_event (GstDTMFSrc *dtmfsrc, gint event_number,
    gint event_volume)
{

  GstDTMFSrcEvent * event = g_malloc (sizeof(GstDTMFSrcEvent));
  event->event_type = DTMF_EVENT_TYPE_START;
  event->sample = 0;
  event->event_number = CLAMP (event_number, MIN_EVENT, MAX_EVENT);
  event->volume = CLAMP (event_volume, MIN_VOLUME, MAX_VOLUME);

  g_async_queue_push (dtmfsrc->event_queue, event);
}

static void
gst_dtmf_src_add_stop_event (GstDTMFSrc *dtmfsrc)
{

  GstDTMFSrcEvent * event = g_malloc (sizeof(GstDTMFSrcEvent));
  event->event_type = DTMF_EVENT_TYPE_STOP;
  event->sample = 0;
  event->event_number = 0;
  event->volume = 0;

  g_async_queue_push (dtmfsrc->event_queue, event);
}

static void
gst_dtmf_src_generate_silence(GstBuffer * buffer, float duration)
{
  gint buf_size;

  /* Create a buffer with data set to 0 */
  buf_size = ((duration/1000)*SAMPLE_RATE*SAMPLE_SIZE*CHANNELS)/8;
  GST_BUFFER_SIZE (buffer) = buf_size;
  GST_BUFFER_MALLOCDATA (buffer) = g_malloc0(buf_size);
  GST_BUFFER_DATA (buffer) = GST_BUFFER_MALLOCDATA (buffer);

}

static void
gst_dtmf_src_generate_tone(GstDTMFSrcEvent *event, DTMF_KEY key, float duration,
    GstBuffer * buffer)
{
  gint16 *p;
  gint tone_size;
  double i = 0;
  double amplitude, f1, f2;
  double volume_factor;

  /* Create a buffer for the tone */
  tone_size = ((duration/1000)*SAMPLE_RATE*SAMPLE_SIZE*CHANNELS)/8;
  GST_BUFFER_SIZE (buffer) = tone_size;
  GST_BUFFER_MALLOCDATA (buffer) = g_malloc(tone_size);
  GST_BUFFER_DATA (buffer) = GST_BUFFER_MALLOCDATA (buffer);

  p = (gint16 *) GST_BUFFER_MALLOCDATA (buffer);

  volume_factor = pow (10, (-event->volume) / 20);

  /*
   * For each sample point we calculate 'x' as the
   * the amplitude value.
   */
  for (i = 0; i < (tone_size / (SAMPLE_SIZE/8)); i++) {
    /*
     * We add the fundamental frequencies together.
     */
    f1 = sin(2 * M_PI * key.low_frequency * (event->sample / SAMPLE_RATE));
    f2 = sin(2 * M_PI * key.high_frequency * (event->sample / SAMPLE_RATE));

    amplitude = (f1 + f2) / 2;

    /* Adjust the volume */
    amplitude *= volume_factor;

    /* Make the [-1:1] interval into a [-32767:32767] interval */
    amplitude *= 32767;

    /* Store it in the data buffer */
    *(p++) = (gint16) amplitude;

    (event->sample)++;
  }
}

static void
gst_dtmf_src_get_times (GstBaseSrc * basesrc, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  /* for live sources, sync on the timestamp of the buffer */
  if (gst_base_src_is_live (basesrc)) {
    GstClockTime timestamp = GST_BUFFER_TIMESTAMP (buffer);

    if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
      /* get duration to calculate end time */
      GstClockTime duration = GST_BUFFER_DURATION (buffer);

      *start = timestamp;
      if (GST_CLOCK_TIME_IS_VALID (duration)) {
        *end = *start + duration;
      }
    }
  } else {
    *start = -1;
    *end = -1;
  }
}


static GstBuffer *
gst_dtmf_src_create_next_tone_packet (GstDTMFSrc *dtmfsrc,
    GstDTMFSrcEvent *event)
{
  GstBuffer *buf = NULL;
  gboolean send_silence = FALSE;
  GstPad *srcpad = GST_BASE_SRC_PAD (dtmfsrc);

  GST_DEBUG_OBJECT (dtmfsrc, "Creating buffer for tone %s",
      DTMF_KEYS[event->event_number].event_name);

  /* create buffer to hold the tone */
  buf = gst_buffer_new ();

  if (event->packet_count * dtmfsrc->interval < MIN_INTER_DIGIT_INTERVAL) {
    send_silence = TRUE;
  }

  if (send_silence) {
    GST_DEBUG_OBJECT (dtmfsrc,  "Generating silence");
    gst_dtmf_src_generate_silence (buf, dtmfsrc->interval);
  } else {
    GST_DEBUG_OBJECT (dtmfsrc,  "Generating tone");
    gst_dtmf_src_generate_tone(event, DTMF_KEYS[event->event_number],
        dtmfsrc->interval, buf);
  }
  event->packet_count++;


  /* timestamp and duration of GstBuffer */
  GST_BUFFER_DURATION (buf) = dtmfsrc->interval * GST_MSECOND;
  GST_BUFFER_TIMESTAMP (buf) = dtmfsrc->timestamp;
  dtmfsrc->timestamp += GST_BUFFER_DURATION (buf);

  /* Set caps on the buffer before pushing it */
  gst_buffer_set_caps (buf, GST_PAD_CAPS (srcpad));

  return buf;
}

static GstFlowReturn
gst_dtmf_src_create (GstBaseSrc * basesrc, guint64 offset,
    guint length, GstBuffer ** buffer)
{
  GstBuffer *buf = NULL;
  GstFlowReturn ret;
  GstDTMFSrcEvent *event;
  GstDTMFSrc * dtmfsrc;

  dtmfsrc = GST_DTMF_SRC (basesrc);

  g_async_queue_ref (dtmfsrc->event_queue);

 start:
  if (dtmfsrc->last_event == NULL) {
    GST_DEBUG_OBJECT (dtmfsrc, "popping");
    event = g_async_queue_pop (dtmfsrc->event_queue);

    GST_DEBUG_OBJECT (dtmfsrc, "popped %d", event->event_type);

    if (event->event_type == DTMF_EVENT_TYPE_STOP) {
      GST_WARNING_OBJECT (dtmfsrc,
          "Received a DTMF stop event when already stopped");
    } else if (event->event_type == DTMF_EVENT_TYPE_START) {
      gst_dtmf_prepare_timestamps (dtmfsrc);

      /* Don't forget to get exclusive access to the stream */
      gst_dtmf_src_set_stream_lock (dtmfsrc, TRUE);

      event->packet_count = 0;
      dtmfsrc->last_event = event;
    } else if (event->event_type == DTMF_EVENT_TYPE_PAUSE_TASK) {
      /*
       * We're pushing it back because it has to stay in there until
       * the task is really paused (and the queue will then be flushed)
       */
      GST_DEBUG_OBJECT (dtmfsrc, "pushing pause_task...");
      g_async_queue_push (dtmfsrc->event_queue, event);
      g_async_queue_unref (dtmfsrc->event_queue);
    }
  } else if (dtmfsrc->last_event->packet_count  * dtmfsrc->interval >=
      MIN_DUTY_CYCLE) {
    event = g_async_queue_try_pop (dtmfsrc->event_queue);

    if (event != NULL) {
      if (event->event_type == DTMF_EVENT_TYPE_START) {
        GST_WARNING_OBJECT (dtmfsrc,
            "Received two consecutive DTMF start events");
      } else if (event->event_type == DTMF_EVENT_TYPE_STOP) {
        gst_dtmf_src_set_stream_lock (dtmfsrc, FALSE);
        g_free (dtmfsrc->last_event);
        dtmfsrc->last_event = NULL;
        goto start;
      } else if (event->event_type == DTMF_EVENT_TYPE_PAUSE_TASK) {
        /*
         * We're pushing it back because it has to stay in there until
         * the task is really paused (and the queue will then be flushed)
         */
        GST_DEBUG_OBJECT (dtmfsrc, "pushing pause_task...");
        g_async_queue_push (dtmfsrc->event_queue, event);
        g_async_queue_unref (dtmfsrc->event_queue);
      }
    }
  }
  g_async_queue_unref (dtmfsrc->event_queue);

  GST_DEBUG_OBJECT (dtmfsrc, "end event check");

  if (dtmfsrc->last_event) {
    buf = gst_dtmf_src_create_next_tone_packet (dtmfsrc, dtmfsrc->last_event);

    GST_DEBUG_OBJECT (dtmfsrc, "Created buffer of size %d", GST_BUFFER_SIZE (buf));
    *buffer = buf;
    ret = GST_FLOW_OK;
  } else {
    *buffer = NULL;
    ret = GST_FLOW_WRONG_STATE;
  }

  GST_DEBUG_OBJECT (dtmfsrc, "returning");
  return ret;

}

static GstStateChangeReturn
gst_dtmf_src_change_state (GstElement * element, GstStateChange transition)
{
  GstDTMFSrc *dtmfsrc;
  GstStateChangeReturn result;
  gboolean no_preroll = FALSE;
  GstDTMFSrcEvent *event = NULL;

  dtmfsrc = GST_DTMF_SRC (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      /* Flushing the event queue */
      event = g_async_queue_try_pop (dtmfsrc->event_queue);

      while (event != NULL) {
        g_free (event);
        event = g_async_queue_try_pop (dtmfsrc->event_queue);
      }
      break;
    default:
      break;
  }

  if ((result =
          GST_ELEMENT_CLASS (parent_class)->change_state (element,
              transition)) == GST_STATE_CHANGE_FAILURE)
    goto failure;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      if (dtmfsrc->last_event) {
        /* Don't forget to release the stream lock */
        gst_dtmf_src_set_stream_lock (dtmfsrc, FALSE);
        g_free (dtmfsrc->last_event);
        dtmfsrc->last_event = NULL;
      }

      /* Flushing the event queue */
      event = g_async_queue_try_pop (dtmfsrc->event_queue);

      while (event != NULL) {
        g_free (event);
        event = g_async_queue_try_pop (dtmfsrc->event_queue);
      }

      /* Indicate that we don't do PRE_ROLL */
      no_preroll = TRUE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      event = g_malloc (sizeof(GstDTMFSrcEvent));
      event->event_type = DTMF_EVENT_TYPE_PAUSE_TASK;
      g_async_queue_push (dtmfsrc->event_queue, event);

      event = NULL;
    default:
      break;
  }

  if (no_preroll && result == GST_STATE_CHANGE_SUCCESS)
    result = GST_STATE_CHANGE_NO_PREROLL;

  return result;

  /* ERRORS */
failure:
  {
    GST_ERROR_OBJECT (dtmfsrc, "parent failed state change");
    return result;
  }
}

gboolean
gst_dtmf_src_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "dtmfsrc",
      GST_RANK_NONE, GST_TYPE_DTMF_SRC);
}

