/* Based on a plugin from Martin Soto's Seamless DVD Player.
 * Copyright (C) 2003, 2004 Martin Soto <martinsoto@users.sourceforge.net>
 *               2005-6 Michael Smith <msmith@fluendo.com>
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

#include <string.h>
#include <unistd.h>

#include <gst/gst.h>
#include <gst/audio/gstaudioclock.h>
#include <gst/base/gstbasesink.h>

#include "alsaspdifsink.h"

GST_DEBUG_CATEGORY_STATIC (alsaspdifsink_debug);
#define GST_CAT_DEFAULT (alsaspdifsink_debug)

/* The magic audio-type we pretend to be for AC3 output */
#define AC3_CHANNELS 2
#define AC3_BITS     16

/* Define AC3 FORMAT as big endian. Fall back to swapping
 * on sound devices that don't support it */
#define AC3_FORMAT_BE   SND_PCM_FORMAT_S16_BE
#define AC3_FORMAT_LE   SND_PCM_FORMAT_S16_LE

/* The size in bytes of an IEC958 frame. */
#define IEC958_FRAME_SIZE 6144

/* Size in bytes of an ALSA PCM frame (4, for this case). */
#define ALSASPDIFSINK_BYTES_PER_FRAME ((AC3_BITS / 8) * AC3_CHANNELS)

#define IEC958_SAMPLES_PER_FRAME (IEC958_FRAME_SIZE / ALSASPDIFSINK_BYTES_PER_FRAME)

#if 0
/* The duration of a single IEC958 frame. */
#define IEC958_FRAME_DURATION (32 * GST_MSECOND)

/* Maximal synchronization difference.  Measures will be taken if
   block timestamps differ from actual playing time in more than this
   value. */
#define MAX_SYNC_DIFF (IEC958_FRAME_DURATION * 0.8)

/* Playing time for the given number of ALSA PCM frames. */
#define ALSASPDIFSINK_TIME_PER_FRAMES(sink, frames) \
  (((GstClockTime) (frames) * GST_SECOND) / AC3_RATE)

/* Number of ALSA PCM frames for the given playing time. */
#define ALSASPDIFSINK_FRAMES_PER_TIME(sink, time) \
  (((GstClockTime) AC3_RATE * (time)) / GST_SECOND)
#endif

/* AlsaSPDIFSink signals and args */
enum
{
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_CARD,
  PROP_DEVICE
};

static GstStaticPadTemplate alsaspdifsink_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-iec958")
    );

#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (alsaspdifsink_debug, "alsaspdifsink", 0, \
            "ALSA S/PDIF audio sink element");

GST_BOILERPLATE_FULL (AlsaSPDIFSink, alsaspdifsink, GstBaseSink,
    GST_TYPE_BASE_SINK, _do_init);

static void alsaspdifsink_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void alsaspdifsink_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static gboolean alsaspdifsink_event (GstBaseSink * bsink, GstEvent * event);
static GstFlowReturn alsaspdifsink_render (GstBaseSink * bsink,
    GstBuffer * buf);
static void alsaspdifsink_get_times (GstBaseSink * bsink, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end);
static gboolean alsaspdifsink_set_caps (GstBaseSink * bsink, GstCaps * caps);

static gboolean alsaspdifsink_open (AlsaSPDIFSink * sink);
static void alsaspdifsink_close (AlsaSPDIFSink * sink);

static GstClock *alsaspdifsink_provide_clock (GstElement * elem);
static GstClockTime alsaspdifsink_get_time (GstClock * clock,
    gpointer user_data);
static void alsaspdifsink_dispose (GObject * object);
static void alsaspdifsink_finalize (GObject * object);

static GstStateChangeReturn alsaspdifsink_change_state (GstElement * element,
    GstStateChange transition);
static int alsaspdifsink_find_pcm_device (AlsaSPDIFSink * sink);
static gboolean alsaspdifsink_set_params (AlsaSPDIFSink * sink);
static snd_pcm_sframes_t alsaspdifsink_delay (AlsaSPDIFSink * sink);

/* Alsa error handler to suppress messages from within the ALSA library */
static void ignore_alsa_err (const char *file, int line, const char *function,
    int err, const char *fmt, ...);

static void
alsaspdifsink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class, "S/PDIF ALSA audiosink",
      "Sink/Audio",
      "Feeds audio to S/PDIF interfaces through the ALSA sound driver",
      "Martin Soto <martinsoto@users.sourceforge.net>, "
      "Michael Smith <msmith@fluendo.com>");
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&alsaspdifsink_sink_factory));
}

static void
alsaspdifsink_class_init (AlsaSPDIFSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;

  gobject_class->set_property = alsaspdifsink_set_property;
  gobject_class->get_property = alsaspdifsink_get_property;
  gobject_class->dispose = alsaspdifsink_dispose;
  gobject_class->finalize = alsaspdifsink_finalize;

  gstelement_class->change_state = alsaspdifsink_change_state;
  gstelement_class->provide_clock = alsaspdifsink_provide_clock;

  gstbasesink_class->event = alsaspdifsink_event;
  gstbasesink_class->render = alsaspdifsink_render;
  gstbasesink_class->get_times = alsaspdifsink_get_times;
  gstbasesink_class->set_caps = alsaspdifsink_set_caps;

#if 0
  /* We ignore the device property anyway, so don't install it
   * we don't want the user supplying just any device string for us. 
   * At most we might want a card number and an iec958.%d device name
   * to attempt */
  g_object_class_install_property (gobject_class, PROP_DEVICE,
      g_param_spec_string ("device", "Device",
          "ALSA device, as defined in an asound configuration file",
          "default", G_PARAM_READWRITE));
#endif
  g_object_class_install_property (gobject_class, PROP_CARD,
      g_param_spec_int ("card", "Card",
          "ALSA card number for the SPDIF device to use",
          0, G_MAXINT, 0, G_PARAM_READWRITE));

  snd_lib_error_set_handler (ignore_alsa_err);
}

static void
alsaspdifsink_init (AlsaSPDIFSink * sink, AlsaSPDIFSinkClass * g_class)
{
  /* Create the provided clock. */
  sink->clock = gst_audio_clock_new ("clock", alsaspdifsink_get_time, sink);

  sink->card = 0;
  sink->device = g_strdup ("default");
}

static void
alsaspdifsink_dispose (GObject * object)
{
  AlsaSPDIFSink *sink = ALSASPDIFSINK (object);

  if (sink->clock)
    gst_object_unref (sink->clock);
  sink->clock = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
alsaspdifsink_finalize (GObject * object)
{
  AlsaSPDIFSink *sink = ALSASPDIFSINK (object);

  g_free (sink->device);
  sink->device = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
alsaspdifsink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  AlsaSPDIFSink *sink;

  sink = ALSASPDIFSINK (object);

  switch (prop_id) {
      /*
         case PROP_DEVICE:
         if(sink->device)
         g_free(sink->device);
         sink->device = g_strdup(g_value_get_string(value));
         break;
       */
    case PROP_CARD:
      sink->card = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
alsaspdifsink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  AlsaSPDIFSink *sink;

  sink = ALSASPDIFSINK (object);

  switch (prop_id) {
      /*
         case PROP_DEVICE:
         g_value_set_string(value, sink->device);
         break;
       */
    case PROP_CARD:
      g_value_set_int (value, sink->card);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
alsaspdifsink_set_caps (GstBaseSink * bsink, GstCaps * caps)
{
  AlsaSPDIFSink *sink = ALSASPDIFSINK (bsink);

  if (!gst_structure_get_int (gst_caps_get_structure (caps, 0), "rate",
          &sink->rate))
    sink->rate = 48000;

  if (!alsaspdifsink_set_params (sink)) {
    GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_WRITE,
        ("Cannot set ALSA hardware parameters"), GST_ERROR_SYSTEM);
    return FALSE;
  }

  return TRUE;
}

static GstClock *
alsaspdifsink_provide_clock (GstElement * elem)
{
  AlsaSPDIFSink *sink = ALSASPDIFSINK (elem);

  return GST_CLOCK (gst_object_ref (sink->clock));
}

static GstClockTime
alsaspdifsink_get_time (GstClock * clock, gpointer user_data)
{
  GstClockTime result;
  snd_pcm_sframes_t raw, delay, samples;
  AlsaSPDIFSink *sink = ALSASPDIFSINK (user_data);

  raw = samples = sink->frames * IEC958_SAMPLES_PER_FRAME;
  delay = alsaspdifsink_delay (sink);

  if (samples > delay)
    samples -= delay;
  else
    samples = 0;

  result = gst_util_uint64_scale_int (samples, GST_SECOND, sink->rate);
  GST_LOG_OBJECT (sink, "Samples raw: %d, delay: %d, real: %d, "
      "Time: %" GST_TIME_FORMAT, (int) raw, (int) delay, (int) samples,
      GST_TIME_ARGS (result));
  return result;
}

static gboolean
alsaspdifsink_open (AlsaSPDIFSink * sink)
{
  char *pcm_name = sink->device;
  int err;
  char devstr[256];             /* Storage for local 'default' device string */

  /*
   * Try and open our default iec958 device. Fall back to searching on card x
   * if this fails, which should only happen on older alsa setups
   */

  /* The string will be one of these:
   * SPDIF_CON: Non-audio flag not set:
   *    spdif:{AES0 0x0 AES1 0x82 AES2 0x0 AES3 0x2}
   * SPDIF_CON: Non-audio flag set:
   *    spdif:{AES0 0x2 AES1 0x82 AES2 0x0 AES3 0x2}
   */
  sprintf (devstr,
      "iec958:{CARD %d AES0 0x%02x AES1 0x%02x AES2 0x%02x AES3 0x%02x}",
      sink->card,
      IEC958_AES0_NONAUDIO,
      IEC958_AES1_CON_ORIGINAL | IEC958_AES1_CON_PCM_CODER,
      0, IEC958_AES3_CON_FS_48000);

  GST_DEBUG_OBJECT (sink, "Generated device string \"%s\"", devstr);
  pcm_name = devstr;

  err = snd_pcm_open (&(sink->pcm), pcm_name, SND_PCM_STREAM_PLAYBACK, 0);
  if (err < 0) {
    GST_DEBUG_OBJECT (sink,
        "Open failed for %s - searching for IEC958 manually\n", pcm_name);

    err = alsaspdifsink_find_pcm_device (sink);
    if (err == 0 && sink->pcm == NULL)
      goto open_failed;
  }
  if (err < 0)
    goto failed;

  return TRUE;

  /* ERRORS */
open_failed:
  {
    GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_WRITE,
        ("Could not open IEC958/SPDIF output device"), GST_ERROR_SYSTEM);
    return FALSE;
  }
failed:
  {
    GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_WRITE,
        ("snd_pcm_open: %s", snd_strerror (err)), GST_ERROR_SYSTEM);
    return FALSE;
  }
}

static gboolean
alsaspdifsink_set_params (AlsaSPDIFSink * sink)
{
  snd_pcm_hw_params_t *params;
  unsigned int rate;
  int err;

  snd_pcm_hw_params_malloc (&params);

  err = snd_pcm_hw_params_any (sink->pcm, params);
  if (err < 0) {
    GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_WRITE,
        ("Broken configuration for this PCM: "
            "no configurations available"), GST_ERROR_SYSTEM);
    goto __error;
  }

  /* Set interleaved access. */
  err = snd_pcm_hw_params_set_access (sink->pcm, params,
      SND_PCM_ACCESS_RW_INTERLEAVED);
  if (err < 0) {
    GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_WRITE,
        ("Access type not available"), GST_ERROR_SYSTEM);
    goto __error;
  }

  err = snd_pcm_hw_params_set_format (sink->pcm, params, AC3_FORMAT_BE);
  if (err < 0) {
    /* Try LE output and swap data */
    GST_DEBUG_OBJECT (sink, "PCM format S16_BE not supported, trying S16_LE");
    err = snd_pcm_hw_params_set_format (sink->pcm, params, AC3_FORMAT_LE);
    sink->need_swap = TRUE;
  } else
    sink->need_swap = FALSE;

  if (err < 0) {
    GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_WRITE,
        ("Sample format not available"), GST_ERROR_SYSTEM);
    goto __error;
  }

  err = snd_pcm_hw_params_set_channels (sink->pcm, params, AC3_CHANNELS);
  if (err < 0) {
    GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_WRITE,
        ("Channels count not available"), GST_ERROR_SYSTEM);
    goto __error;
  }

  rate = sink->rate;
  GST_DEBUG_OBJECT (sink, "Setting S/PDIF sample rate: %d", rate);
  err = snd_pcm_hw_params_set_rate_near (sink->pcm, params, &rate, 0);
  if (err != 0) {
    GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_WRITE,
        ("Rate not available"), GST_ERROR_SYSTEM);
    goto __error;
  }

  err = snd_pcm_hw_params (sink->pcm, params);
  if (err < 0) {
    GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_WRITE,
        ("PCM hw_params failed: %s", snd_strerror (err)), GST_ERROR_SYSTEM);
    goto __error;
  }

  snd_pcm_hw_params_free (params);

  return TRUE;

  /* ERRORS */
__error:
  {
    snd_pcm_hw_params_free (params);
    return FALSE;
  }
}

static void
alsaspdifsink_close (AlsaSPDIFSink * sink)
{
  if (sink->pcm) {
    snd_pcm_close (sink->pcm);
    sink->pcm = NULL;
  }
}

/* Try and find an IEC958 PCM device and mixer on card 0 and open it
 * This function is only used on older ALSA installs that don't have the
 * correct iec958 alias stuff set up, and relies on there being only
 * one IEC958 PCM device (relies IEC958 in the device name) and one IEC958
 * mixer control for doing the settings.
 */
static int
alsaspdifsink_find_pcm_device (AlsaSPDIFSink * sink)
{
  int err = -1, dev, idx, count;
  const gchar *ctl_name = "hw:0";
  const gchar *spdif_name = SND_CTL_NAME_IEC958 ("", PLAYBACK, NONE);
  int card = sink->card;
  gchar pcm_name[24];
  snd_pcm_t *pcm = NULL;
  snd_ctl_t *ctl = NULL;
  snd_ctl_card_info_t *info = NULL;
  snd_ctl_elem_list_t *clist = NULL;
  snd_ctl_elem_id_t *cid = NULL;
  snd_pcm_info_t *pinfo = NULL;

  GST_WARNING ("Opening IEC958 named device failed. Trying to autodetect");

  if ((err = snd_ctl_open (&ctl, ctl_name, card)) < 0)
    return err;

  snd_ctl_card_info_malloc (&info);
  snd_pcm_info_malloc (&pinfo);

  /* Find a mixer for IEC958 settings */
  snd_ctl_elem_list_malloc (&clist);
  if ((err = snd_ctl_elem_list (ctl, clist)) < 0)
    goto beach;

  if ((err =
          snd_ctl_elem_list_alloc_space (clist,
              snd_ctl_elem_list_get_count (clist))) < 0)
    goto beach;
  if ((err = snd_ctl_elem_list (ctl, clist)) < 0)
    goto beach;

  count = snd_ctl_elem_list_get_used (clist);
  for (idx = 0; idx < count; idx++) {
    if (strstr (snd_ctl_elem_list_get_name (clist, idx), spdif_name) != NULL)
      break;
  }
  if (idx == count) {
    /* No SPDIF mixer availble */
    err = 0;
    goto beach;
  }
  snd_ctl_elem_id_malloc (&cid);
  snd_ctl_elem_list_get_id (clist, idx, cid);

  /* Now find a PCM device for IEC 958 */
  if ((err = snd_ctl_card_info (ctl, info)) < 0)
    goto beach;
  dev = -1;
  do {
    if (snd_ctl_pcm_next_device (ctl, &dev) < 0)
      goto beach;
    if (dev < 0)
      break;                    /* No more devices */

    /* Filter for playback devices */
    snd_pcm_info_set_device (pinfo, dev);
    snd_pcm_info_set_subdevice (pinfo, 0);
    snd_pcm_info_set_stream (pinfo, SND_PCM_STREAM_PLAYBACK);
    if ((err = snd_ctl_pcm_info (ctl, pinfo)) < 0) {
      if (err != -ENOENT)
        goto beach;             /* Genuine error */

      /* Device has no playback streams */
      continue;
    }
    if (strstr (snd_pcm_info_get_name (pinfo), "IEC958") == NULL)
      continue;                 /* Not the device we are looking for */

    count = snd_pcm_info_get_subdevices_count (pinfo);
    GST_LOG_OBJECT (sink, "Device %d has %d subdevices\n", dev,
        snd_pcm_info_get_subdevices_count (pinfo));
    for (idx = 0; idx < count; idx++) {
      snd_pcm_info_set_subdevice (pinfo, idx);

      if ((err = snd_ctl_pcm_info (ctl, pinfo)) < 0)
        goto beach;

      g_assert (snd_pcm_info_get_stream (pinfo) == SND_PCM_STREAM_PLAYBACK);

      GST_LOG_OBJECT (sink, "Found playback stream on dev %d sub-d %d\n", dev,
          idx);

      /* Found a suitable PCM device, let's open it */
      g_snprintf (pcm_name, 24, "hw:%d,%d", card, dev);
      if ((err =
              snd_pcm_open (&(pcm), pcm_name, SND_PCM_STREAM_PLAYBACK, 0)) < 0)
        goto beach;

      break;
    }
  } while (pcm == NULL);

  if (pcm != NULL) {
    snd_ctl_elem_value_t *cval;
    snd_aes_iec958_t iec958;

    /* Have a PCM device and a mixer, set things up */
    snd_ctl_elem_value_malloc (&cval);
    snd_ctl_elem_value_set_id (cval, cid);
    snd_ctl_elem_value_get_iec958 (cval, &iec958);
    iec958.status[0] = IEC958_AES0_NONAUDIO;
    iec958.status[1] = IEC958_AES1_CON_ORIGINAL | IEC958_AES1_CON_PCM_CODER;
    iec958.status[2] = 0;
    iec958.status[3] = IEC958_AES3_CON_FS_48000;
    snd_ctl_elem_value_set_iec958 (cval, &iec958);
    snd_ctl_elem_value_free (cval);

    sink->pcm = pcm;
    pcm = NULL;
    err = 0;
  }

beach:
  if (pcm)
    snd_pcm_close (pcm);
  if (clist)
    snd_ctl_elem_list_clear (clist);
  if (ctl)
    snd_ctl_close (ctl);
  if (clist)
    snd_ctl_elem_list_free (clist);
  if (cid)
    snd_ctl_elem_id_free (cid);
  if (info)
    snd_ctl_card_info_free (info);
  if (pinfo)
    snd_pcm_info_free (pinfo);
  return err;
}

static void
alsaspdifsink_write_frame (AlsaSPDIFSink * sink, guchar * buf)
{
  snd_pcm_sframes_t res;
  int num_frames = IEC958_FRAME_SIZE / ALSASPDIFSINK_BYTES_PER_FRAME;

  /* If we couldn't output big endian when we opened the devic, then
   * we need to swap here */
  if (sink->need_swap) {
    int i;
    guchar tmp;

    for (i = 0; i < IEC958_FRAME_SIZE; i += 2) {
      tmp = buf[i];
      buf[i] = buf[i + 1];
      buf[i + 1] = tmp;
    }
  }

  res = 0;
  do {
    if (res == -EPIPE) {
      /* Underrun. */
      GST_INFO_OBJECT (sink, "buffer underrun");
      res = snd_pcm_prepare (sink->pcm);
    } else if (res == -ESTRPIPE) {
      /* Suspend. */
      while ((res = snd_pcm_resume (sink->pcm)) == -EAGAIN) {
        GST_DEBUG_OBJECT (sink, "sleeping for suspend");
        g_usleep (100000);
      }

      if (res < 0) {
        res = snd_pcm_prepare (sink->pcm);
      }
    }

    if (res >= 0) {
      res = snd_pcm_writei (sink->pcm, (void *) buf, num_frames);
    }

    if (res > 0) {
      num_frames -= res;
    }

  } while (res == -EPIPE || num_frames > 0);

  sink->frames++;

  if (res < 0) {
    GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_WRITE,
        ("writei returned error: %s", snd_strerror (res)), GST_ERROR_SYSTEM);
    return;
  }
}

static gboolean
alsaspdifsink_event (GstBaseSink * bsink, GstEvent * event)
{
  AlsaSPDIFSink *sink = ALSASPDIFSINK (bsink);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      snd_pcm_drop (sink->pcm);
      break;
    case GST_EVENT_FLUSH_STOP:
      snd_pcm_start (sink->pcm);
      break;
    default:
      break;
  }

  return TRUE;
}

static void
alsaspdifsink_get_times (GstBaseSink * bsink, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  /* Like GstBaseAudioSink, we set these to NONE */
  *start = GST_CLOCK_TIME_NONE;
  *end = GST_CLOCK_TIME_NONE;
}

static snd_pcm_sframes_t
alsaspdifsink_delay (AlsaSPDIFSink * sink)
{
  snd_pcm_sframes_t delay;
  int err;

  err = snd_pcm_delay (sink->pcm, &delay);
  if (err < 0 || delay < 0) {
    return 0;
  }

  return delay;
}

#if 0
static void
generate_iec958_zero_frame (guchar * buffer)
{
  /* 2 sync words, 16 bits each */
  buffer[0] = 0xF8;
  buffer[1] = 0x72;
  buffer[2] = 0x4E;
  buffer[3] = 0x1F;

  /* 16-bit burst-info. Contains data type (zero here, for 'null data'),
     stream number (we output '0' for this always), and a few other bits.
     As it happens, all-zero is the correct value.
   */
  buffer[4] = 0;
  buffer[5] = 0;

  /* 16-bit frame size. Also zero */
  buffer[6] = 0;
  buffer[7] = 0;

  memset (buffer + 8, 0, IEC958_FRAME_SIZE - 8);
}
#endif

static GstFlowReturn
alsaspdifsink_render (GstBaseSink * bsink, GstBuffer * buf)
{
  AlsaSPDIFSink *sink = ALSASPDIFSINK (bsink);

#if 0
  GstClockTime next_write;

  if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_TIMESTAMP (buf)))
    sink->cur_ts = GST_BUFFER_TIMESTAMP (buf);

  next_write = gst_element_get_time (GST_ELEMENT (sink)) +
      alsaspdifsink_current_delay (sink);

  /*     
     fprintf (stderr, "Drift: % 0.6fs, delay: % 0.6fs\r",  
     GST_TIME_ARGS (GST_CLOCK_DIFF (sink->cur_ts, next_write)),
     GST_TIME_ARGS (alsaspdifsink_current_delay (sink)));
   */

  /* If we're too far behind, send empty IEC958 frames. */
  if (sink->cur_ts > next_write + MAX_SYNC_DIFF) {
    int frames = (int) (
        ((double) (sink->cur_ts - next_write)) /
        (double) IEC958_FRAME_DURATION + 0.5);
    int i;

    for (i = 0; i < frames; i++) {
      static guchar frame[IEC958_FRAME_SIZE];

      generate_iec958_zero_frame (frame);

      alsaspdifsink_write_frame (sink, frame);
    }
  }
  /* If we're too far ahead, just drop this buffer */
  else if (sink->cur_ts + MAX_SYNC_DIFF < next_write) {
    goto end;
  }
#endif

  GST_LOG_OBJECT (sink, "Writing %d bytes to spdif out", GST_BUFFER_SIZE (buf));
  if (GST_BUFFER_SIZE (buf) == IEC958_FRAME_SIZE)
    alsaspdifsink_write_frame (sink, GST_BUFFER_DATA (buf));
  else
    GST_WARNING_OBJECT (sink, "Ignoring buffer of incorrect size");

#if 0
end:
  if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_DURATION (buf)))
    sink->cur_ts = GST_BUFFER_DURATION (buf);
#endif

  return GST_FLOW_OK;
}

/* Drop error output from within alsalib on the floor */
static void
ignore_alsa_err (const char *file, int line, const char *function,
    int err, const char *fmt, ...)
{
}

static GstStateChangeReturn
alsaspdifsink_change_state (GstElement * element, GstStateChange transition)
{
  AlsaSPDIFSink *sink = ALSASPDIFSINK (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      sink->frames = 0;
      gst_audio_clock_reset (GST_AUDIO_CLOCK (sink->clock), 0);
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!alsaspdifsink_open (sink)) {
        GST_WARNING_OBJECT (sink, "Failed to open alsa device");
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  GST_INFO_OBJECT (sink, "Parent change_state returned %d", ret);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      alsaspdifsink_close (sink);
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  /* no rank so it doesn't get autoplugged by autoaudiosink */
  if (!gst_element_register (plugin, "alsaspdifsink", GST_RANK_NONE,
          GST_TYPE_ALSASPDIFSINK)) {
    return FALSE;
  }
  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "alsaspdif",
    "Alsa plugin for S/PDIF output",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
